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

#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"
#include "timmy/core/recognition_pipeline.h"

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

        // Returns a stable command-line label for an assembly state.
        std::string_view assemblyName(core::RecognitionAssemblyState state)
        {
            switch (state)
            {
            case core::RecognitionAssemblyState::pending: return "pending";
            case core::RecognitionAssemblyState::complete: return "complete";
            case core::RecognitionAssemblyState::rejected: return "rejected";
            }
            return "unknown";
        }

        class ConsoleRecognitionObserver final
            : public core::IRecognitionPipelineObserver
        {
        public:
            // Ignores lifecycle events already described by the command loop.
            void onRecognitionStateChanged(core::RecognitionPipelineState) override
            {
            }

            // Prints structured runtime diagnostics from the shared pipeline.
            void onRecognitionDiagnostic(
                const asr::AsrDiagnostic& diagnostic) override
            {
                std::lock_guard lock(outputMutex_);
                std::cerr
                    << "ASR runtime error ["
                    << diagnosticName(diagnostic.code)
                    << "]: "
                    << diagnostic.message
                    << '\n';
            }

            // Prints every observable stage of a final recognition result.
            void onRecognitionTranscript(
                const core::RecognitionTranscript& transcript) override
            {
                std::lock_guard lock(outputMutex_);
                std::cout
                    << "RAW: " << transcript.rawTranscript << '\n'
                    << "NORMALIZED: " << transcript.normalizedTranscript << '\n'
                    << "ASSEMBLY: " << assemblyName(transcript.assemblyState);
                if (!transcript.assemblyText.empty())
                {
                    std::cout << " — " << transcript.assemblyText;
                }
                std::cout << '\n' << std::flush;
            }

        private:
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

        auto backend = core::createAsrBackend(core::AsrEngineId::whisperCpp,
            { options.vadModelPath });
        auto selector = std::make_shared<audio::AudioChannelSelector>(0);
        core::RecognitionPipeline pipeline(selector, std::move(backend));
        auto observer = std::make_shared<ConsoleRecognitionObserver>();
        pipeline.setObserver(observer);

        asr::AsrConfiguration asrConfiguration;
        asrConfiguration.modelResourcePath = options.modelPath;
        asrConfiguration.preferredLanguage = options.preferredLanguage;
        asrConfiguration.recognitionHints = core::commandRecognitionHints();
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
