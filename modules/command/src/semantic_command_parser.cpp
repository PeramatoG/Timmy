/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/semantic_command_parser.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <string>
#include <vector>

namespace timmy::command {

    namespace {

        // Converts ASCII letters to lowercase while preserving other bytes.
        std::string asciiLower(std::string_view text)
        {
            std::string lowered(text);
            for (char& value : lowered)
            {
                const auto byte = static_cast<unsigned char>(value);
                if (byte < 0x80U)
                {
                    value = static_cast<char>(std::tolower(byte));
                }
            }
            return lowered;
        }

        // Splits assembler-normalized text on its single spaces.
        std::vector<std::string_view> splitWords(std::string_view text)
        {
            std::vector<std::string_view> words;
            std::size_t start = 0;
            while (start < text.size())
            {
                const auto end = text.find(' ', start);
                words.push_back(text.substr(start, end - start));
                if (end == std::string_view::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return words;
        }

        // Parses an unsigned decimal token completely and without overflow.
        std::optional<std::uint32_t> parseUnsigned(std::string_view token)
        {
            std::uint32_t value = 0;
            const auto parsed = std::from_chars(
                token.data(), token.data() + token.size(), value);
            if (token.empty() || parsed.ec != std::errc{}
                || parsed.ptr != token.data() + token.size())
            {
                return std::nullopt;
            }
            return value;
        }

        // Maps one supported target keyword and positive identifier to a target.
        std::optional<LightingTarget> parseTarget(
            std::string_view keyword,
            std::string_view identifier)
        {
            const auto id = parseUnsigned(identifier);
            if (!id || *id == 0)
            {
                return std::nullopt;
            }
            if (keyword == "channel" || keyword == "canal")
            {
                return Channel{*id};
            }
            if (keyword == "fixture")
            {
                return Fixture{*id};
            }
            if (keyword == "group" || keyword == "grupo")
            {
                return Group{*id};
            }
            return std::nullopt;
        }

        // Maps a full keyword or complete numeric token to a bounded level.
        std::optional<IntensityLevel> parseLevel(std::string_view token)
        {
            if (token == "full" || token == "tope")
            {
                return IntensityLevel::fromPercent(100);
            }
            if (!token.empty() && token.back() == '%')
            {
                token.remove_suffix(1);
            }
            const auto value = parseUnsigned(token);
            if (!value || *value > static_cast<std::uint32_t>(
                    std::numeric_limits<int>::max()))
            {
                return std::nullopt;
            }
            return IntensityLevel::fromPercent(static_cast<int>(*value));
        }

    } // namespace

    // Parses exactly one complete assembler-supported intensity command.
    SemanticParseResult SemanticCommandParser::parse(
        std::string_view completeText) const
    {
        const std::string lowered = asciiLower(completeText);
        const auto words = splitWords(lowered);
        if (words.size() != 4
            || (words[2] != "at" && words[2] != "al" && words[2] != "a"))
        {
            return {};
        }

        const auto target = parseTarget(words[0], words[1]);
        const auto level = parseLevel(words[3]);
        if (!target || !level)
        {
            return {};
        }

        return {
            SemanticParseState::produced,
            SemanticCommand{SetIntensity{*target, *level}}
        };
    }

} // namespace timmy::command
