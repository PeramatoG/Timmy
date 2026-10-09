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
#include <cmath>
#include <algorithm>

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

    class RecognitionPipeline::AudioInput final : public audio::IAudioConsumer
    {
    public:
        AudioInput(std::shared_ptr<audio::AudioFormatAdapter> adapter, bool measure)
            : adapter_(std::move(adapter)), measure_(measure) {}
        void onAudioBlock(const audio::AudioBlockView& block) override
        {
            std::lock_guard lock(gateMutex);
            if (!enabled) return;
            if (measure_)
            {
                std::lock_guard statsLock(statsMutex);
                if (block.format.channelCount == 1 && block.format.sampleRate != 0)
                {
                    const auto count = std::min(block.frameCount, block.interleavedSamples.size());
                    diagnostics.audioSamples += count;
                    diagnostics.audioDurationSeconds += static_cast<double>(count) / block.format.sampleRate;
                    for (float sample : block.interleavedSamples.first(count))
                    {
                        diagnostics.inputPeak = std::max(diagnostics.inputPeak, std::abs(static_cast<double>(sample)));
                        squares += static_cast<double>(sample) * sample;
                    }
                    diagnostics.inputRms = diagnostics.audioSamples
                        ? std::sqrt(squares / static_cast<double>(diagnostics.audioSamples)) : 0;
                }
            }
            adapter_->onAudioBlock(block);
        }
        asr::AttemptDiagnostics snapshot() const
        { std::lock_guard lock(statsMutex); return diagnostics; }
        void reset()
        {
            std::lock_guard lock(gateMutex);
            enabled = false;
            adapter_->reset();
            std::lock_guard statsLock(statsMutex);
            diagnostics = {};
            squares = 0;
        }
        // Recursive only to permit synchronous test/alternative-backend callbacks
        // to close acceptance while the audio submission is still on the stack.
        std::recursive_mutex gateMutex;
        bool enabled = false;
    private:
        std::shared_ptr<audio::AudioFormatAdapter> adapter_;
        mutable std::mutex statsMutex;
        asr::AttemptDiagnostics diagnostics;
        double squares = 0;
        const bool measure_;
    };

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

        void onAsrProgress(const asr::RecognitionProgress& progress) override
        { owner_.handleProgress(progress); }

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
        std::shared_ptr<asr::IAsrBackend> backend, command::CommandProfile profile, bool isolateUtterances, bool benchmarkAttempts)
        : channelSelector_(std::move(channelSelector)),
          formatAdapter_(std::make_shared<audio::AudioFormatAdapter>()),
          audioInput_(std::make_shared<AudioInput>(formatAdapter_, benchmarkAttempts)),
          backend_(std::move(backend)),
          backendListener_(std::make_shared<BackendListener>(*this)),
          profile_(std::move(profile)),
          normalizer_(profile_.normalization),
          assembler_(std::make_unique<command::CommandAssembler>(profile_.parsing)),
          semanticParser_(profile_.parsing),
          isolateUtterances_(isolateUtterances), benchmarkAttempts_(benchmarkAttempts)
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

        {
            std::lock_guard lock(audioInput_->gateMutex);
            audioInput_->enabled = !benchmarkAttempts_;
        }
        channelSelector_->addConsumer(audioInput_);
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
            channelSelector_->removeConsumer(audioInput_);
        }
        disarmAttempt();
        audioInput_->reset();

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
        command::IntentValidationResult validation;
        command::IntentRenderingResult rendering;
        RecognitionSemanticState semanticState =
            RecognitionSemanticState::notAttempted;
        std::shared_ptr<IRecognitionPipelineObserver> currentObserver;
        {
            std::lock_guard lock(mutex_);
            if (state_ != RecognitionPipelineState::listening
                || (benchmarkAttempts_ && (!attemptArmed_ || result.generation != attemptGeneration_)))
            {
                return;
            }
            if (isolateUtterances_)
            {
                assembler_->reset();
                commandContext_.reset();
            }
            assembly = assembler_->accept(normalized.normalizedText, commandContext_);
            if (assembly.state == command::AssemblyState::complete)
            {
                semantic = semanticParser_.parse(assembly.text, commandContext_);
                semanticState = coreSemanticState(semantic.state);
                if (semantic.command) {
                    validation = command::validateIntent(*semantic.command, profile_.validation);
                    if (validation.accepted) {
                        rendering = command::renderIntent(*semantic.command, profile_.rendering);
                        commandContext_.observe(semantic);
                    }
                }
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
                std::move(semantic.command),
                std::move(validation),
                std::move(rendering),
                result.detectedLanguage,
                result.metrics,
                result.generation,
                attemptDiagnostics()
            });
        }
    }

    asr::AsrOperationResult RecognitionPipeline::prepareAttempt(asr::AttemptGeneration generation)
    {
        disarmAttempt();
        audioInput_->reset();
        {
            std::lock_guard lock(mutex_);
            attemptGeneration_ = generation;
            assembler_->reset();
            commandContext_.reset();
        }
        return backend_->resetAttempt(generation);
    }
    void RecognitionPipeline::armAttempt()
    {
        asr::AttemptGeneration generation;
        {
            std::lock_guard gateLock(audioInput_->gateMutex);
            std::lock_guard lock(mutex_);
            attemptArmed_ = true;
            audioInput_->enabled = true;
            generation = attemptGeneration_;
        }
        handleProgress({asr::RecognitionProgressState::waitingForSpeech, generation});
    }
    void RecognitionPipeline::disarmAttempt()
    {
        std::lock_guard gateLock(audioInput_->gateMutex);
        std::lock_guard lock(mutex_);
        attemptArmed_ = false;
        audioInput_->enabled = false;
    }
    asr::AttemptDiagnostics RecognitionPipeline::attemptDiagnostics() const
    {
        auto diagnostics = audioInput_->snapshot();
        const auto backend = backend_->attemptDiagnostics();
        diagnostics.droppedAudioBlocks = backend.droppedAudioBlocks;
        diagnostics.droppedVadFrames = backend.droppedVadFrames;
        diagnostics.droppedInferenceJobs = backend.droppedInferenceJobs;
        return diagnostics;
    }
    void RecognitionPipeline::handleProgress(const asr::RecognitionProgress& progress)
    {
        std::shared_ptr<IRecognitionPipelineObserver> currentObserver;
        {
            std::lock_guard lock(mutex_);
            if (benchmarkAttempts_ && (progress.generation != attemptGeneration_
                || (!attemptArmed_ && progress.state != asr::RecognitionProgressState::backendReady))) return;
            currentObserver = observer_;
        }
        if (currentObserver) currentObserver->onRecognitionProgress(progress);
    }

    // Starts a fresh linguistic session without retaining an incomplete command.
    void RecognitionPipeline::discardPendingAssembly()
    {
        std::lock_guard lock(mutex_);
        assembler_->reset();
        commandContext_.reset();
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
