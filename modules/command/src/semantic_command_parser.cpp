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
#include <string>
#include <utility>
#include <vector>

namespace timmy::command {

    namespace {

        using Words = std::vector<std::string_view>;
        enum class Action { none, intensity, select, release, test, home };

        std::string asciiLower(std::string_view text)
        {
            std::string result(text);
            for (char& value : result)
            {
                const auto byte = static_cast<unsigned char>(value);
                if (byte < 0x80U) value = static_cast<char>(std::tolower(byte));
            }
            return result;
        }

        // Keep commas as list separators; reject other punctuation in the grammar.
        Words splitWords(std::string_view text)
        {
            Words words;
            std::size_t index = 0;
            while (index < text.size())
            {
                const auto byte = static_cast<unsigned char>(text[index]);
                if (byte < 0x80U && std::isspace(byte) != 0)
                {
                    ++index;
                    continue;
                }
                const auto start = index;
                if (text[index] == ',') ++index;
                else
                {
                    while (index < text.size() && text[index] != ',')
                    {
                        const auto next = static_cast<unsigned char>(text[index]);
                        if (next < 0x80U && std::isspace(next) != 0) break;
                        ++index;
                    }
                }
                words.push_back(text.substr(start, index - start));
            }
            return words;
        }

        std::optional<std::uint32_t> parseUnsigned(std::string_view token)
        {
            std::uint32_t value = 0;
            const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
            if (token.empty() || parsed.ec != std::errc{}
                || parsed.ptr != token.data() + token.size()) return std::nullopt;
            return value;
        }

        std::optional<TargetType> targetType(std::string_view word)
        {
            if (word == "canal" || word == "canales" || word == "channel" || word == "channels")
                return TargetType::channel;
            if (word == "fixture" || word == "fixtures") return TargetType::fixture;
            if (word == "grupo" || word == "grupos" || word == "group" || word == "groups")
                return TargetType::group;
            return std::nullopt;
        }

        bool isArticle(std::string_view word)
        {
            return word == "el" || word == "la" || word == "los" || word == "las" || word == "the";
        }

        bool isConnector(std::string_view word)
        {
            return word == "at" || word == "al" || word == "a";
        }

        std::optional<IntensityLevel> parseLevel(std::string_view word)
        {
            if (word == "full" || word == "tope") return IntensityLevel::fromPercent(100);
            if (!word.empty() && word.back() == '%') word.remove_suffix(1);
            const auto percent = parseUnsigned(word);
            if (!percent || *percent > 100) return std::nullopt;
            return IntensityLevel::fromPercent(static_cast<int>(*percent));
        }

        bool isListSeparator(std::string_view word)
        {
            return word == "," || word == "y" || word == "and";
        }

        // A singular "a <level>" preserves the original intensity alias. Plural
        // targets and explicit selection actions use "a" as an inclusive range.
        bool isRangeConnector(const Words& words, std::size_t index,
            bool allowAl, bool preferRange)
        {
            const auto word = words[index];
            if (word == "thru" || word == "through" || word == "hasta" || word == "to") return true;
            if (word == "al") return allowAl;
            if (word != "a") return false;
            if (index + 1 >= words.size()) return preferRange;
            if (!parseUnsigned(words[index + 1])) return false;
            if (preferRange) return true;
            return index + 2 < words.size()
                && (isConnector(words[index + 2]) || isListSeparator(words[index + 2])
                    || words[index + 2] == "except" || words[index + 2] == "excepto");
        }

        bool parseRanges(const Words& words, std::size_t& index,
            std::vector<TargetRange>& ranges, bool allowAl, bool preferRange,
            bool& incomplete, std::size_t maxRanges)
        {
            while (index < words.size())
            {
                const auto first = parseUnsigned(words[index++]);
                if (!first || *first == 0) return false;
                auto last = *first;
                if (index < words.size() && isRangeConnector(words, index, allowAl, preferRange))
                {
                    ++index;
                    if (index == words.size())
                    {
                        incomplete = true;
                        return false;
                    }
                    const auto end = parseUnsigned(words[index++]);
                    if (!end || *end < *first) return false;
                    last = *end;
                }
                ranges.push_back({*first, last});
                if (ranges.size() > maxRanges) return false;
                if (index == words.size() || !isListSeparator(words[index])) return true;
                const bool comma = words[index++] == ",";
                if (comma && index < words.size()
                    && (words[index] == "and" || words[index] == "y")) ++index;
            }
            incomplete = true;
            return false;
        }

