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

} // namespace

// Runs model-free Whisper backend lifecycle tests.
int main()
{
    testCapabilities();
    testMissingModelDiagnostic();
    testInitializationFailure();
    return EXIT_SUCCESS;
}
