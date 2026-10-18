/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/core/recognition_replay.h"
#include "timmy/audio/float_wav.h"

#include <condition_variable>
#include <stdexcept>

namespace timmy::core {
    namespace {
        class ReplayObserver final : public IRecognitionPipelineObserver
        {
        public:
            void onRecognitionStateChanged(RecognitionPipelineState) override {}
            void onRecognitionDiagnostic(const asr::AsrDiagnostic& value) override
            { std::lock_guard lock(mutex); diagnostic = value; ready.notify_all(); }
            void onRecognitionTranscript(const RecognitionTranscript& value) override
            { std::lock_guard lock(mutex); transcript = value; ready.notify_all(); }
            std::mutex mutex;
            std::condition_variable ready;
            std::optional<asr::AsrDiagnostic> diagnostic;
            std::optional<RecognitionTranscript> transcript;
        };
    }
    RecognitionReplayResult replayInferenceWav(std::shared_ptr<asr::IAsrBackend> backend,
        const std::filesystem::path& path, const asr::AsrConfiguration& configuration,
        command::CommandProfile profile, std::chrono::milliseconds timeout)
    {
        audio::FloatWav wav;
        try {
            wav = audio::readFloatWav(path);
            if (wav.sampleRate != asr::IAsrBackend::requiredSampleRate)
                throw std::invalid_argument("Exact replay requires a mono 16 kHz float32 WAV; resampling would change the input.");
            if (!backend || timeout <= std::chrono::milliseconds::zero())
                throw std::invalid_argument("Replay requires a backend and a positive timeout.");
        } catch (const std::exception& error) {
            return {{false, asr::AsrDiagnostic{asr::AsrDiagnosticCode::invalidAudioFormat, error.what()}}, {}};
        }
        auto observer = std::make_shared<ReplayObserver>();
        RecognitionPipeline pipeline(std::make_shared<audio::AudioChannelSelector>(0),
            std::move(backend), std::move(profile), true, true);
        pipeline.setObserver(observer);
        auto operation = pipeline.start(configuration);
        if (!operation.succeeded) return {operation, {}};
        operation = pipeline.prepareAttempt(1);
        if (!operation.succeeded) return {operation, {}};
        pipeline.armAttempt();
        operation = pipeline.submitInferenceInput(wav.samples);
        if (!operation.succeeded) return {operation, {}};
        RecognitionReplayResult result;
        {
            std::unique_lock lock(observer->mutex);
            const bool completed = observer->ready.wait_for(lock, timeout, [&] {
                return observer->transcript || observer->diagnostic;
            });
            if (!completed)
                result.operation = {false, asr::AsrDiagnostic{asr::AsrDiagnosticCode::recognitionFailure, "Replay timed out."}};
            else if (observer->diagnostic) result.operation = {false, observer->diagnostic};
            else { result.operation = {true, std::nullopt}; result.transcript = observer->transcript; }
        }
        pipeline.stop(); // Joins workers without holding observer/session locks.
        return result;
    }
}
