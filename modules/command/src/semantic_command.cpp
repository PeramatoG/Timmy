/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/semantic_command.h"

#include <string>
#include <type_traits>

namespace timmy::command {

    // Creates an intensity only when its percentage is inside the semantic range.
    std::optional<IntensityLevel> IntensityLevel::fromPercent(int percent)
    {
        if (percent < 0 || percent > 100)
        {
            return std::nullopt;
        }
        return IntensityLevel(static_cast<std::uint8_t>(percent));
    }

    // Returns the normalized percentage represented by this level.
    int IntensityLevel::percent() const
    {
        return percent_;
    }

    // Stores a percentage already validated by the public factory.
    IntensityLevel::IntensityLevel(std::uint8_t percent)
        : percent_(percent)
    {
    }

    // Produces transport-neutral diagnostic text for a semantic command.
    std::string describe(const SemanticCommand& command)
    {
        return std::visit([](const auto& value) {
            return std::visit([&value](const auto& target) {
                using Target = std::decay_t<decltype(target)>;
                std::string targetName;
                if constexpr (std::is_same_v<Target, Channel>)
                {
                    targetName = "Channel";
                }
                else if constexpr (std::is_same_v<Target, Fixture>)
                {
                    targetName = "Fixture";
                }
                else
                {
                    targetName = "Group";
                }
                return "SetIntensity(" + targetName + "("
                    + std::to_string(target.id) + "), "
                    + std::to_string(value.level.percent()) + "%)";
            }, value.target);
        }, command);
    }

} // namespace timmy::command
