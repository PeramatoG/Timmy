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

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace timmy::command {

    struct Channel
    {
        std::uint32_t id;
    };

    struct Fixture
    {
        std::uint32_t id;
    };

    struct Group
    {
        std::uint32_t id;
    };

    using LightingTarget = std::variant<Channel, Fixture, Group>;

    class IntensityLevel
    {
    public:
        [[nodiscard]] static std::optional<IntensityLevel> fromPercent(int percent);
        [[nodiscard]] int percent() const;

    private:
        explicit IntensityLevel(std::uint8_t percent);

        std::uint8_t percent_ = 0;
    };

    struct SetIntensity
    {
        LightingTarget target;
        IntensityLevel level;
    };

    using SemanticCommand = std::variant<SetIntensity>;

    [[nodiscard]] std::string describe(const SemanticCommand& command);

} // namespace timmy::command
