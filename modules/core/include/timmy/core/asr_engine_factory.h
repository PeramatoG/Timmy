/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "timmy/asr/asr_backend.h"
#include "timmy/asr/configurable_settings.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace timmy::core {

    enum class AsrEngineId
    {
        whisperCpp
    };

    struct AsrEngineDescriptor
    {
        AsrEngineId id;
        std::string_view displayName;
    };

    struct AsrEngineResources
    {
        std::filesystem::path voiceActivityModelPath;
    };

    [[nodiscard]] const std::vector<AsrEngineDescriptor>& availableAsrEngines();
    [[nodiscard]] const std::vector<asr::SettingDescriptor>& asrSettingDescriptors(
        AsrEngineId engine);
    [[nodiscard]] std::shared_ptr<asr::IAsrBackend> createAsrBackend(
        AsrEngineId engine,
        const AsrEngineResources& resources,
        const asr::SettingValues& settings = {});

} // namespace timmy::core
