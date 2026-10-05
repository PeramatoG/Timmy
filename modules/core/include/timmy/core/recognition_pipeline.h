/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "timmy/asr/asr_backend.h"
#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_format_adapter.h"
#include "timmy/command/transcript_normalizer.h"

#include <memory>
#include <mutex>
#include <string>

namespace timmy::core {

    enum class RecognitionPipelineState
    {
        stopped,
        starting,
        listening,
        stopping,
        failed
    };

    struct RecognitionTranscript
    {
        std::string rawTranscript;
        std::string normalizedTranscript;
    };

    class IRecognitionPipelineObserver
    {
    public:
        virtual ~IRecognitionPipelineObserver() = default;
        virtual void onRecognitionStateChanged(RecognitionPipelineState state) = 0;
        virtual void onRecognitionDiagnostic(const asr::AsrDiagnostic& diagnostic) = 0;
        virtual void onRecognitionTranscript(const RecognitionTranscript& transcript) = 0;
    };

    class RecognitionPipeline final
    {
    public:
        RecognitionPipeline(
            std::shared_ptr<audio::AudioChannelSelector> channelSelector,
            std::shared_ptr<asr::IAsrBackend> backend);
        ~RecognitionPipeline();

        RecognitionPipeline(const RecognitionPipeline&) = delete;
        RecognitionPipeline& operator=(const RecognitionPipeline&) = delete;

        void setObserver(std::shared_ptr<IRecognitionPipelineObserver> observer);
        [[nodiscard]] RecognitionPipelineState state() const;
        asr::AsrOperationResult start(const asr::AsrConfiguration& configuration);
        asr::AsrOperationResult stop();

    private:
        class BackendListener;
        friend class BackendListener;

        void handleResult(const asr::AsrResult& result);
        void handleDiagnostic(const asr::AsrDiagnostic& diagnostic);
        void publishState(RecognitionPipelineState state);
        std::shared_ptr<IRecognitionPipelineObserver> observer() const;

        std::shared_ptr<audio::AudioChannelSelector> channelSelector_;
        std::shared_ptr<audio::AudioFormatAdapter> formatAdapter_;
        std::shared_ptr<asr::IAsrBackend> backend_;
        std::shared_ptr<BackendListener> backendListener_;
        command::TranscriptNormalizer normalizer_;
        mutable std::mutex mutex_;
        std::shared_ptr<IRecognitionPipelineObserver> observer_;
        RecognitionPipelineState state_ = RecognitionPipelineState::stopped;
        bool attached_ = false;
    };

} // namespace timmy::core
