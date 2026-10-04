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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

    using timmy::asr::AsrConfiguration;
    using timmy::asr::AsrDiagnosticCode;
    using timmy::asr::AsrState;
    using timmy::asr::WhisperAsrBackend;
    using timmy::asr::WhisperAsrBackendOptions;
    using timmy::asr::detail::makeWhisperCommandDecodingProfile;

    // Fails the test process with a focused contract message.
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
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
        require(!capabilities.supportsGrammarConstraints,
            "Whisper backend should not advertise grammar constraints");
        require(!capabilities.supportsVocabularyHints,
            "Whisper backend should not advertise vocabulary hints");
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
        const auto result = backend.start({ std::nullopt, modelPath });
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
    testCapabilities();
    testCommandDecodingProfile();
    testMissingModelDiagnostic();
    testInitializationFailure();
    testMissingVadModelDiagnostic();
    return EXIT_SUCCESS;
}
