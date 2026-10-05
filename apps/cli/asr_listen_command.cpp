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

#include "timmy/asr/whisper_asr_backend.h"
#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/audio/audio_format_adapter.h"

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

        // Creates the initial backend-neutral lighting recognition hints.
        asr::RecognitionHints makeLightingRecognitionHints()
        {
            return {
                {
                    "channel", "canal", "fixture", "group", "grupo",
                    "at", "al", "full", "select", "selecciona", "set",
                    "pon", "up", "sube", "down", "baja"
                },
                {
                    "Canal 4 al 100%",
                    "Grupo 5 al 70%",
                    "Pon el grupo 5 al 70%",
                    "Selecciona el canal 25",
                    "Selecciona los canales del 20 al 30",
                    "Canal 1, Canal 2 y Canal 3 al 40%",
                    "Sube el canal 5 al 50%",
                    "Baja el grupo 2 al 20%",
                    "Channel 4 at 50%",
                    "Set group 5 at 70%",
                    "Select channels 20 through 30"
                },
                asr::RecognitionConstraintIntent::commandPhrase
            };
        }

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

        class ConsoleAsrListener final
            : public asr::IAsrResultListener
        {
        public:
            // Prints final recognition events without interleaving worker output.
            void onAsrResult(const asr::AsrResult& result) override
            {
                if (result.kind != asr::AsrResultKind::final)
                {
                    return;
                }

                std::lock_guard lock(outputMutex_);
                std::cout << result.transcript << '\n' << std::flush;
            }

            // Prints structured runtime diagnostics from the backend worker.
            void onAsrDiagnostic(
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

        asr::WhisperAsrBackendOptions backendOptions;
        backendOptions.vadModelPath = options.vadModelPath;
        auto backend =
            std::make_shared<asr::WhisperAsrBackend>(backendOptions);
        auto listener = std::make_shared<ConsoleAsrListener>();
        backend->setResultListener(listener);

        asr::AsrConfiguration asrConfiguration;
        asrConfiguration.modelResourcePath = options.modelPath;
        asrConfiguration.preferredLanguage = options.preferredLanguage;
        asrConfiguration.recognitionHints = makeLightingRecognitionHints();
        const auto asrStart = backend->start(asrConfiguration);
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

        auto adapter = std::make_shared<audio::AudioFormatAdapter>();
        auto selector = std::make_shared<audio::AudioChannelSelector>(0);
        adapter->addConsumer(backend);
        selector->addConsumer(adapter);

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
            backend->stop();
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
        backend->stop();
        std::cout << "ASR listener stopped.\n";
        return 0;
    }

} // namespace timmy::cli
