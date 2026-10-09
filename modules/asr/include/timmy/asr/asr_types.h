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

#include <filesystem>
#include <chrono>
#include <cstdint>
#include <optional>
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
    };

    // Zero denotes ordinary continuous recognition; benchmark generations are nonzero.
    using AttemptGeneration = std::uint64_t;
    enum class RecognitionProgressState
    {
        backendReady, waitingForSpeech, speechStarted, utteranceFinalized,
        inferenceStarted, inferenceCompletedWithText, inferenceCompletedNoText
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
        // Absent means the backend cannot report this counter.
        std::optional<std::uint64_t> droppedAudioBlocks{};
        std::optional<std::uint64_t> droppedVadFrames{};
        std::optional<std::uint64_t> droppedInferenceJobs{};
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
