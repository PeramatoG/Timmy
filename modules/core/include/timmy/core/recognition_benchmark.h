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

#include "timmy/core/recognition_pipeline.h"
#include "timmy/asr/configurable_settings.h"

#include <fstream>
#include <mutex>
#include <vector>

namespace timmy::core {

    inline constexpr const char* liveBenchmarkCorpusVersion = "timmy-live-v1";

    struct RecognitionBenchmarkCase
    {
        std::string id;
        std::string category;
        std::string phrase;
        // nullopt explicitly means no accepted/renderable command is expected.
        std::optional<command::SemanticCommand> expected;
    };

    [[nodiscard]] const std::vector<RecognitionBenchmarkCase>& liveBenchmarkCases();
    enum class BenchmarkRecognitionOutcome { finalResult, noSpeech, emptyAsrResult, operatorNoResult };
    [[nodiscard]] const char* benchmarkOutcomeName(BenchmarkRecognitionOutcome outcome);
    enum class BenchmarkAttemptState
    { preparing, ready, countdown, waitingSpeech, speechDetected, waitingEnd, processing, result, closed };

    [[nodiscard]] bool benchmarkPasses(const RecognitionBenchmarkCase& test,
        const RecognitionTranscript& actual,
        BenchmarkRecognitionOutcome outcome = BenchmarkRecognitionOutcome::finalResult);

    struct BenchmarkAudioSource
    {
        std::string deviceDisplayName;
        std::size_t channelIndex{0}; // Zero-based index in the captured device.
        std::optional<std::string> channelDisplayName{};
    };

    struct RecognitionBenchmarkSnapshot
    {
        std::string engineId;
        asr::AsrConfiguration configuration;
        std::filesystem::path vadModelPath;
        asr::SettingValues settings;
        command::CommandProfile commandProfile;
        BenchmarkAudioSource audioSource{};
        bool saveAsrInputWavs = false;
    };

    struct RecognitionBenchmarkAttempt
    {
        std::string caseId;
        unsigned attempt;
        bool passed;
        BenchmarkRecognitionOutcome outcome{BenchmarkRecognitionOutcome::finalResult};
        asr::AttemptGeneration generation = 0;
        asr::AttemptDiagnostics diagnostics{};
    };

    // Has no emitter or output path. record() runs on a recognition worker, never
    // the realtime callback. One result is accepted until explicit Next/Retry.
    class RecognitionBenchmarkSession final
    {
    public:
        RecognitionBenchmarkSession(std::filesystem::path resultPath,
            RecognitionBenchmarkSnapshot snapshot);
        ~RecognitionBenchmarkSession();
        RecognitionBenchmarkSession(const RecognitionBenchmarkSession&) = delete;
        RecognitionBenchmarkSession& operator=(const RecognitionBenchmarkSession&) = delete;

        using Clock = std::chrono::steady_clock;
        // Frontend runs preparation away from its UI thread; no callbacks hold
        // the session mutex while resetting/joining backend workers.
        asr::AsrOperationResult prepare(RecognitionPipeline& pipeline);
        bool startAttempt(Clock::time_point now = Clock::now());
        [[nodiscard]] std::optional<RecognitionBenchmarkAttempt> tick(
            RecognitionPipeline& pipeline, Clock::time_point now = Clock::now());
        void progress(const asr::RecognitionProgress& progress);
        [[nodiscard]] BenchmarkAttemptState attemptState() const;
        [[nodiscard]] asr::AttemptGeneration generation() const;
        [[nodiscard]] std::optional<RecognitionBenchmarkAttempt> record(
            const RecognitionTranscript& transcript);
        // Explicit operator outcome, not a fabricated ASR transcript. The same
        // result gate arbitrates this action against a concurrent final result.
        [[nodiscard]] std::optional<RecognitionBenchmarkAttempt> recordNoResult(asr::AttemptDiagnostics diagnostics = {});
        // Close inference admission before snapshotting files/counters.
        [[nodiscard]] std::optional<RecognitionBenchmarkAttempt> recordNoResult(RecognitionPipeline& pipeline);
        // Navigation closes result acceptance until explicit prepare + Start attempt.
        [[nodiscard]] bool next();
        void retry();
        void close();
        [[nodiscard]] bool awaitingResult() const;
        [[nodiscard]] std::size_t index() const;
        [[nodiscard]] const RecognitionBenchmarkSnapshot& snapshot() const { return snapshot_; }
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }
        [[nodiscard]] const std::string& id() const { return sessionId_; }

    private:
        std::optional<RecognitionBenchmarkAttempt> recordAttempt(
            const RecognitionTranscript& transcript, BenchmarkRecognitionOutcome outcome);
        const std::filesystem::path path_;
        const RecognitionBenchmarkSnapshot snapshot_;
        const std::string sessionId_;
        mutable std::mutex mutex_;
        std::ofstream output_;
        std::size_t index_{0};
        unsigned attempt_{1};
        bool awaiting_{true};
        BenchmarkAttemptState state_{BenchmarkAttemptState::preparing};
        asr::AttemptGeneration generation_{0};
        Clock::time_point countdownAt_{}, speakAt_{};
        std::optional<Clock::time_point> speechAt_, inferenceAt_;
        asr::AttemptDiagnostics diagnostics_{};
        bool closed_{false};
    };

    // One JSON object without its terminating newline; useful to other frontends.
    [[nodiscard]] std::string benchmarkJson(const RecognitionBenchmarkSnapshot& snapshot,
        const std::string& sessionId, const RecognitionBenchmarkCase& test,
        unsigned attempt, const RecognitionTranscript& transcript,
        BenchmarkRecognitionOutcome outcome = BenchmarkRecognitionOutcome::finalResult);
} // namespace timmy::core
