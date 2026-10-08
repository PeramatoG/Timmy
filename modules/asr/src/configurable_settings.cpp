/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/configurable_settings.h"

#include <cmath>
#include <stdexcept>

namespace timmy::asr {

    SettingValues defaultSettings(const std::vector<SettingDescriptor>& descriptors)
    {
        SettingValues values;
        for (const auto& descriptor : descriptors)
        {
            values.emplace(descriptor.id, descriptor.defaultValue);
        }
        return values;
    }

    SettingValues validatedSettings(
        const std::vector<SettingDescriptor>& descriptors, const SettingValues& values)
    {
        auto result = defaultSettings(descriptors);
        for (const auto& [id, value] : values)
        {
            const auto found = result.find(id);
            if (found == result.end() || found->second.index() != value.index())
                throw std::invalid_argument("Unknown setting or invalid type: " + id);
            found->second = value;
        }
        for (const auto& descriptor : descriptors)
        {
            const auto& value = result.at(descriptor.id);
            if (std::holds_alternative<bool>(value)) continue;
            const double number = std::holds_alternative<int>(value)
                ? static_cast<double>(std::get<int>(value)) : std::get<double>(value);
            if (!std::isfinite(number) || (descriptor.minimum && number < *descriptor.minimum)
                || (descriptor.maximum && number > *descriptor.maximum))
                throw std::invalid_argument("Setting out of range: " + descriptor.id);
        }
        return result;
    }
} // namespace timmy::asr
