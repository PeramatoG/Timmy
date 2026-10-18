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
#include "timmy/core/inference_audio.h"
#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_capture_stats.h"
#include "timmy/audio/audio_format_adapter.h"
#include "timmy/command/command_processor.h"
#include "timmy/command/transcript_normalizer.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace timmy::command {
    class CommandAssembler;
}

namespace timmy::core {

    enum class RecognitionPipelineState
    {
        stopped,
        starting,
        listening,
        stopping,
        failed
    };

    enum class RecognitionAssemblyState
    {
        pending,
        complete,
        rejected
    };

    enum class RecognitionSemanticState
    {
        notAttempted,
        produced,
        unsupportedOrInvalid
    };

    struct RecognitionTranscript
    {
        std::string rawTranscript;
        std::string normalizedTranscript;
        RecognitionAssemblyState assemblyState{RecognitionAssemblyState::rejected};
        std::string assemblyText;
        RecognitionSemanticState semanticState{RecognitionSemanticState::notAttempted};
        std::optional<command::SemanticCommand> semanticCommand;
        command::IntentValidationResult validation;
        command::IntentRenderingResult rendering;
        std::optional<std::string> detectedLanguage{};
        asr::RecognitionMetrics metrics{};
        asr::AttemptGeneration generation = 0;
        asr::AttemptDiagnostics diagnostics{};
        std::string repairedTranscript{};
        bool repairApplied{false};
        std::vector<command::CommandRepair> repairs{};
        std::vector<std::string> numericInferenceReasons{};
    };

    class IRecognitionPipelineObserver
    {
    public:
        virtual ~IRecognitionPipelineObserver() = default;
        virtual void onRecognitionStateChanged(RecognitionPipelineState state) = 0;
        virtual void onRecognitionDiagnostic(const asr::AsrDiagnostic& diagnostic) = 0;
        virtual void onRecognitionProgress(const asr::RecognitionProgress&) {}
        virtual void onRecognitionTranscript(const RecognitionTranscript& transcript) = 0;
    };

    class RecognitionPipeline final
    {
    public:
        // Standalone attempts can opt out of inter-utterance assembly/context;
        // ordinary recognition retains both by default.
        RecognitionPipeline(
            std::shared_ptr<audio::AudioChannelSelector> channelSelector,
            std::shared_ptr<asr::IAsrBackend> backend, command::CommandProfile profile = {},
            bool isolateUtterances = false, bool benchmarkAttempts = false,
            std::shared_ptr<const audio::IAudioCaptureStatsSource> captureStats = {});
        ~RecognitionPipeline();

        RecognitionPipeline(const RecognitionPipeline&) = delete;
        RecognitionPipeline& operator=(const RecognitionPipeline&) = delete;

        void setObserver(std::shared_ptr<IRecognitionPipelineObserver> observer);
        [[nodiscard]] RecognitionPipelineState state() const;
        asr::AsrOperationResult start(const asr::AsrConfiguration& configuration);
        asr::AsrOperationResult stop();
        asr::AsrOperationResult prepareAttempt(asr::AttemptGeneration generation,
            std::optional<InferenceAudioCapture> capture = std::nullopt);
        // Exact, already segmented backend input: no device, resampler or VAD pass.
        asr::AsrOperationResult submitInferenceInput(std::span<const float> samples);
        void armAttempt();
        void disarmAttempt();
        [[nodiscard]] asr::AttemptDiagnostics attemptDiagnostics() const;
        // Resets both pending assembly and the minimal last-target linguistic context.
        void discardPendingAssembly();

    private:
        class BackendListener;
        class AudioInput;
        friend class BackendListener;

        bool handleInferenceInput(const asr::InferenceInputView& input);
        void handleResult(const asr::AsrResult& result);
        void handleProgress(const asr::RecognitionProgress& progress);
        void handleDiagnostic(const asr::AsrDiagnostic& diagnostic);
        void publishState(RecognitionPipelineState state);
        std::shared_ptr<IRecognitionPipelineObserver> observer() const;

        std::shared_ptr<audio::AudioChannelSelector> channelSelector_;
        std::shared_ptr<audio::AudioFormatAdapter> formatAdapter_;
        std::shared_ptr<AudioInput> audioInput_;
        std::shared_ptr<asr::IAsrBackend> backend_;
        std::shared_ptr<BackendListener> backendListener_;
        command::CommandProfile profile_;
        command::TranscriptNormalizer normalizer_;
        command::CommandLexicalRepair lexicalRepair_;
        std::unique_ptr<command::CommandAssembler> assembler_;
        command::SemanticCommandParser semanticParser_;
        command::CommandContext commandContext_;
        mutable std::mutex mutex_;
        std::shared_ptr<IRecognitionPipelineObserver> observer_;
        RecognitionPipelineState state_ = RecognitionPipelineState::stopped;
        bool attached_ = false;
        const bool isolateUtterances_;
        const bool benchmarkAttempts_;
        asr::AttemptGeneration attemptGeneration_ = 0;
        bool attemptArmed_ = false;
        mutable std::mutex inferenceAudioMutex_;
        std::optional<InferenceAudioCapture> inferenceCapture_;
        std::vector<asr::InferenceInputDiagnostics> inferenceInputs_;
    };

} // namespace timmy::core
