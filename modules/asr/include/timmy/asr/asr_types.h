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

#include "timmy/asr/configurable_settings.h"
#include "timmy/asr/compute_selection.h"

#include <filesystem>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace timmy::asr {

    enum class AsrState
    {
        stopped,
        starting,
        running,
        stopping,
        failed
    };

    struct AsrCapabilities
    {
        bool supportsPartialResults = false;
        bool supportsExplicitLanguage = false;
        bool supportsGrammarConstraints = false;
        bool supportsVocabularyHints = false;
    };

    enum class RecognitionConstraintIntent
    {
        none,
        commandPhrase
    };

    struct RecognitionHints
    {
        std::vector<std::string> vocabulary;
        std::vector<std::string> expectedPhrases;
        RecognitionConstraintIntent constraintIntent =
            RecognitionConstraintIntent::none;
    };

    struct AsrConfiguration
    {
        std::optional<std::string> preferredLanguage;
        std::filesystem::path modelResourcePath;
        RecognitionHints recognitionHints;
        ComputeSelection compute{};
    };

    enum class AsrResultKind
    {
        partial,
        final
    };

    // Durations describe actual inference input/work; absent for backends without metrics.
    struct RecognitionMetrics
    {
        std::optional<double> audioDurationSeconds{};
        std::optional<double> inferenceDurationSeconds{};
        // Measured for the same utterance as this result, including queue wait.
        std::optional<double> endOfUtteranceToResultSeconds{};
        std::optional<std::chrono::steady_clock::time_point> utteranceFinalizedAt{};
    };

    // Zero denotes ordinary continuous recognition; benchmark generations are nonzero.
    using AttemptGeneration = std::uint64_t;
    enum class RecognitionProgressState
    {
        backendReady, waitingForSpeech, speechStarted, utteranceFinalized,
        inferenceStarted, inferenceCompletedWithText, inferenceCompletedNoText
    };
    // The borrowed view is valid only during the synchronous inference-worker
    // notification, immediately before the backend consumes these exact samples.
    struct InferenceInputView
    {
        AttemptGeneration generation;
        std::uint64_t inferenceIndex; // One-based, reset only after old workers join.
        std::span<const float> samples;
        std::uint32_t sampleRate = 16000;
    };
    struct InferenceInputDiagnostics
    {
        std::uint64_t inferenceIndex = 0;
        std::uint32_t sampleRate = 16000;
        std::uint64_t sampleCount = 0;
        std::string sampleFormat = "ieee_float32";
        double peakLinear = 0, rmsLinear = 0;
        std::optional<double> peakDbfs{}, rmsDbfs{}; // Silence is null, not -infinity JSON.
        std::uint64_t nearFullScaleSamples = 0;
        double nearFullScaleRatio = 0;
        // No acoustic speech/noise windows are currently exported by endpointing.
        std::optional<double> speechRmsDbfs{}, noiseRmsDbfs{}, estimatedSnrDb{};
        std::optional<std::string> wavPath{}; // Relative to the JSONL's parent.
        std::optional<std::string> wavError{};
    };
    struct AttemptDiagnostics
    {
        std::uint64_t audioSamples = 0;
        double audioDurationSeconds = 0;
        double inputPeak = 0;
        double inputRms = 0;
        bool speechDetected = false;
        std::optional<double> speechStartSeconds{};
        std::optional<double> utteranceDurationSeconds{};
        std::optional<double> inferenceDurationSeconds{};
        // Legacy backend-reported block loss, not device-frame capture loss.
        // Absent means the backend cannot report this counter; Whisper leaves it absent.
        std::optional<std::uint64_t> droppedAudioBlocks{};
        std::optional<std::uint64_t> droppedVadFrames{};
        std::optional<std::uint64_t> droppedInferenceJobs{};
        std::optional<std::uint64_t> capturedFrames{};
        std::optional<std::uint64_t> droppedCaptureFrames{};
        std::optional<std::uint64_t> captureRingOccupancyFrames{};
        std::optional<std::uint64_t> captureRingMaxOccupancyFrames{};
        std::optional<std::uint64_t> captureRingCapacityFrames{};
        std::optional<std::uint64_t> captureSessionRingHighWaterFrames{};
        std::optional<std::uint64_t> vadQueueDepth{};
        std::optional<std::uint64_t> vadQueueMaxDepth{};
        std::optional<std::uint64_t> inferenceQueueDepth{};
        std::optional<std::uint64_t> inferenceQueueMaxDepth{};
        std::optional<double> armedDurationSeconds{};
        std::optional<double> capturedDurationSeconds{};
        std::optional<double> utteranceFinalizedSeconds{};
        std::optional<double> inferenceAudioDurationSeconds{};
        std::optional<double> realTimeFactor{};
        std::optional<double> endOfUtteranceToResultSeconds{};
        std::optional<double> oldestVadQueueAgeSeconds{};
        std::optional<double> vadQueuedAudioDurationSeconds{};
        std::optional<double> vadQueueWaitTotalSeconds{};
        std::optional<double> vadQueueWaitMaxSeconds{};
        std::optional<double> inferenceQueueWaitTotalSeconds{};
        std::optional<double> inferenceQueueWaitMaxSeconds{};
        std::optional<double> commandProcessingDurationSeconds{};
        std::optional<double> normalizationDurationSeconds{};
        std::optional<double> assemblyDurationSeconds{};
        std::optional<double> semanticParseDurationSeconds{};
        std::optional<double> validationDurationSeconds{};
        std::optional<double> renderingDurationSeconds{};
        // Capture counts are deltas since arming, before channel selection/resampling.
        // Capture ring maximum is sampled during this attempt, not a session maximum.
        // Queue depths exclude work already dequeued; queue age/wait are wall time.
        // Speech/endpoint offsets are observed worker stages, not acoustic timestamps.
        // Per-job observations and optional Core-owned diagnostic audio storage.
        std::vector<InferenceInputDiagnostics> inferenceInputs{};
        // Backend-owned scalar diagnostics, using the settings serialization contract.
        // Empty means unreported. Passed flags describe actual inference in this attempt.
        SettingValues recognitionGuidance{};
        std::optional<ComputeDiagnostics> compute{};
    };
    struct RecognitionProgress
    {
        RecognitionProgressState state;
        AttemptGeneration generation = 0;
        std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
        std::optional<std::uint64_t> droppedAudioBlocks{};
        std::optional<std::uint64_t> droppedVadFrames{};
        std::optional<std::uint64_t> droppedInferenceJobs{};
    };

    struct AsrResult
    {
        std::string transcript;
        AsrResultKind kind = AsrResultKind::final;
        std::optional<std::string> detectedLanguage;
        RecognitionMetrics metrics{};
        AttemptGeneration generation = 0;
    };

    enum class AsrDiagnosticCode
    {
        invalidConfiguration,
        unavailableResource,
        initializationFailure,
        invalidAudioFormat,
        recognitionFailure
    };

    struct AsrDiagnostic
    {
        AsrDiagnosticCode code = AsrDiagnosticCode::recognitionFailure;
        std::string message;
    };

    struct AsrOperationResult
    {
        bool succeeded = false;
        std::optional<AsrDiagnostic> diagnostic;
    };

} // namespace timmy::asr
