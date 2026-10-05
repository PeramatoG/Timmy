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

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

    class FakeBackend final
        : public timmy::asr::IAsrBackend
    {
    public:
        // Reports the minimal capabilities of the deterministic fake.
        timmy::asr::AsrCapabilities capabilities() const override
        {
            return {};
        }

        // Reports the fake backend lifecycle state.
        timmy::asr::AsrState state() const override
        {
            return state_;
        }

        // Starts when the test supplies a model resource.
        timmy::asr::AsrOperationResult start(
            const timmy::asr::AsrConfiguration& configuration) override
        {
            if (configuration.modelResourcePath.empty())
            {
                state_ = timmy::asr::AsrState::failed;
                return {
                    false,
                    timmy::asr::AsrDiagnostic{
                        timmy::asr::AsrDiagnosticCode::invalidConfiguration,
                        "A model is required."
                    }
                };
            }

            state_ = timmy::asr::AsrState::running;
            return { true, std::nullopt };
        }

        // Stops without owning external resources.
        timmy::asr::AsrOperationResult stop() override
        {
            state_ = timmy::asr::AsrState::stopped;
            return { true, std::nullopt };
        }

        // Publishes a deterministic final transcript to the pipeline.
        void emitFinal(std::string transcript)
        {
            publishResult({
                std::move(transcript),
                timmy::asr::AsrResultKind::final,
                std::nullopt
            });
        }

        // Publishes a deterministic runtime failure to the pipeline.
        void emitError(std::string message)
        {
            publishDiagnostic({
                timmy::asr::AsrDiagnosticCode::recognitionFailure,
                std::move(message)
            });
        }

    private:
        // Ignores audio because event publication is controlled by each test.
        void processAudioBlock(
            const timmy::audio::AudioBlockView&) override
        {
        }

        timmy::asr::AsrState state_ =
            timmy::asr::AsrState::stopped;
    };

    class RecordingObserver final
        : public timmy::core::IRecognitionPipelineObserver
    {
    public:
        // Records lifecycle updates for deterministic assertions.
        void onRecognitionStateChanged(
            const timmy::core::RecognitionPipelineStatus& status) override
        {
            statuses.push_back(status);
        }

        // Records the raw and normalized transcript pair.
        void onRecognitionTranscript(
            const timmy::core::RecognitionTranscript& transcript) override
        {
            transcripts.push_back(transcript);
        }

        std::vector<timmy::core::RecognitionPipelineStatus> statuses;
        std::vector<timmy::core::RecognitionTranscript> transcripts;
    };

    // Terminates the test with a useful message when a contract is violated.
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

} // namespace

// Verifies shared ASR lifecycle, normalization, and diagnostic observation.
int main()
{
    auto source =
        std::make_shared<timmy::audio::AudioChannelSelector>(0);
    auto backend = std::make_shared<FakeBackend>();
    auto observer = std::make_shared<RecordingObserver>();
    timmy::core::RecognitionPipeline pipeline(source, backend);
    pipeline.setObserver(observer);

    timmy::asr::AsrConfiguration configuration;
    configuration.modelResourcePath = "test-model";
    require(pipeline.start(configuration).succeeded,
        "pipeline should start a valid backend");
    require(pipeline.state()
            == timmy::core::RecognitionPipelineState::listening,
        "successful startup should publish listening state");

    backend->emitFinal("  Canal 4   al 50 %. ");
    require(observer->transcripts.size() == 1,
        "one final result should publish one transcript");
    require(observer->transcripts.front().rawText
            == "  Canal 4   al 50 %. ",
        "pipeline should preserve the raw ASR transcript");
    require(observer->transcripts.front().normalizedText
            == "Canal 4 al 50%",
        "pipeline should normalize final transcripts outside the frontend");

    backend->emitError("Recognition failed.");
    require(pipeline.state()
            == timmy::core::RecognitionPipelineState::error,
        "runtime diagnostics should publish error state");
    require(observer->statuses.back().diagnostic.has_value(),
        "error state should retain its structured diagnostic");
    require(observer->statuses.back().diagnostic->code
            == timmy::asr::AsrDiagnosticCode::recognitionFailure,
        "error state should retain its diagnostic code");
    require(observer->statuses.back().diagnostic->message
            == "Recognition failed.",
        "error state should retain its diagnostic detail");

    pipeline.stop();
    require(pipeline.state()
            == timmy::core::RecognitionPipelineState::stopped,
        "stop should publish stopped state");

    const auto invalidStart = pipeline.start({});
    require(!invalidStart.succeeded,
        "invalid backend configuration should fail through the pipeline");
    require(observer->statuses.back().diagnostic.has_value()
            && observer->statuses.back().diagnostic->code
                == timmy::asr::AsrDiagnosticCode::invalidConfiguration
            && observer->statuses.back().diagnostic->message
                == "A model is required.",
        "startup diagnostics should preserve their code and message");
    return EXIT_SUCCESS;
}
