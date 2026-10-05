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

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace timmy::core {

    struct ResourcePathRequest
    {
        std::string_view environmentVariable;
        std::filesystem::path suppliedPath;
        std::filesystem::path executablePath;
        std::filesystem::path developmentRelativePath;
    };

    struct ResourcePathResult
    {
        std::filesystem::path path;
        bool found = false;
        std::string diagnostic;
    };

    using EnvironmentLookup =
        std::function<std::optional<std::filesystem::path>(std::string_view)>;

    [[nodiscard]] ResourcePathResult resolveResourcePath(
        const ResourcePathRequest& request,
        EnvironmentLookup environmentLookup = {});

} // namespace timmy::core
