/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_processor.h"
#include <type_traits>
#include <utility>

namespace timmy::command {
    namespace {
        // Console key syntax belongs to rendering; OSC addresses stay in transport.
        std::optional<std::string> targetKey(TargetType type)
        {
            switch (type)
            {
            case TargetType::channel: return "CH";
            case TargetType::fixture: return "FXT";
            case TargetType::group: return "GRP";
            }
            return std::nullopt;
        }

        void appendNumber(std::vector<std::string>& keys, std::uint32_t number)
        {
            for (char digit : std::to_string(number)) keys.emplace_back(1, digit);
        }

        void appendRange(std::vector<std::string>& keys,
            const TargetRange& range)
        {
            appendNumber(keys, range.first);
            if (range.first != range.last)
            {
                keys.push_back("THRU");
                appendNumber(keys, range.last);
            }
        }

        // OK is Hydra's additive selection separator; ranges stay compact.
        void appendIncluded(std::vector<std::string>& keys,
            const std::vector<TargetRange>& terms)
        {
            for (std::size_t index = 0; index < terms.size(); ++index)
            {
                if (index != 0) keys.push_back("OK");
                appendRange(keys, terms[index]);
            }
        }

        bool appendSelection(std::vector<std::string>& keys,
            const TargetSelection& selection)
        {
            const auto typeKey = targetKey(selection.type);
            if (!selection.isValid() || selection.all || !typeKey) return false;
            keys.push_back(*typeKey);
            appendIncluded(keys, selection.included);
            for (const auto& excluded : selection.excluded)
            {
                // Each exclusion subtracts a term; an additive OK separator
                // would instead risk putting that term back in the selection.
                keys.push_back("EXCP");
                appendRange(keys, excluded);
            }
            return true;
        }

        bool appendModification(std::vector<std::string>& keys,
            const ModifySelection& operation)
        {
            const auto typeKey = targetKey(operation.target.type);
            if (!operation.target.isValid() || operation.target.all || !typeKey
                || !operation.target.excluded.empty()) return false;
            const char* modifier = nullptr;
            switch (operation.modification)
            {
            case SelectionModification::add: modifier = "OK"; break;
            case SelectionModification::remove: modifier = "EXCP"; break;
            default: return false;
            }
            // Hydra II / Piccolo II v7.04, sections 5.1.1 and 5.2.2:
            // OK is plus, and EXCP precedes a typed subtractive term. Start
            // with the modifier to extend the console's current selection.
            // Repeat EXCP for every removed range, never an additive OK.
            for (const auto& term : operation.target.included)
            {
                keys.push_back(modifier);
                keys.push_back(*typeKey);
                appendRange(keys, term);
            }
            keys.push_back("OK");
            return true;
        }
    }

    IntentRenderingResult renderHydra2Intent(const CommandIntent& commandValue, bool preClear)
    {
        const auto validation = validateIntent(commandValue);
        if (!validation.accepted) return {std::nullopt, validation.diagnostic};
        std::vector<std::string> keys;
        const bool valid = std::visit([&](const auto& operation) {
            using Operation = std::decay_t<decltype(operation)>;
            // Standalone actions and relative edits bypass optional pre-clear.
            if constexpr (std::is_same_v<Operation, AdvanceTest>)
            {
                keys.push_back("TEST");
            }
            else if constexpr (std::is_same_v<Operation, ClearEditor>)
            {
                keys.push_back("RST");
            }
            else if constexpr (std::is_same_v<Operation, ClearSelection>)
            {
                // v7.04 section 5.1.1: clear selection, retaining edited values.
                keys = {"C", "C"};
            }
            else if constexpr (std::is_same_v<Operation, ReleaseEditor>)
            {
                // v7.04 section 5.2.12 (5-16): release the complete editor
                // with fading; the adjacent sudden-clear command is RST.
                keys = {"REL", "REL"};
            }
            else if constexpr (std::is_same_v<Operation, ModifySelection>)
            {
                if (!appendModification(keys, operation)) return false;
            }
            else
            {
                if (preClear) keys = {"C", "C"};
                if constexpr (std::is_same_v<Operation, NavigateSelection>)
                {
                    const auto typeKey = targetKey(operation.type);
                    if (!typeKey) return false;
                    keys.push_back(*typeKey);
                    switch (operation.direction)
                    {
                    case SelectionDirection::next: keys.push_back("NEXT"); break;
                    case SelectionDirection::previous: keys.push_back("PREV"); break;
                    default: return false;
                    }
                }
                else
                {
                    if constexpr (std::is_same_v<Operation, HomeSelection>)
                    {
                        if (operation.target.type != TargetType::fixture)
                            return false;
                    }
                    if (!appendSelection(keys, operation.target)) return false;
                    if constexpr (std::is_same_v<Operation, SetIntensity>)
                    {
                        keys.push_back("AT");
                        const int level = operation.level.percent();
                        if (level == 100) keys.push_back("AT");
                        else
                        {
                            keys.push_back(std::to_string(level / 10));
                            keys.push_back(std::to_string(level % 10));
                        }
                    }
                    else if constexpr (std::is_same_v<Operation, SelectSelection>)
                        keys.push_back("OK");
                    else if constexpr (std::is_same_v<Operation, ReleaseSelection>)
                        keys.push_back("REL");
                    else if constexpr (std::is_same_v<Operation, TestSelection>)
                        keys.push_back("TEST");
                    else if constexpr (std::is_same_v<Operation, HomeSelection>)
                        keys.push_back("HOME");
                }
            }
            return true;
        }, commandValue);
        if (!valid)
            return {std::nullopt, "Invalid target selection or operation for Hydra II."};
        std::string text;
        for (const auto& key : keys)
        {
            if (!text.empty()) text += " ";
            text += key;
        }
        return {std::move(text), {}, RenderingTarget::hydra2Keys, std::move(keys)};
    }
}
