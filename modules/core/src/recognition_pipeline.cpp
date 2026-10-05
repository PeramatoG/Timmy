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

#include "timmy/command/transcript_normalizer.h"

#include <utility>

namespace timmy::core {

    // Connects a selected mono audio source to a backend-neutral ASR pipeline.
    RecognitionPipeline::RecognitionPipeline(
        std::shared_ptr<audio::AudioChannelSelector> audioSource,
        std::shared_ptr<asr::IAsrBackend> backend)
        : audioSource_(std::move(audioSource)),
          formatAdapter_(std::make_shared<audio::AudioFormatAdapter>()),
          backend_(std::move(backend))
    {
        backend_->setResultListener(
            std::shared_ptr<asr::IAsrResultListener>(
                static_cast<asr::IAsrResultListener*>(this),
                [](auto*) {}));
        formatAdapter_->addConsumer(backend_);
        audioSource_->addConsumer(formatAdapter_);
    }

    // Stops recognition and disconnects the pipeline from its audio source.
    RecognitionPipeline::~RecognitionPipeline()
    {
        stop();
        backend_->setResultListener(nullptr);
        audioSource_->removeConsumer(formatAdapter_);
        formatAdapter_->removeConsumer(backend_);
    }

    // Replaces the observer receiving backend-neutral pipeline events.
    void RecognitionPipeline::setObserver(
        std::shared_ptr<IRecognitionPipelineObserver> observer)
    {
        std::lock_guard lock(observerMutex_);
        observer_ = std::move(observer);
    }

    // Starts the configured ASR backend and reports the resulting state.
    asr::AsrOperationResult RecognitionPipeline::start(
        const asr::AsrConfiguration& configuration)
    {
        const auto result = backend_->start(configuration);
        if (!result.succeeded)
        {
            publishState({
                RecognitionPipelineState::error,
                result.diagnostic.value_or(asr::AsrDiagnostic{
                    asr::AsrDiagnosticCode::initializationFailure,
                    "ASR startup failed."
                })
            });
            return result;
        }

        publishState({ RecognitionPipelineState::listening, std::nullopt });
        return result;
    }

    // Stops ASR processing and resets buffered format-conversion state.
    void RecognitionPipeline::stop()
    {
        backend_->stop();
        formatAdapter_->reset();
        publishState({ RecognitionPipelineState::stopped, std::nullopt });
    }

    // Returns the most recently published pipeline state.
    RecognitionPipelineState RecognitionPipeline::state() const noexcept
    {
        std::lock_guard lock(stateMutex_);
        return state_;
    }

    // Normalizes final ASR results and publishes raw and normalized text together.
    void RecognitionPipeline::onAsrResult(const asr::AsrResult& result)
    {
        if (result.kind != asr::AsrResultKind::final)
        {
            return;
        }

        const command::TranscriptNormalizer normalizer;
        const auto normalized = normalizer.normalize(result.transcript);
        const auto currentObserver = observer();
        if (currentObserver)
        {
            currentObserver->onRecognitionTranscript({
                normalized.originalText,
                normalized.normalizedText
            });
        }
    }

    // Converts asynchronous ASR diagnostics into an observable error state.
    void RecognitionPipeline::onAsrDiagnostic(
        const asr::AsrDiagnostic& diagnostic)
    {
        publishState({ RecognitionPipelineState::error, diagnostic });
    }

    // Stores and publishes a backend-neutral lifecycle state.
    void RecognitionPipeline::publishState(
        RecognitionPipelineStatus status)
    {
        {
            std::lock_guard lock(stateMutex_);
            state_ = status.state;
        }

        const auto currentObserver = observer();
        if (currentObserver)
        {
            currentObserver->onRecognitionStateChanged(status);
        }
    }

    // Takes a stable shared snapshot of the current observer.
    std::shared_ptr<IRecognitionPipelineObserver>
    RecognitionPipeline::observer() const
    {
        std::lock_guard lock(observerMutex_);
        return observer_;
    }

    // Creates the shared backend-neutral hints for short lighting commands.
    asr::RecognitionHints makeDefaultCommandRecognitionHints()
    {
        return {
            {
                "channel", "canal", "fixture", "group", "grupo",
                "at", "al", "full", "select", "selecciona", "set",
                "pon", "up", "sube", "down", "baja"
            },
            {
                "Canal 4 al 100%",
                "Grupo 5 al 70%",
                "Pon el grupo 5 al 70%",
                "Selecciona el canal 25",
                "Selecciona los canales del 20 al 30",
                "Canal 1, Canal 2 y Canal 3 al 40%",
                "Sube el canal 5 al 50%",
                "Baja el grupo 2 al 20%",
                "Channel 4 at 50%",
                "Set group 5 at 70%",
                "Select channels 20 through 30"
            },
            asr::RecognitionConstraintIntent::commandPhrase
        };
    }

} // namespace timmy::core
