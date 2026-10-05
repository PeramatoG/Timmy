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

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace timmy::asr {

    enum class AsrEngine
    {
        whisperCpp
    };

    struct AsrEngineInfo
    {
        AsrEngine engine = AsrEngine::whisperCpp;
        std::string displayName;
        bool usesVadModelResource = false;
    };

    struct AsrBackendCreateOptions
    {
        AsrEngine engine = AsrEngine::whisperCpp;
        std::filesystem::path vadModelResourcePath;
    };

    [[nodiscard]]
    std::vector<AsrEngineInfo> availableAsrEngines();

    [[nodiscard]]
    std::shared_ptr<IAsrBackend> createAsrBackend(
        const AsrBackendCreateOptions& options);

} // namespace timmy::asr
