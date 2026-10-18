/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "timmy/core/asr_engine_factory.h"
#include "timmy/command/command_profile.h"

#include <algorithm>
#include <charconv>
#include <string>
#include <stdexcept>
#include <string_view>

namespace timmy::cli {
    struct AsrInputOptions
    {
        std::filesystem::path modelPath, vadModelPath;
        std::optional<std::string> preferredLanguage;
        asr::ComputeSelection compute{};
        asr::SettingValues settings;
        command::TargetType defaultTarget = command::TargetType::channel;
        std::string commandProfileName = "native";
    };
    enum class OptionParse { unknown, accepted, invalid };
    inline bool setAsrSetting(AsrInputOptions& options, std::string_view id, std::string_view text)
    {
        const auto& schema = core::asrSettingDescriptors(core::AsrEngineId::whisperCpp);
        const auto descriptor = std::find_if(schema.begin(), schema.end(),
            [&](const auto& item) { return item.id == id; });
        if (descriptor == schema.end() || text.empty()) return false;
        asr::SettingValue value;
        if (std::holds_alternative<bool>(descriptor->defaultValue)) {
            if (text == "on" || text == "true") value = true;
            else if (text == "off" || text == "false") value = false;
            else return false;
        } else if (std::holds_alternative<int>(descriptor->defaultValue)) {
            int number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
            value = number;
        } else {
            double number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
            value = number;
        }
        auto candidate = options.settings;
        candidate[std::string(id)] = value;
        try { options.settings = asr::validatedSettings(schema, candidate); }
        catch (const std::invalid_argument&) { return false; }
        return true;
    }
    inline OptionParse parseAsrInputOption(std::string_view argument, int& index,
        int argc, char* argv[], AsrInputOptions& options)
    {
        if (argument != "--model" && argument != "--vad-model" && argument != "--language"
            && argument != "--compute" && argument != "--compute-device" && argument != "--default-target"
            && argument != "--grammar" && argument != "--prompt" && argument != "--setting"
            && argument != "--command-profile") return OptionParse::unknown;
        if (++index >= argc) return OptionParse::invalid;
        const std::string_view value = argv[index];
        if (value.empty()) return OptionParse::invalid;
        if (argument == "--model") options.modelPath = value;
        else if (argument == "--vad-model") options.vadModelPath = value;
        else if (argument == "--language") options.preferredLanguage = value;
        else if (argument == "--compute") {
            try { options.compute.backend = asr::computeBackendFromId(value); }
            catch (const std::invalid_argument&) { return OptionParse::invalid; }
        } else if (argument == "--compute-device") {
            int device = -1;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), device);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || device < 0)
                return OptionParse::invalid;
            options.compute.deviceIndex = device;
        } else if (argument == "--default-target") {
            const auto target = command::defaultTargetFromName(value);
            if (!target) return OptionParse::invalid;
            options.defaultTarget = *target;
        } else if (argument == "--command-profile") {
            if (value != "native" && value != "hydra2") return OptionParse::invalid;
            options.commandProfileName = value;
        } else if (argument == "--grammar" || argument == "--prompt") {
            if (!setAsrSetting(options, argument == "--grammar" ? "whisper.grammar_enabled" : "whisper.compact_prompt", value))
                return OptionParse::invalid;
        } else {
            const auto equals = value.find('=');
            if (equals == std::string_view::npos || !setAsrSetting(options, value.substr(0, equals), value.substr(equals + 1)))
                return OptionParse::invalid;
        }
        return OptionParse::accepted;
    }
}
