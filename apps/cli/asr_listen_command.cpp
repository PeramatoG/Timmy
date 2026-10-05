/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "asr_listen_command.h"

#include "timmy/asr/asr_backend_factory.h"
#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/core/recognition_pipeline.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace timmy::cli {

    namespace {

        volatile std::sig_atomic_t stopRequested = 0;

        struct AsrListenOptions
        {
            std::filesystem::path modelPath;
            std::filesystem::path vadModelPath;
            std::optional<std::string> preferredLanguage;
        };

        // Returns a stable command-line label for an ASR diagnostic code.
        std::string_view diagnosticName(asr::AsrDiagnosticCode code)
        {
            switch (code)
            {
            case asr::AsrDiagnosticCode::invalidConfiguration:
                return "invalid-configuration";
            case asr::AsrDiagnosticCode::unavailableResource:
                return "unavailable-resource";
            case asr::AsrDiagnosticCode::initializationFailure:
                return "initialization-failure";
            case asr::AsrDiagnosticCode::invalidAudioFormat:
                return "invalid-audio-format";
            case asr::AsrDiagnosticCode::recognitionFailure:
                return "recognition-failure";
            }
            return "unknown";
        }

        class ConsoleRecognitionObserver final
            : public core::IRecognitionPipelineObserver
        {
        public:
            // Prints lifecycle errors without interleaving worker output.
            void onRecognitionStateChanged(
                const core::RecognitionPipelineStatus& status) override
            {
                if (status.state
                    == core::RecognitionPipelineState::listening)
                {
                    hasStarted_.store(true);
                    return;
                }

                if (status.state != core::RecognitionPipelineState::error
                    || !status.diagnostic.has_value())
                {
                    return;
                }

                if (!hasStarted_.load())
                {
                    return;
                }

                std::lock_guard lock(outputMutex_);
                std::cerr
                    << "ASR runtime error ["
                    << diagnosticName(status.diagnostic->code)
                    << "]: "
                    << status.diagnostic->message
                    << '\n';
            }

            // Prints final raw recognition text as in the existing CLI contract.
            void onRecognitionTranscript(
                const core::RecognitionTranscript& transcript) override
            {
                std::lock_guard lock(outputMutex_);
                std::cout << transcript.rawText << '\n' << std::flush;
            }

        private:
            std::atomic_bool hasStarted_{ false };
            std::mutex outputMutex_;
        };

        // Requests an orderly shutdown from the command's polling loop.
        void handleInterrupt(int)
        {
            stopRequested = 1;
        }

        // Prints options for the manual Whisper capture path.
        void printListenUsage()
        {
            std::cout
                << "Usage:\n"
                << "  timmy-cli asr listen --model <path> [options]\n\n"
                << "Options:\n"
                << "  --model PATH       Local whisper.cpp model file.\n"
                << "  --vad-model PATH   Local ggml-silero-v6.2.0.bin file.\n"
                << "  --language CODE    Preferred language code.\n"
                << "  -h, --help         Show this help.\n";
        }

        // Parses required model and optional language arguments.
        bool parseOptions(
            int argc,
            char* argv[],
            AsrListenOptions& options)
        {
            for (int index = 0; index < argc; ++index)
            {
                const std::string_view argument = argv[index];
                if (argument == "--model"
                    || argument == "--vad-model"
                    || argument == "--language")
                {
                    if (index + 1 >= argc)
                    {
                        std::cerr << "Missing value for " << argument << ".\n";
                        return false;
                    }

                    const std::string value = argv[++index];
                    if (argument == "--model")
                    {
                        options.modelPath = value;
                    }
                    else if (argument == "--vad-model")
                    {
                        options.vadModelPath = value;
                    }
                    else
                    {
                        options.preferredLanguage = value;
                    }
                    continue;
                }

                std::cerr << "Unknown ASR listen option: " << argument << '\n';
                return false;
            }

            if (options.modelPath.empty())
            {
                std::cerr << "The --model option is required.\n";
                return false;
            }
            return true;
        }

    } // namespace

    // Captures the default microphone and prints final Whisper transcripts.
    int runAsrListen(int argc, char* argv[])
    {
        if (argc == 1)
        {
            const std::string_view argument = argv[0];
            if (argument == "--help" || argument == "-h")
            {
                printListenUsage();
                return 0;
            }
        }

        AsrListenOptions options;
        if (!parseOptions(argc, argv, options))
        {
            std::cerr << '\n';
            printListenUsage();
            return 2;
        }

        asr::AsrBackendCreateOptions backendOptions;
        backendOptions.engine = asr::AsrEngine::whisperCpp;
        backendOptions.vadModelResourcePath = options.vadModelPath;
        auto backend = asr::createAsrBackend(backendOptions);
        auto selector = std::make_shared<audio::AudioChannelSelector>(0);
        core::RecognitionPipeline pipeline(selector, backend);
        auto observer =
            std::make_shared<ConsoleRecognitionObserver>();
        pipeline.setObserver(observer);

        asr::AsrConfiguration asrConfiguration;
        asrConfiguration.modelResourcePath = options.modelPath;
        asrConfiguration.preferredLanguage = options.preferredLanguage;
        asrConfiguration.recognitionHints =
            core::makeDefaultCommandRecognitionHints();
        const auto asrStart = pipeline.start(asrConfiguration);
        if (!asrStart.succeeded)
        {
            std::cerr
                << "ASR startup failed ["
                << diagnosticName(asrStart.diagnostic->code)
                << "]: "
                << asrStart.diagnostic->message
                << '\n';
            return 1;
        }

        audio::AudioEngine engine;
        engine.addConsumer(selector);
        const auto audioStart = engine.start({});
        if (!audioStart.ok())
        {
            std::cerr
                << "Audio startup failed ["
                << static_cast<int>(audioStart.diagnostic->code)
                << "]: "
                << audioStart.diagnostic->message
                << '\n';
            pipeline.stop();
            return 1;
        }

        stopRequested = 0;
        std::signal(SIGINT, handleInterrupt);
        std::cout
            << "Listening on the default capture device. "
            << "Press Ctrl+C to stop.\n";
        while (!stopRequested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        engine.stop();
        pipeline.stop();
        std::cout << "ASR listener stopped.\n";
        return 0;
    }

} // namespace timmy::cli
