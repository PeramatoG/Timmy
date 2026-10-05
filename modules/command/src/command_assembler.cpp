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

#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace timmy::command {

    namespace {

        // Converts ASCII letters to lowercase without changing UTF-8 bytes.
        std::string asciiLower(std::string_view text)
        {
            std::string lowered(text);
            for (char& value : lowered)
            {
                const unsigned char byte = static_cast<unsigned char>(value);
                if (byte < 0x80U)
                {
                    value = static_cast<char>(std::tolower(byte));
                }
            }
            return lowered;
        }

        // Splits normalized text at its single ASCII spaces.
        std::vector<std::string_view> splitWords(std::string_view text)
        {
            std::vector<std::string_view> words;
            std::size_t start = 0;
            while (start < text.size())
            {
                const std::size_t end = text.find(' ', start);
                words.push_back(text.substr(start, end - start));
                if (end == std::string_view::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return words;
        }

        // Reports whether a token consists only of decimal digits.
        bool isNumber(std::string_view word)
        {
            if (word.empty())
            {
                return false;
            }
            for (const char value : word)
            {
                if (value < '0' || value > '9')
                {
                    return false;
                }
            }
            return true;
        }

        // Reports whether a token is a numeric lighting level with an optional percent sign.
        bool isNumericLevel(std::string_view word)
        {
            if (!word.empty() && word.back() == '%')
            {
                word.remove_suffix(1);
            }
            return isNumber(word);
        }

        // Reports whether the words form a supported target identifier.
        bool isTarget(const std::vector<std::string_view>& words)
        {
            if (words.size() < 2 || !isNumber(words[1]))
            {
                return false;
            }
            constexpr std::array targets{
                std::string_view{"canal"}, std::string_view{"channel"},
                std::string_view{"fixture"}, std::string_view{"grupo"},
                std::string_view{"group"}};
            for (const std::string_view target : targets)
            {
                if (words[0] == target)
                {
                    return true;
                }
            }
            return false;
        }

        // Reports whether the words contain a complete target-and-level command shape.
        bool isCompleteShape(std::string_view text)
        {
            const std::string lowered = asciiLower(text);
            const auto words = splitWords(lowered);
            if (!isTarget(words) || words.size() != 4)
            {
                return false;
            }

            const bool hasConnector = words[2] == "at" || words[2] == "al"
                || words[2] == "a";
            const bool hasLevel = isNumericLevel(words[3]) || words[3] == "full"
                || words[3] == "tope";
            return hasConnector && hasLevel;
        }

        // Reports whether the text can be retained as a target awaiting its level.
        bool isPendingTarget(std::string_view text)
        {
            const std::string lowered = asciiLower(text);
            const auto words = splitWords(lowered);
            return words.size() == 2 && isTarget(words);
        }

    } // namespace

    // Accepts one normalized fragment and returns its deterministic assembly state.
    AssemblyResult CommandAssembler::accept(std::string_view normalizedFragment)
    {
        if (normalizedFragment.empty()
            || normalizedFragment.size() > maxPendingLength)
        {
            return {AssemblyState::rejected, {}};
        }

        if (isCompleteShape(normalizedFragment))
        {
            pending_.clear();
            return {AssemblyState::complete, std::string(normalizedFragment)};
        }

        if (!pending_.empty())
        {
            const std::size_t combinedLength = pending_.size() + 1
                + normalizedFragment.size();
            if (combinedLength <= maxPendingLength)
            {
                std::string combined = pending_;
                combined.push_back(' ');
                combined.append(normalizedFragment);
                if (isCompleteShape(combined))
                {
                    pending_.clear();
                    return {AssemblyState::complete, std::move(combined)};
                }
            }
        }

        if (isPendingTarget(normalizedFragment))
        {
            pending_ = normalizedFragment;
            return {AssemblyState::pending, pending_};
        }

        return {AssemblyState::rejected, {}};
    }

    // Clears any retained incomplete command fragment.
    void CommandAssembler::reset()
    {
        pending_.clear();
    }

    // Reports whether an incomplete command fragment is currently retained.
    bool CommandAssembler::hasPending() const
    {
        return !pending_.empty();
    }

} // namespace timmy::command
