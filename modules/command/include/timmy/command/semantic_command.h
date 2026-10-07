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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

    enum class TargetType
    {
        channel,
        fixture,
        group
    };

    // An inclusive span stays compact even for a large console selection.
    struct TargetRange
    {
        std::uint32_t first;
        std::uint32_t last;
    };

    struct TargetSelection
    {
        static constexpr std::size_t maxTerms = 32;

        TargetType type{TargetType::channel};
        std::vector<TargetRange> included;
        std::vector<TargetRange> excluded;

        TargetSelection() = default;
        TargetSelection(TargetType type, std::vector<TargetRange> included,
            std::vector<TargetRange> excluded = {});
        TargetSelection(const LightingTarget& target);
        TargetSelection(Channel target);
        TargetSelection(Fixture target);
        TargetSelection(Group target);

        [[nodiscard]] bool isValid() const;
        [[nodiscard]] std::optional<LightingTarget> singleTarget() const;
    };

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
        TargetSelection target;
        IntensityLevel level;
    };

    struct SelectSelection { TargetSelection target; };
    struct ReleaseSelection { TargetSelection target; };
    struct TestSelection { TargetSelection target; };
    struct HomeSelection { TargetSelection target; };
    struct AdvanceTest {};
    struct ClearEditor {};
    struct ClearSelection {};
    struct ReleaseEditor {};

    enum class SelectionModification { add, remove };
    struct ModifySelection
    {
        SelectionModification modification;
        TargetSelection target;
    };

    enum class SelectionDirection { next, previous };
    struct NavigateSelection
    {
        TargetType type;
        SelectionDirection direction;
    };

    using SemanticCommand = std::variant<SetIntensity, SelectSelection,
        ReleaseSelection, TestSelection, HomeSelection, AdvanceTest,
        ClearEditor, ClearSelection, ReleaseEditor, ModifySelection, NavigateSelection>;

    [[nodiscard]] std::string describe(const TargetSelection& selection);

    [[nodiscard]] std::string describe(const SemanticCommand& command);

} // namespace timmy::command
