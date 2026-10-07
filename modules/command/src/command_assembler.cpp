/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_assembler.h"

#include "timmy/command/semantic_command_parser.h"

#include <cctype>
#include <string>
#include <utility>

namespace timmy::command {

    namespace {

        // Only the literal TEST fragment completes a retained selection before
        // considering a standalone action. Other advance phrases stay actions.
        bool isTestFragment(std::string_view fragment)
        {
            if (fragment.size() != 4) return false;
            constexpr std::string_view test{"test"};
            for (std::size_t index = 0; index < fragment.size(); ++index)
            {
                const auto byte = static_cast<unsigned char>(fragment[index]);
                if (byte >= 0x80U || std::tolower(byte) != test[index]) return false;
            }
            return true;
        }

    } // namespace

    AssemblyResult CommandAssembler::accept(std::string_view normalizedFragment)
    {
        if (normalizedFragment.empty() || normalizedFragment.size() > maxPendingLength)
            return {AssemblyState::rejected, {}};

        const SemanticCommandParser parser;
        if (!pending_.empty() && isTestFragment(normalizedFragment)
            && pending_.size() + normalizedFragment.size() + 1 <= maxPendingLength)
        {
            std::string combined = "test " + pending_;
            if (parser.classify(combined).state == CommandTextState::complete)
            {
                pending_.clear();
                return {AssemblyState::complete, std::move(combined)};
            }
        }

        const auto fragment = parser.classify(normalizedFragment);
        if (fragment.state == CommandTextState::complete)
        {
            pending_.clear();
            return {AssemblyState::complete, std::string(normalizedFragment)};
        }

        if (!pending_.empty()
            && pending_.size() + normalizedFragment.size() + 1 <= maxPendingLength)
        {
            std::string combined = pending_ + " " + std::string(normalizedFragment);
            const auto classification = parser.classify(combined);
            if (classification.state == CommandTextState::complete)
            {
                pending_.clear();
                return {AssemblyState::complete, std::move(combined)};
            }
            if (classification.state == CommandTextState::pending)
            {
                pending_ = std::move(combined);
                return {AssemblyState::pending, pending_};
            }
        }

        if (fragment.state == CommandTextState::pending)
        {
            pending_ = normalizedFragment;
            return {AssemblyState::pending, pending_};
        }
        return {AssemblyState::rejected, {}};
    }

    void CommandAssembler::reset()
    {
        pending_.clear();
    }

    bool CommandAssembler::hasPending() const
    {
        return !pending_.empty();
    }

} // namespace timmy::command
