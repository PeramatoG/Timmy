/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/whisper_asr_backend.h"

#include "whisper_command_decoding.h"
#include "diagnostic_queue.h"
#include "whisper_inference_cancellation.h"
#include "whisper_recognition_guidance.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {

    using timmy::asr::AsrConfiguration;
    using timmy::asr::AsrDiagnosticCode;
    using timmy::asr::AsrState;
    using timmy::asr::WhisperAsrBackend;
    using timmy::asr::WhisperAsrBackendOptions;
    using timmy::asr::detail::makeWhisperCommandDecodingProfile;
    using timmy::asr::RecognitionConstraintIntent;
    using timmy::asr::RecognitionHints;
    using timmy::asr::detail::isValidWhisperRecognitionGuidance;
    using timmy::asr::detail::makeWhisperRecognitionGuidance;
    using timmy::asr::detail::WhisperInferenceCancellation;
    using timmy::asr::detail::WhisperInferenceOutcome;

    // Fails the test process with a focused contract message.
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void testQueueDiagnostics()
    {
        using Queue = timmy::asr::detail::DiagnosticQueue<int>;
        using namespace std::chrono_literals;
        const auto start = Queue::Clock::time_point{};
        Queue vad(256), inference(2);
        for (int i = 0; i < 257; ++i) vad.push(i, start + i * 1ms);
        require(vad.size() == 256 && vad.maximum() == 256 && vad.dropped() == 1,
            "VAD queue must stay bounded and account for its evicted frame");
        require(vad.oldestAge(start + 1s) == 0.999,
            "oldest VAD residence must refer to the oldest surviving frame");
        require(vad.pop(start + 1s).value == 1 && vad.waitTotal() == 0.999,
            "VAD eviction must preserve drop-oldest policy and measure dequeue latency");
        inference.push(1, start, true); inference.push(2, start + 1s, true);
        inference.push(3, start + 2s, true);
        require(inference.size() == 2 && inference.maximum() == 2 && inference.dropped() == 1,
            "inference queue must retain its existing two-job bound");
        const auto job = inference.pop(start + 3s);
        require(job.value == 2 && job.finalized && job.queuedAt == start + 1s,
            "inference metadata must stay associated with the retained utterance");
        require(inference.waitMax() == 2 && inference.waitTotal() == 2,
            "queue wait must include backlog before inference");
        inference.pop(start + 4s);
        require(inference.empty() && inference.maximum() == 2 && !inference.oldestAge()
                && inference.waitTotal() == 4,
            "draining must preserve historical accounting and clear oldest age");
        for (auto* queue : {&vad, &inference})
        {
            queue->reset();
            require(queue->empty() && queue->maximum() == 0 && queue->dropped() == 0
                    && queue->waitTotal() == 0 && queue->waitMax() == 0 && !queue->oldestAge(),
                "attempt reset must clear all queue statistics");
        }
        WhisperAsrBackend backend;
        require(!backend.attemptDiagnostics().droppedAudioBlocks,
            "Whisper cannot invent an upstream capture-loss zero");
    }

    // Verifies the generic capability declaration for the first backend.
    void testCapabilities()
    {
        WhisperAsrBackend backend;
        const auto capabilities = backend.capabilities();
        require(!capabilities.supportsPartialResults,
            "Whisper backend should publish final results only");
        require(capabilities.supportsExplicitLanguage,
            "Whisper backend should support an explicit language");
        require(capabilities.supportsGrammarConstraints,
            "Whisper backend should advertise its grammar guidance");
        require(capabilities.supportsVocabularyHints,
            "Whisper backend should advertise its vocabulary prompt");
    }

    // Verifies the bilingual lighting guidance without loading a model.
    void testRecognitionGuidance()
    {
        const RecognitionHints hints{
            { "channel", "canal", "group", "grupo", "select", "pon" },
            {
                "Canal 4 al 100%",
                "Pon el grupo 5 al 70%",
                "Selecciona los canales del 20 al 30",
                "Canal 1, Canal 2 y Canal 3 al 40%",
                "Select channels 20 through 30"
            },
            RecognitionConstraintIntent::commandPhrase
        };
        const auto guidance = makeWhisperRecognitionGuidance(hints);
        require(isValidWhisperRecognitionGuidance(guidance),
            "default recognition guidance should be valid GBNF");
        require(guidance.startRule == "root"
                && guidance.grammarPenalty == 20.0F,
            "guidance should use the expected root and soft penalty");

        require(guidance.grammar.find("phrase ::= element")
                    != std::string::npos
                && guidance.grammar.find("vocabulary ::= \"channel\" | \"canal\"")
                    != std::string::npos,
            "grammar should constrain vocabulary without prescribing domain syntax");
        require(guidance.initialPrompt
                == "Vocabulary: channel, canal, group, grupo, select, pon.",
            "prompt should use compact vocabulary guidance without example commands");

        const auto promptOnly = makeWhisperRecognitionGuidance({
            { "fixture" },
            { "Fixture 2 at full" },
            RecognitionConstraintIntent::none
        });
        require(isValidWhisperRecognitionGuidance(promptOnly)
                && !promptOnly.initialPrompt.empty()
                && promptOnly.grammar.empty(),
            "unconstrained hints should produce only an initial prompt");

        const auto emptyGuidance = makeWhisperRecognitionGuidance({});
        require(isValidWhisperRecognitionGuidance(emptyGuidance)
                && emptyGuidance.initialPrompt.empty()
                && emptyGuidance.grammar.empty(),
            "empty generic hints should disable Whisper guidance");

        auto invalidGuidance = guidance;
        invalidGuidance.startRule = "missing";
        require(!isValidWhisperRecognitionGuidance(invalidGuidance),
            "an absent start rule should invalidate guidance");
    }

    // Verifies the deterministic command-decoding settings and CPU limits.
    void testCommandDecodingProfile()
    {
        const auto unavailableCpuCount =
            makeWhisperCommandDecodingProfile(0);
        require(unavailableCpuCount.threadCount == 1,
            "unknown hardware concurrency should use one thread");

        const auto limitedCpuCount = makeWhisperCommandDecodingProfile(2);
        require(limitedCpuCount.threadCount == 2,
            "available hardware concurrency should set the thread count");

        const auto abundantCpuCount = makeWhisperCommandDecodingProfile(16);
        require(abundantCpuCount.noContext,
            "command decoding should not reuse previous transcript context");
        require(abundantCpuCount.noTimestamps,
            "command decoding should not generate timestamps");
        require(abundantCpuCount.singleSegment,
            "command decoding should produce one command segment");
        require(abundantCpuCount.threadCount == 4,
            "command decoding should limit CPU threads to four");
        require(abundantCpuCount.maxTokens == 32,
            "command decoding should limit results to 32 tokens");
        require(abundantCpuCount.audioContext == 0,
            "command decoding should initially use the full audio context");
        require(abundantCpuCount.beamSize == 5,
            "command decoding should use the upstream beam size");
        require(abundantCpuCount.temperature == 0.4F
                && abundantCpuCount.temperatureIncrement == 1.0F,
            "command decoding should use the upstream fallback temperatures");
        require(abundantCpuCount.greedyBestOf == 5,
            "command decoding should preserve the upstream fallback width");
    }

    // Verifies stop aborts in-flight work without treating it as a failure.
    void testCooperativeInferenceCancellation()
    {
        WhisperInferenceCancellation cancellation;
        std::promise<void> inferenceEntered;
        auto entered = inferenceEntered.get_future();
        auto worker = std::async(std::launch::async, [&] {
            return cancellation.run([&] {
                inferenceEntered.set_value();
                while (!WhisperInferenceCancellation::abortCallback(
                    &cancellation))
                {
                    std::this_thread::yield();
                }
                // Whisper returns a nonzero status when computation aborts.
                return -1;
            });
        });
        require(entered.wait_for(std::chrono::seconds(5))
                == std::future_status::ready,
            "fake inference should start before requesting cancellation");
        cancellation.requestStop();
        require(worker.wait_for(std::chrono::seconds(5))
                == std::future_status::ready,
            "abort callback should observe a stop request from another thread");
        require(worker.get() == WhisperInferenceOutcome::cancelled,
            "an intentional inference abort should not be a recognition failure");

        bool inferenceCalled = false;
        require(cancellation.run([&] {
            inferenceCalled = true;
            return 0;
        }) == WhisperInferenceOutcome::cancelled && !inferenceCalled,
            "shutdown should discard queued inference before invoking Whisper");

        for (int restart = 0; restart < 3; ++restart)
        {
            cancellation.reset();
            require(!WhisperInferenceCancellation::abortCallback(&cancellation),
                "restart should clear the previous inference stop request");
            require(cancellation.run([] { return 0; })
                    == WhisperInferenceOutcome::completed,
                "restarted inference should accept successful recognition");
            require(cancellation.run([] { return -1; })
                    == WhisperInferenceOutcome::failed,
                "a genuine inference error should remain a failure");
            require(cancellation.run([&] {
                cancellation.requestStop();
                return -1;
            }) == WhisperInferenceOutcome::failed,
                "stop should not hide a genuine error before Whisper acknowledges abort");
            cancellation.reset();
            require(cancellation.run([&] {
                cancellation.requestStop();
                return 0;
            }) == WhisperInferenceOutcome::cancelled,
                "stop concurrent with inference completion should suppress its result");
        }
    }

    // Verifies missing model failures without downloading model weights.
    void testMissingModelDiagnostic()
    {
        WhisperAsrBackend backend;
        AsrConfiguration configuration;
        configuration.modelResourcePath =
            "definitely-not-a-whisper-model.bin";
        const auto result = backend.start(configuration);
        require(!result.succeeded && result.diagnostic.has_value(),
            "a missing model should fail with a diagnostic");
        require(result.diagnostic->code
                == AsrDiagnosticCode::unavailableResource,
            "a missing model should use unavailableResource");
        require(backend.state() == AsrState::failed,
            "a missing model should leave a visible failed state");
        require(backend.stop().succeeded
                && backend.state() == AsrState::stopped,
            "a failed backend should stop cleanly");
        require(backend.stop().succeeded,
            "repeated stop should remain safe after failed startup");
        const auto restart = backend.start(configuration);
        require(!restart.succeeded && restart.diagnostic.has_value()
                && restart.diagnostic->code
                    == AsrDiagnosticCode::unavailableResource,
            "failed startup should remain safely restartable");
        require(backend.stop().succeeded
                && backend.state() == AsrState::stopped,
            "a restarted failed backend should stop cleanly");
    }

    // Verifies invalid local weights report initialization failure safely.
    void testInitializationFailure()
    {
        const auto modelPath =
            std::filesystem::temp_directory_path()
            / "timmy-invalid-whisper-model.bin";
        {
            std::ofstream model(modelPath, std::ios::binary);
            model << "not whisper weights";
        }

        WhisperAsrBackend backend;
        const auto result = backend.start({ std::nullopt, modelPath, {} });
        std::filesystem::remove(modelPath);
        require(!result.succeeded && result.diagnostic.has_value(),
            "invalid weights should fail with a diagnostic");
        require(result.diagnostic->code
                == AsrDiagnosticCode::initializationFailure,
            "invalid weights should use initializationFailure");
        require(backend.state() == AsrState::failed,
            "invalid weights should leave a visible failed state");
        require(backend.stop().succeeded,
            "initialization failure should remain safely stoppable");
    }

    // Verifies an explicitly missing VAD model has a clear startup diagnostic.
    void testMissingVadModelDiagnostic()
    {
        const auto modelPath =
            std::filesystem::temp_directory_path()
            / "timmy-present-whisper-placeholder.bin";
        {
            std::ofstream model(modelPath, std::ios::binary);
            model << "placeholder";
        }

        WhisperAsrBackendOptions options;
        options.vadModelPath = "definitely-not-a-silero-vad-model.bin";
        WhisperAsrBackend backend(options);
        AsrConfiguration configuration;
        configuration.modelResourcePath = modelPath;
        const auto result = backend.start(configuration);
        std::filesystem::remove(modelPath);

        require(!result.succeeded && result.diagnostic.has_value(),
            "a missing explicit VAD model should fail with a diagnostic");
        require(result.diagnostic->code
                == AsrDiagnosticCode::unavailableResource,
            "a missing VAD model should use unavailableResource");
        require(result.diagnostic->message.find("Silero VAD")
                != std::string::npos,
            "the VAD startup diagnostic should identify Silero VAD");
        require(backend.stop().succeeded,
            "a missing VAD model failure should remain safely stoppable");
    }

} // namespace

// Runs model-free Whisper backend lifecycle tests.
int main()
{
    testQueueDiagnostics();
    testCapabilities();
    testCommandDecodingProfile();
    testCooperativeInferenceCancellation();
    testRecognitionGuidance();
    testMissingModelDiagnostic();
    testInitializationFailure();
    testMissingVadModelDiagnostic();
    return EXIT_SUCCESS;
}
