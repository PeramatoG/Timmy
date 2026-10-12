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
#include "timmy/core/recognition_command_executor.h"
#include "timmy/transport/osc_command_emitter.h"
#include "timmy/transport/hydra2_osc_command_profile.h"
#include <charconv>
#include "timmy/command/semantic_command.h"

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
            std::string profile;
            command::TargetType defaultTarget = command::TargetType::channel;
            std::string host;
            std::uint32_t port = 0;
            transport::Hydra2OscOptions hydra;
            bool outputOptions = false;
            bool hydraOptions = false;
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

        // Returns a stable command-line label for semantic parsing state.
        std::string_view semanticName(core::RecognitionSemanticState state)
        {
            switch (state)
            {
            case core::RecognitionSemanticState::notAttempted: return "not-attempted";
            case core::RecognitionSemanticState::produced: return "produced";
            case core::RecognitionSemanticState::unsupportedOrInvalid:
                return "unsupported-or-invalid";
            }
            return "unknown";
        }

        class ConsoleRecognitionObserver final
            : public core::IRecognitionPipelineObserver
        {
        public:
            core::RecognitionCommandExecutor* executor = nullptr;

            void onExecution(const core::RecognitionExecutionResult& result)
            {
                std::lock_guard lock(outputMutex_);
                const auto label = result.state == core::RecognitionExecutionState::queued
                    ? "queued" : result.state == core::RecognitionExecutionState::succeeded
                    ? "succeeded" : "failed";
                std::cout << "DISPATCH: " << label;
                if (!result.diagnostic.empty()) std::cout << " — " << result.diagnostic;
                if (!result.renderedCommand.empty()) std::cout << " — " << result.renderedCommand;
                std::cout << '\n' << std::flush;
            }

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
                std::unique_lock lock(outputMutex_);
                std::cout
                    << "RAW: " << transcript.rawTranscript << '\n'
                    << "NORMALIZED: " << transcript.normalizedTranscript << '\n'
                    << "REPAIRED: " << transcript.repairedTranscript << '\n'
                    << "ASSEMBLY: " << assemblyName(transcript.assemblyState);
                if (!transcript.assemblyText.empty())
                {
                    std::cout << " — " << transcript.assemblyText;
                }
                std::cout << '\n'
                    << "PARSED INTENT: " << semanticName(transcript.semanticState);
                if (transcript.semanticCommand)
                {
                    std::cout << " — "
                        << command::describe(*transcript.semanticCommand);
                }
                std::cout << "\nVALIDATION: " << (!transcript.semanticCommand ? "not attempted" : transcript.validation.accepted ? "accepted" : "rejected");
                if (!transcript.validation.diagnostic.empty()) std::cout << " — " << transcript.validation.diagnostic;
                if (transcript.rendering.text) std::cout << "\nRENDERED COMMAND: " << *transcript.rendering.text;
                else if (!transcript.rendering.diagnostic.empty())
                    std::cout << "\nRENDERING: rejected — " << transcript.rendering.diagnostic;
                std::cout << '\n' << std::flush;
                lock.unlock();
                if (executor)
                {
                    const auto result = executor->submit(transcript);
                    if (result.state == core::RecognitionExecutionState::failed) onExecution(result);
                }
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
                << "  --default-target TYPE  channel (default), fixture or group.\n"
                << "  --profile NAME     Opt in to hydra2 or generic OSC emission.\n"
                << "  --host HOST        Required with --profile.\n"
                << "  --port PORT        Required with --profile (Hydra II: 8000).\n"
                << "  --key-hold-ms MS   Hydra II key hold duration.\n"
                << "  --key-gap-ms MS    Hydra II key gap duration.\n"
                << "  --pre-clear        Hydra II pre-clear.\n"
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
                if (argument == "--pre-clear")
                {
                    options.hydra.preClear = true;
                    options.outputOptions = options.hydraOptions = true;
                    continue;
                }
                if (argument == "--default-target" || argument == "--profile" || argument == "--host"
                    || argument == "--port" || argument == "--key-hold-ms"
                    || argument == "--key-gap-ms")
                {
                    if (++index >= argc) return false;
                    const std::string_view value = argv[index];
                    if (argument == "--default-target")
                    {
                        const auto target = command::defaultTargetFromName(value);
                        if (!target) return false;
                        options.defaultTarget = *target;
                    }
                    else if (argument == "--profile")
                    {
                        if (value != "hydra2" && value != "generic") return false;
                        options.profile = value;
                    }
                    else if (argument == "--host")
                    {
                        options.host = value;
                        options.outputOptions = true;
                    }
                    else
                    {
                        std::uint32_t number = 0;
                        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                        if (value.empty() || parsed.ec != std::errc{}
                            || parsed.ptr != value.data() + value.size()) return false;
                        options.outputOptions = true;
                        if (argument == "--port")
                        {
                            if (number == 0 || number > 65535) return false;
                            options.port = number;
                        }
                        else
                        {
                            options.hydraOptions = true;
                            if (argument == "--key-hold-ms") options.hydra.hold = std::chrono::milliseconds{number};
                            else options.hydra.gap = std::chrono::milliseconds{number};
                        }
                    }
                    continue;
                }
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

            if ((options.profile.empty() && options.outputOptions)
                || (!options.profile.empty() && (options.host.empty() || options.port == 0))
                || (options.hydraOptions && options.profile != "hydra2"))
            {
                std::cerr << "Output options require an explicit profile, host and port; timing options require hydra2.\n";
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

        stopRequested = 0;
        struct InterruptGuard
        {
            using Handler = void (*)(int);
            Handler previous = std::signal(SIGINT, handleInterrupt);
            ~InterruptGuard() { std::signal(SIGINT, previous); }
        } interruptGuard;

        auto backend = core::createAsrBackend(core::AsrEngineId::whisperCpp,
            { options.vadModelPath });
        auto selector = std::make_shared<audio::AudioChannelSelector>(0);
        auto languageProfile = options.profile == "hydra2"
            ? command::hydra2CommandProfile(options.hydra.preClear) : command::CommandProfile{};
        audio::AudioEngine engine;
        languageProfile.parsing.defaultTarget = options.defaultTarget;
        core::RecognitionPipeline pipeline(selector, std::move(backend), languageProfile, false, false, engine.captureStatsSource());
        auto observer = std::make_shared<ConsoleRecognitionObserver>();
        std::unique_ptr<core::RecognitionCommandExecutor> executor;
        if (!options.profile.empty())
        {
            std::shared_ptr<const transport::IOscCommandProfile> profile;
            if (options.profile == "hydra2")
                profile = std::make_shared<transport::Hydra2OscCommandProfile>(options.hydra);
            else profile = std::make_shared<transport::GenericOscCommandProfile>();
            auto emitter = std::make_shared<transport::OscCommandEmitter>(
                transport::OscDestination{options.host, options.port}, profile);
            executor = std::make_unique<core::RecognitionCommandExecutor>(
                core::CommandDispatcher(emitter, languageProfile),
                [observer](const auto& result) { observer->onExecution(result); });
            observer->executor = executor.get();
        }
        pipeline.setObserver(observer);

        asr::AsrConfiguration asrConfiguration;
        asrConfiguration.modelResourcePath = options.modelPath;
        asrConfiguration.preferredLanguage = options.preferredLanguage;
        asrConfiguration.recognitionHints = core::commandRecognitionHints(languageProfile);
        const auto asrStart = pipeline.start(asrConfiguration);
        if (!asrStart.succeeded)
        {
            std::cerr
                << "ASR startup failed ["
                << diagnosticName(asrStart.diagnostic->code)
                << "]: "
                << asrStart.diagnostic->message
                << '\n';
            pipeline.stop();
            if (executor) executor->shutdown();
            observer->executor = nullptr;
            return 1;
        }

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
            if (executor) executor->shutdown();
            observer->executor = nullptr;
            return 1;
        }

        std::cout
            << "Listening on the default capture device. "
            << "Press Ctrl+C to stop.\n";
        while (!stopRequested)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        engine.stop();
        pipeline.stop();
        if (executor) executor->shutdown();
        observer->executor = nullptr;
        std::cout << "ASR listener stopped.\n";
        return 0;
    }

} // namespace timmy::cli
