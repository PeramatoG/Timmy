/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "asr_replay_command.h"
#include "asr_options.h"
#include "timmy/core/recognition_replay.h"
#include "timmy/core/command_recognition_profile.h"

#include <iostream>

namespace timmy::cli {
    namespace {
        void printReplayUsage()
        {
            std::cout << "Usage: timmy-cli asr replay --wav PATH --model PATH [options]\n"
                << "  --language CODE        Preferred language.\n"
                << "  --compute ID           automatic (default), cpu or vulkan.\n"
                << "  --compute-device N     GGML GPU index.\n"
                << "  --grammar on|off       Vocabulary grammar (default: on).\n"
                << "  --prompt on|off        Compact prompt (default: on).\n"
                << "  --setting ID=VALUE     Shared backend decoding/VAD tuning.\n"
                << "  --command-profile NAME native (default) or hydra2; never emits commands.\n"
                << "  --default-target TYPE  channel (default), fixture or group.\n"
                << "  --vad-model PATH       Optional VAD resource; exact replay bypasses segmentation.\n"
                << "  --timeout-seconds N    Inference wait limit (default: 120).\n"
                << "  --help                 Show this help.\n"
                << "Input must be mono IEEE float32 WAV at 16 kHz. No audio device is opened.\n"
                << "Exact replay preserves endpoint padding and bypasses VAD/fixed-window slicing.\n"
                << "Output includes transcript stages, settings, input levels and inference timing.\n";
        }
    }
    int runAsrReplay(int argc, char* argv[])
    {
        AsrInputOptions options;
        std::filesystem::path wavPath;
        int timeout = 120;
        for (int index = 0; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--help" || argument == "-h") { printReplayUsage(); return 0; }
            if (argument == "--wav" || argument == "--timeout-seconds") {
                if (++index >= argc) { printReplayUsage(); return 2; }
                const std::string_view value = argv[index];
                if (argument == "--wav") wavPath = value;
                else {
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), timeout);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || timeout <= 0) return 2;
                }
                continue;
            }
            if (parseAsrInputOption(argument, index, argc, argv, options) != OptionParse::accepted) {
                std::cerr << "Invalid replay option: " << argument << '\n'; return 2;
            }
        }
        if (wavPath.empty() || options.modelPath.empty()) { printReplayUsage(); return 2; }
        try {
            auto profile = options.commandProfileName == "hydra2"
                ? command::hydra2CommandProfile() : command::CommandProfile{};
            profile.parsing.defaultTarget = options.defaultTarget;
            asr::AsrConfiguration configuration;
            configuration.modelResourcePath = options.modelPath;
            configuration.preferredLanguage = options.preferredLanguage;
            configuration.compute = options.compute;
            configuration.recognitionHints = core::commandRecognitionHints(profile);
            const auto replay = core::replayInferenceWav(core::createAsrBackend(core::AsrEngineId::whisperCpp,
                {options.vadModelPath}, options.settings), wavPath, configuration, profile,
                std::chrono::seconds(timeout));
            if (!replay.operation.succeeded) {
                std::cerr << "Replay failed: " << (replay.operation.diagnostic
                    ? replay.operation.diagnostic->message : "unknown error") << '\n'; return 1;
            }
            const auto& transcript = *replay.transcript;
            std::cout << "WAV: " << wavPath.string() << "\nMODEL: " << options.modelPath.string()
                << "\nLANGUAGE: " << options.preferredLanguage.value_or("automatic")
                << "\nCOMMAND PROFILE: " << profile.id << "\nDEFAULT TARGET: " << command::defaultTargetName(options.defaultTarget)
                << "\nINPUT: exact inference PCM (VAD bypassed)\n"
                << "RAW: " << transcript.rawTranscript << "\nNORMALIZED: " << transcript.normalizedTranscript
                << "\nREPAIRED: " << transcript.repairedTranscript
                << "\nPARSED INTENT: " << (transcript.semanticCommand ? command::describe(*transcript.semanticCommand) : "none")
                << "\nVALIDATION: " << (transcript.validation.accepted ? "accepted" : "not accepted")
                << "\nRENDERED COMMAND: " << transcript.rendering.text.value_or("none")
                << "\nDETECTED LANGUAGE: " << transcript.detectedLanguage.value_or("unavailable") << '\n';
            for (const auto& repair : transcript.repairs)
                std::cout << "REPAIR [" << repair.category << "]: " << repair.reason << '\n';
            const auto number = [&](const char* label, const std::optional<double>& value) {
                std::cout << label << ": ";
                if (value) std::cout << *value; else std::cout << "unavailable";
                std::cout << '\n';
            };
            number("INFERENCE SECONDS", transcript.metrics.inferenceDurationSeconds);
            number("AUDIO SECONDS", transcript.metrics.audioDurationSeconds);
            number("REAL TIME FACTOR", transcript.diagnostics.realTimeFactor);
            for (const auto& audio : transcript.diagnostics.inferenceInputs) {
                std::cout << "INPUT SAMPLES: " << audio.sampleCount << "\nSAMPLE RATE: " << audio.sampleRate
                    << "\nPEAK LINEAR: " << audio.peakLinear << "\nRMS LINEAR: " << audio.rmsLinear
                    << "\nNEAR FULL SCALE SAMPLES: " << audio.nearFullScaleSamples
                    << "\nNEAR FULL SCALE RATIO: " << audio.nearFullScaleRatio << '\n';
                number("PEAK DBFS", audio.peakDbfs); number("RMS DBFS", audio.rmsDbfs);
            }
            if (transcript.diagnostics.compute)
                std::cout << asr::computeStatus(*transcript.diagnostics.compute) << '\n';
            std::cout << std::boolalpha;
            for (const auto& [id, value] : asr::validatedSettings(
                    core::asrSettingDescriptors(core::AsrEngineId::whisperCpp), options.settings))
                std::visit([&](auto v) { std::cout << "SETTING " << id << ": " << v << '\n'; }, value);
            for (const auto& [id, value] : transcript.diagnostics.recognitionGuidance)
                std::visit([&](auto v) { std::cout << "GUIDANCE " << id << ": " << v << '\n'; }, value);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Replay failed: " << error.what() << '\n'; return 1;
        }
    }
}