        std::optional<TargetSelection> parseSelection(const Words& words,
            std::size_t& index, Action action, bool& incomplete)
        {
            if (index < words.size() && isArticle(words[index])) ++index;
            if (index == words.size())
            {
                incomplete = action != Action::none;
                return std::nullopt;
            }
            const auto type = targetType(words[index]);
            if (!type) return std::nullopt;
            const bool plural = words[index].back() == 's';
            ++index;
            bool allowAl = false;
            if (index < words.size() && (words[index] == "del" || words[index] == "from"))
            {
                allowAl = words[index] == "del";
                ++index;
            }
            TargetSelection selection;
            selection.type = *type;
            const bool preferRange = plural || action == Action::select
                || action == Action::release || action == Action::test || action == Action::home;
            if (!parseRanges(words, index, selection.included, allowAl, preferRange, incomplete, TargetSelection::maxTerms)) return std::nullopt;
            if (index < words.size() && (words[index] == "except" || words[index] == "excepto"))
            {
                ++index;
                if (index < words.size() && isArticle(words[index])) ++index;
                if (!parseRanges(words, index, selection.excluded, false, true, incomplete,
                    TargetSelection::maxTerms - selection.included.size())) return std::nullopt;
            }
            if (!selection.isValid()) return std::nullopt;
            return selection;
        }

        CommandTextResult complete(SemanticCommand command)
        {
            return {CommandTextState::complete, std::move(command)};
        }

        std::optional<SelectionDirection> navigation(std::string_view word)
        {
            if (word == "next" || word == "siguiente") return SelectionDirection::next;
            if (word == "previous" || word == "prev" || word == "anterior") return SelectionDirection::previous;
            return std::nullopt;
        }

    } // namespace

    CommandTextResult SemanticCommandParser::classify(std::string_view text) const
    {
        if (text.empty() || text.size() > maxCommandLength) return {};
        const auto lowered = asciiLower(text);
        const auto words = splitWords(lowered);
        if (words.empty()) return {};

        if (lowered == "limpia editor" || lowered == "limpia el editor"
            || lowered == "borra editor" || lowered == "borra el editor"
            || lowered == "borra todo el editor" || lowered == "clear editor")
            return complete(ClearEditor{});

        if (words.size() == 1 && (words[0] == "test" || words[0] == "siguiente" || words[0] == "next"))
            return complete(AdvanceTest{});
        if (words.size() == 2 && ((words[0] == "test" && words[1] == "siguiente")
                || ((words[0] == "next" || words[0] == "siguiente") && words[1] == "test")))
            return complete(AdvanceTest{});

        if (words.size() == 2)
        {
            const auto directionFirst = navigation(words[0]);
            const auto targetLast = targetType(words[1]);
            if (directionFirst && targetLast) return complete(NavigateSelection{*targetLast, *directionFirst});
            const auto targetFirst = targetType(words[0]);
            const auto directionLast = navigation(words[1]);
            if (targetFirst && directionLast) return complete(NavigateSelection{*targetFirst, *directionLast});
        }

        Action action = Action::none;
        std::size_t index = 0;
        const auto first = words[0];
        if (first == "selecciona" || first == "select") action = Action::select;
        else if (first == "libera" || first == "suelta" || first == "release") action = Action::release;
        else if (first == "test" || first == "prueba" || first == "comprueba") action = Action::test;
        else if (first == "home" || first == "lleva") action = Action::home;
        else if (first == "pon" || first == "sube" || first == "baja" || first == "set") action = Action::intensity;
        if (action != Action::none) ++index;
        bool incomplete = false;
        const auto selection = parseSelection(words, index, action, incomplete);
        if (!selection)
        {
            return incomplete ? CommandTextResult{CommandTextState::pending, std::nullopt}
                              : CommandTextResult{};
        }

        if (index == words.size())
        {
            switch (action)
            {
            case Action::select: return complete(SelectSelection{*selection});
            case Action::release: return complete(ReleaseSelection{*selection});
            case Action::test: return complete(TestSelection{*selection});
            case Action::home:
                if (first == "home" && selection->type == TargetType::fixture)
                    return complete(HomeSelection{*selection});
                if (first == "lleva" && selection->type == TargetType::fixture)
                    return {CommandTextState::pending, std::nullopt};
                return {};
            case Action::none:
            case Action::intensity: return {CommandTextState::pending, std::nullopt};
            }
        }

        if (words.size() - index == 1 && isConnector(words[index])
            && (action == Action::none || action == Action::intensity
                || (action == Action::home && first == "lleva"
                    && selection->type == TargetType::fixture)))
            return {CommandTextState::pending, std::nullopt};

        if (words.size() - index == 2 && isConnector(words[index]))
        {
            if (words[index + 1] == "home" && selection->type == TargetType::fixture
                && (action == Action::none || (action == Action::home && first == "lleva")))
                return complete(HomeSelection{*selection});
            if (action == Action::none || action == Action::intensity)
            {
                const auto level = parseLevel(words[index + 1]);
                if (level) return complete(SetIntensity{*selection, *level});
            }
        }
        return {};
    }

    SemanticParseResult SemanticCommandParser::parse(std::string_view completeText) const
    {
        auto result = classify(completeText);
        if (result.state != CommandTextState::complete || !result.command) return {};
        return {SemanticParseState::produced, std::move(result.command)};
    }

} // namespace timmy::command
