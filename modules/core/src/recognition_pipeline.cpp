/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/recognition_pipeline.h"

#include "timmy/command/command_assembler.h"

#include <utility>

namespace timmy::core {

    namespace {

        // Converts the command-layer result to the public Core observer state.
        RecognitionAssemblyState coreAssemblyState(command::AssemblyState state)
        {
            switch (state)
            {
            case command::AssemblyState::pending:
                return RecognitionAssemblyState::pending;
            case command::AssemblyState::complete:
                return RecognitionAssemblyState::complete;
            case command::AssemblyState::rejected:
                return RecognitionAssemblyState::rejected;
            }
            return RecognitionAssemblyState::rejected;
        }

        // Converts a command-layer parse result to the public Core observer state.
        RecognitionSemanticState coreSemanticState(
            command::SemanticParseState state)
        {
            switch (state)
            {
            case command::SemanticParseState::produced:
                return RecognitionSemanticState::produced;
            case command::SemanticParseState::unsupportedOrInvalid:
                return RecognitionSemanticState::unsupportedOrInvalid;
            }
            return RecognitionSemanticState::unsupportedOrInvalid;
        }

    } // namespace

    class RecognitionPipeline::BackendListener final
        : public asr::IAsrResultListener
    {
    public:
        explicit BackendListener(RecognitionPipeline& owner)
            : owner_(owner)
        {
        }

        // Forwards backend results into the frontend-independent pipeline.
        void onAsrResult(const asr::AsrResult& result) override
        {
            owner_.handleResult(result);
        }

        // Forwards structured backend diagnostics without losing their code.
        void onAsrDiagnostic(const asr::AsrDiagnostic& diagnostic) override
        {
            owner_.handleDiagnostic(diagnostic);
        }

    private:
        RecognitionPipeline& owner_;
    };

    // Creates a pipeline attached to an existing selected audio channel.
    RecognitionPipeline::RecognitionPipeline(
        std::shared_ptr<audio::AudioChannelSelector> channelSelector,
        std::shared_ptr<asr::IAsrBackend> backend)
        : channelSelector_(std::move(channelSelector)),
          formatAdapter_(std::make_shared<audio::AudioFormatAdapter>()),
          backend_(std::move(backend)),
          backendListener_(std::make_shared<BackendListener>(*this)),
          assembler_(std::make_unique<command::CommandAssembler>())
    {
        backend_->setResultListener(backendListener_);
        formatAdapter_->addConsumer(backend_);
    }

    // Stops all recognition work before releasing observer and backend links.
    RecognitionPipeline::~RecognitionPipeline()
    {
        stop();
        backend_->setResultListener(nullptr);
        setObserver(nullptr);
    }

    // Replaces the observer used for state, diagnostics, and transcripts.
    void RecognitionPipeline::setObserver(
        std::shared_ptr<IRecognitionPipelineObserver> observer)
    {
        std::lock_guard lock(mutex_);
        observer_ = std::move(observer);
    }

    // Returns the current pipeline lifecycle state.
    RecognitionPipelineState RecognitionPipeline::state() const
    {
        std::lock_guard lock(mutex_);
        return state_;
    }

    // Starts the backend and then connects it to the selected channel.
    asr::AsrOperationResult RecognitionPipeline::start(
        const asr::AsrConfiguration& configuration)
    {
        stop();
        discardPendingAssembly();
        publishState(RecognitionPipelineState::starting);
        formatAdapter_->reset();

        const auto result = backend_->start(configuration);
        if (!result.succeeded)
        {
            publishState(RecognitionPipelineState::failed);
            if (result.diagnostic)
            {
                handleDiagnostic(*result.diagnostic);
            }
            return result;
        }

        channelSelector_->addConsumer(formatAdapter_);
        {
            std::lock_guard lock(mutex_);
            attached_ = true;
        }
        publishState(RecognitionPipelineState::listening);
        return result;
    }

    // Disconnects audio before joining backend-owned recognition workers.
    asr::AsrOperationResult RecognitionPipeline::stop()
    {
        const auto currentState = state();
        if (currentState == RecognitionPipelineState::stopped)
        {
            discardPendingAssembly();
            return { true, std::nullopt };
        }

        publishState(RecognitionPipelineState::stopping);
        bool wasAttached = false;
        {
            std::lock_guard lock(mutex_);
            wasAttached = attached_;
            attached_ = false;
        }
        if (wasAttached)
        {
            channelSelector_->removeConsumer(formatAdapter_);
        }
        formatAdapter_->reset();

        const auto result = backend_->stop();
        discardPendingAssembly();
        publishState(result.succeeded
            ? RecognitionPipelineState::stopped
            : RecognitionPipelineState::failed);
        if (!result.succeeded && result.diagnostic)
        {
            handleDiagnostic(*result.diagnostic);
        }
        return result;
    }

    // Normalizes and assembles each final transcript before publishing it.
    void RecognitionPipeline::handleResult(const asr::AsrResult& result)
    {
        if (result.kind != asr::AsrResultKind::final
            || state() != RecognitionPipelineState::listening)
        {
            return;
        }

        const auto normalized = normalizer_.normalize(result.transcript);
        command::AssemblyResult assembly;
        command::SemanticParseResult semantic;
        RecognitionSemanticState semanticState =
            RecognitionSemanticState::notAttempted;
        std::shared_ptr<IRecognitionPipelineObserver> currentObserver;
        {
            std::lock_guard lock(mutex_);
            if (state_ != RecognitionPipelineState::listening)
            {
                return;
            }
            assembly = assembler_->accept(normalized.normalizedText);
            if (assembly.state == command::AssemblyState::complete)
            {
                semantic = semanticParser_.parse(assembly.text);
                semanticState = coreSemanticState(semantic.state);
            }
            currentObserver = observer_;
        }
        if (currentObserver)
        {
            currentObserver->onRecognitionTranscript({
                normalized.originalText,
                normalized.normalizedText,
                coreAssemblyState(assembly.state),
                std::move(assembly.text),
                semanticState,
                std::move(semantic.command)
            });
        }
    }

    // Discards any incomplete command retained by the shared pipeline.
    void RecognitionPipeline::discardPendingAssembly()
    {
        std::lock_guard lock(mutex_);
        assembler_->reset();
    }

    // Publishes an unchanged backend diagnostic to the current observer.
    void RecognitionPipeline::handleDiagnostic(
        const asr::AsrDiagnostic& diagnostic)
    {
        if (const auto currentObserver = observer())
        {
            currentObserver->onRecognitionDiagnostic(diagnostic);
        }
    }

    // Updates lifecycle state before notifying the current observer.
    void RecognitionPipeline::publishState(RecognitionPipelineState state)
    {
        std::shared_ptr<IRecognitionPipelineObserver> currentObserver;
        {
            std::lock_guard lock(mutex_);
            state_ = state;
            currentObserver = observer_;
        }
        if (currentObserver)
        {
            currentObserver->onRecognitionStateChanged(state);
        }
    }

    // Returns a stable observer snapshot for a worker-thread callback.
    std::shared_ptr<IRecognitionPipelineObserver> RecognitionPipeline::observer() const
    {
        std::lock_guard lock(mutex_);
        return observer_;
    }

} // namespace timmy::core
