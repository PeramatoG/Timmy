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

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace timmy::asr {

    using SettingValue = std::variant<bool, int, double>;
    using SettingValues = std::map<std::string, SettingValue>;

    struct SettingDescriptor
    {
        std::string id;
        std::string label;
        std::string section;
        SettingValue defaultValue;
        std::optional<double> minimum;
        std::optional<double> maximum;
    };

    [[nodiscard]] SettingValues defaultSettings(
        const std::vector<SettingDescriptor>& descriptors);

    // Reject unknown IDs, wrong types, nonfinite values and out-of-range values.
    // Missing IDs use descriptor defaults; validation never clamps user values.
    [[nodiscard]] SettingValues validatedSettings(
        const std::vector<SettingDescriptor>& descriptors, const SettingValues& values);
} // namespace timmy::asr
