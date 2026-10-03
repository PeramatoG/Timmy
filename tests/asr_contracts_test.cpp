/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/asr_backend.h"
#include "timmy/audio/audio_format_adapter.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

    using timmy::asr::AsrCapabilities;
    using timmy::asr::AsrConfiguration;
    using timmy::asr::AsrDiagnostic;
    using timmy::asr::AsrDiagnosticCode;
    using timmy::asr::AsrOperationResult;
    using timmy::asr::AsrResult;
    using timmy::asr::AsrResultKind;
    using timmy::asr::AsrState;
    using timmy::asr::IAsrBackend;
    using timmy::asr::IAsrResultListener;
    using timmy::audio::AudioBlockView;

    class RecordingListener final
        : public IAsrResultListener
    {
    public:
        // Records a recognition event for deterministic assertions.
        void onAsrResult(const AsrResult& result) override
        {
            results.push_back(result);
        }

        // Records a diagnostic event for deterministic assertions.
        void onAsrDiagnostic(const AsrDiagnostic& diagnostic) override
        {
            diagnostics.push_back(diagnostic);
        }

        std::vector<AsrResult> results;
        std::vector<AsrDiagnostic> diagnostics;
    };

    class FakeAsrBackend final
        : public IAsrBackend
    {
    public:
        // Reports generic features without exposing implementation settings.
        AsrCapabilities capabilities() const override
        {
            return { true, true, false, true };
        }

        // Reports the fake backend lifecycle state.
        AsrState state() const override
        {
            return state_;
        }

        // Starts only when a backend-neutral local resource was configured.
        AsrOperationResult start(
            const AsrConfiguration& configuration) override
        {
            state_ = AsrState::starting;

            if (configuration.modelResourcePath.empty())
            {
                state_ = AsrState::failed;
                return {
                    false,
                    AsrDiagnostic{
                        AsrDiagnosticCode::invalidConfiguration,
                        "A local model resource is required by this fake."
                    }
                };
            }

            state_ = AsrState::running;
            return { true, std::nullopt };
        }

        // Stops the fake backend without external resources.
        AsrOperationResult stop() override
        {
            state_ = AsrState::stopping;
            state_ = AsrState::stopped;
            return { true, std::nullopt };
        }

        std::size_t acceptedFrameCount = 0;

    private:
        // Consumes validated audio and emits alternating partial/final events.
        void processAudioBlock(const AudioBlockView& block) override
        {
            acceptedFrameCount += block.frameCount;
            const bool firstResult = acceptedFrameCount == block.frameCount;
            publishResult({
                firstResult ? "fixture" : "fixture one at full",
                firstResult ? AsrResultKind::partial : AsrResultKind::final,
                std::string("en")
            });
        }

        AsrState state_ = AsrState::stopped;
    };

    // Fails the process with a useful message when a contract is violated.
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    // Verifies backend-neutral lifecycle, configuration, and capabilities.
    void testLifecycleAndCapabilities()
    {
        FakeAsrBackend backend;
        require(backend.state() == AsrState::stopped,
            "backend should initially be stopped");

        const AsrCapabilities capabilities = backend.capabilities();
        require(capabilities.supportsPartialResults,
            "fake should report partial result support");
        require(capabilities.supportsExplicitLanguage,
            "fake should report explicit language support");
        require(!capabilities.supportsGrammarConstraints,
            "grammar support should be independently represented");
        require(capabilities.supportsVocabularyHints,
            "vocabulary support should be independently represented");

        const AsrOperationResult invalidStart = backend.start({});
        require(!invalidStart.succeeded && invalidStart.diagnostic.has_value(),
            "invalid configuration should return a diagnostic");
        require(invalidStart.diagnostic->code
                == AsrDiagnosticCode::invalidConfiguration,
            "invalid configuration should retain its diagnostic category");
        require(backend.state() == AsrState::failed,
            "failed initialization should be observable in state");

        AsrConfiguration configuration;
        configuration.preferredLanguage = "en";
        configuration.modelResourcePath = "test-only-resource";
        require(backend.start(configuration).succeeded,
            "valid generic configuration should start the fake");
        require(backend.state() == AsrState::running,
            "successful start should enter running state");
        require(backend.stop().succeeded
                && backend.state() == AsrState::stopped,
            "stop should return success and enter stopped state");
    }

    // Verifies that the audio adapter feeds valid 16 kHz mono PCM to ASR.
    void testAdapterFlowAndResults()
    {
        auto backend = std::make_shared<FakeAsrBackend>();
        auto listener = std::make_shared<RecordingListener>();
        backend->setResultListener(listener);
        require(backend->start({ std::nullopt, "test-only-resource" }).succeeded,
            "fake should start before consuming audio");

        timmy::audio::AudioFormatAdapter adapter;
        adapter.addConsumer(backend);

        std::vector<float> sourceSamples(480, 0.25F);
        const AudioBlockView sourceBlock{
            sourceSamples,
            sourceSamples.size(),
            { 48000, 1 }
        };
        adapter.onAudioBlock(sourceBlock);
        adapter.onAudioBlock(sourceBlock);

        require(backend->acceptedFrameCount > 0,
            "adapter should deliver resampled frames to ASR");
        require(listener->results.size() == 2,
            "listener should receive both recognition events");
        require(listener->results[0].kind == AsrResultKind::partial
                && listener->results[0].transcript == "fixture",
            "first recognition event should be the expected partial result");
        require(listener->results[1].kind == AsrResultKind::final
                && listener->results[1].transcript == "fixture one at full",
            "second recognition event should be the expected final result");
        require(listener->results[1].detectedLanguage == "en",
            "generic detected language should reach the listener");
    }

    // Verifies that only a running backend receives valid audio blocks.
    void testLifecycleAudioGating()
    {
        FakeAsrBackend backend;
        auto listener = std::make_shared<RecordingListener>();
        backend.setResultListener(listener);
        std::vector<float> samples(160, 0.25F);
        const AudioBlockView validBlock{
            samples,
            samples.size(),
            { 16000, 1 }
        };

        backend.onAudioBlock(validBlock);
        require(backend.acceptedFrameCount == 0,
            "stopped backend must ignore valid audio");

        require(!backend.start({}).succeeded,
            "empty configuration should fail to start the fake");
        backend.onAudioBlock(validBlock);
        require(backend.acceptedFrameCount == 0,
            "failed backend must ignore valid audio");

        require(backend.start({ std::nullopt, "test-only-resource" }).succeeded,
            "valid configuration should start the fake");
        backend.onAudioBlock(validBlock);
        require(backend.acceptedFrameCount == samples.size(),
            "running backend should process valid audio");

        require(backend.stop().succeeded,
            "running fake should stop successfully");
        backend.onAudioBlock(validBlock);
        require(backend.acceptedFrameCount == samples.size(),
            "stopped backend must ignore later valid audio");
        require(listener->diagnostics.empty(),
            "normal lifecycle gating should not publish diagnostics");
    }

    // Verifies deterministic rejection of unsupported ASR input formats.
    void testInvalidAudioIsRejected()
    {
        FakeAsrBackend backend;
        auto listener = std::make_shared<RecordingListener>();
        backend.setResultListener(listener);
        std::vector<float> samples(320, 0.0F);
        require(backend.start({ std::nullopt, "test-only-resource" }).succeeded,
            "fake should be running before format validation");

        backend.onAudioBlock({ samples, 320, { 48000, 1 } });
        backend.onAudioBlock({ samples, 160, { 16000, 2 } });

        require(backend.acceptedFrameCount == 0,
            "invalid audio must not reach the backend implementation");
        require(listener->diagnostics.size() == 2,
            "each invalid format should produce a diagnostic");
        require(listener->diagnostics[0].code
                    == AsrDiagnosticCode::invalidAudioFormat
                && listener->diagnostics[1].code
                    == AsrDiagnosticCode::invalidAudioFormat,
            "format errors should use the structured diagnostic category");
    }

} // namespace

// Runs deterministic contract tests without hardware or external services.
int main()
{
    testLifecycleAndCapabilities();
    testAdapterFlowAndResults();
    testLifecycleAudioGating();
    testInvalidAudioIsRejected();
    return EXIT_SUCCESS;
}
