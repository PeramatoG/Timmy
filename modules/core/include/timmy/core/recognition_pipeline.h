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

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace timmy::core {

    enum class RecognitionPipelineState
    {
        stopped,
        listening,
        error
    };

    struct RecognitionTranscript
    {
        std::string rawText;
        std::string normalizedText;
    };

    struct RecognitionPipelineStatus
    {
        RecognitionPipelineState state = RecognitionPipelineState::stopped;
        std::optional<asr::AsrDiagnostic> diagnostic;
    };

    class IRecognitionPipelineObserver
    {
    public:
        // Allows observers to be destroyed through the common contract.
        virtual ~IRecognitionPipelineObserver() = default;

        virtual void onRecognitionStateChanged(
            const RecognitionPipelineStatus& status) = 0;

        virtual void onRecognitionTranscript(
            const RecognitionTranscript& transcript) = 0;
    };

    class RecognitionPipeline final
        : private asr::IAsrResultListener
    {
    public:
        RecognitionPipeline(
            std::shared_ptr<audio::AudioChannelSelector> audioSource,
            std::shared_ptr<asr::IAsrBackend> backend);
        ~RecognitionPipeline() override;

        RecognitionPipeline(const RecognitionPipeline&) = delete;
        RecognitionPipeline& operator=(const RecognitionPipeline&) = delete;

        void setObserver(
            std::shared_ptr<IRecognitionPipelineObserver> observer);

        [[nodiscard]]
        asr::AsrOperationResult start(
            const asr::AsrConfiguration& configuration);

        void stop();

        [[nodiscard]]
        RecognitionPipelineState state() const noexcept;

    private:
        void onAsrResult(const asr::AsrResult& result) override;
        void onAsrDiagnostic(
            const asr::AsrDiagnostic& diagnostic) override;

        void publishState(
            RecognitionPipelineStatus status);

        std::shared_ptr<IRecognitionPipelineObserver> observer() const;

        std::shared_ptr<audio::AudioChannelSelector> audioSource_;
        std::shared_ptr<audio::AudioFormatAdapter> formatAdapter_;
        std::shared_ptr<asr::IAsrBackend> backend_;

        mutable std::mutex observerMutex_;
        std::shared_ptr<IRecognitionPipelineObserver> observer_;

        mutable std::mutex stateMutex_;
        RecognitionPipelineState state_ = RecognitionPipelineState::stopped;
    };

    [[nodiscard]]
    asr::RecognitionHints makeDefaultCommandRecognitionHints();

} // namespace timmy::core
