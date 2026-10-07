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

#include <algorithm>
#include <string>
#include <utility>
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

    namespace {

        std::string targetName(TargetType type)
        {
            switch (type)
            {
            case TargetType::channel: return "Channel";
            case TargetType::fixture: return "Fixture";
            case TargetType::group: return "Group";
            }
            return "InvalidTarget";
        }

        std::string describeRanges(const std::vector<TargetRange>& ranges)
        {
            std::string result;
            for (const auto& range : ranges)
            {
                if (!result.empty()) result += ", ";
                result += std::to_string(range.first);
                if (range.first != range.last)
                {
                    result += " through " + std::to_string(range.last);
                }
            }
            return result;
        }

    } // namespace

    TargetSelection::TargetSelection(TargetType targetType,
        std::vector<TargetRange> includedRanges,
        std::vector<TargetRange> excludedRanges)
        : type(targetType), included(std::move(includedRanges)),
          excluded(std::move(excludedRanges))
    {
    }

    TargetSelection::TargetSelection(const LightingTarget& target)
    {
        std::visit([this](const auto& value) {
            using Target = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Target, Channel>) type = TargetType::channel;
            else if constexpr (std::is_same_v<Target, Fixture>) type = TargetType::fixture;
            else type = TargetType::group;
            included.push_back({value.id, value.id});
        }, target);
    }

    TargetSelection::TargetSelection(Channel target)
        : TargetSelection(LightingTarget{target}) {}

    TargetSelection::TargetSelection(Fixture target)
        : TargetSelection(LightingTarget{target}) {}

    TargetSelection::TargetSelection(Group target)
        : TargetSelection(LightingTarget{target}) {}

    bool TargetSelection::isValid() const
    {
        if ((type != TargetType::channel && type != TargetType::fixture
                && type != TargetType::group)
            || (all ? (!included.empty() || !excluded.empty()) : included.empty())
            || included.size() + excluded.size() > maxTerms)
        {
            return false;
        }
        const auto validRanges = [](const std::vector<TargetRange>& ranges) {
            return std::all_of(ranges.begin(), ranges.end(), [](const auto& range) {
                return range.first > 0 && range.first <= range.last;
            });
        };
        return validRanges(included) && validRanges(excluded);
    }

    std::optional<LightingTarget> TargetSelection::singleTarget() const
    {
        if (!isValid() || included.size() != 1 || !excluded.empty()
            || included.front().first != included.front().last)
        {
            return std::nullopt;
        }
        const auto id = included.front().first;
        switch (type)
        {
        case TargetType::channel: return Channel{id};
        case TargetType::fixture: return Fixture{id};
        case TargetType::group: return Group{id};
        }
        return std::nullopt;
    }

    std::string describe(const TargetSelection& selection)
    {
        std::string result = targetName(selection.type) + "("
            + (selection.all ? "All" : describeRanges(selection.included));
        if (!selection.excluded.empty())
        {
            result += " except " + describeRanges(selection.excluded);
        }
        return result + ")";
    }

    // Produces readable, transport-neutral diagnostics for every operation.
    std::string describe(const SemanticCommand& command)
    {
        return std::visit([](const auto& value) -> std::string {
            using Operation = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Operation, SetIntensity>)
            {
                return "SetIntensity(" + describe(value.target) + ", "
                    + std::to_string(value.level.percent()) + "%)";
            }
            else if constexpr (std::is_same_v<Operation, AdvanceTest>)
            {
                return "AdvanceTest()";
            }
            else if constexpr (std::is_same_v<Operation, ClearEditor>)
            {
                return "ClearEditor()";
            }
            else if constexpr (std::is_same_v<Operation, ClearSelection>)
            {
                return "ClearSelection()";
            }
            else if constexpr (std::is_same_v<Operation, ReleaseEditor>)
            {
                return "ReleaseEditor()";
            }
            else if constexpr (std::is_same_v<Operation, ModifySelection>)
            {
                return "ModifySelection(" + std::string(value.modification == SelectionModification::add
                    ? "Add" : "Remove") + ", " + describe(value.target) + ")";
            }
            else if constexpr (std::is_same_v<Operation, NavigateSelection>)
            {
                return "NavigateSelection(" + targetName(value.type) + ", "
                    + (value.direction == SelectionDirection::next ? "Next" : "Previous")
                    + ")";
            }
            else
            {
                std::string operation;
                if constexpr (std::is_same_v<Operation, SelectSelection>) operation = "SelectSelection";
                else if constexpr (std::is_same_v<Operation, ReleaseSelection>) operation = "ReleaseSelection";
                else if constexpr (std::is_same_v<Operation, TestSelection>) operation = "TestSelection";
                else operation = "HomeSelection";
                return operation + "(" + describe(value.target) + ")";
            }
        }, command);
    }

} // namespace timmy::command
