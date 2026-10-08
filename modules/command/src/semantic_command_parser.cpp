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
#include "timmy/command/command_lexicon.h"

#include <charconv>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace timmy::command {

    namespace {

        using Words = std::vector<std::string>;
        enum class Action { none, intensity, select, release, test, home, homeTo, add, remove };

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
            if (word == "channel" || word == "channels") return TargetType::channel;
            if (word == "fixture" || word == "fixtures") return TargetType::fixture;
            if (word == "group" || word == "groups") return TargetType::group;
            return std::nullopt;
        }

        bool isConnector(std::string_view word)
        {
            return word == "at" || word == "al" || word == "a";
        }

        bool isModification(std::string_view word)
        {
            return word == "add" || word == "remove" || word == canonicalScopedRemove;
        }

        bool startsModification(const Words& words, std::size_t index)
        {
            if (index < words.size() && words[index] == "and") ++index;
            if (index < words.size() && words[index] == "me") ++index;
            return index < words.size() && isModification(words[index]);
        }

        std::optional<IntensityLevel> parseLevel(std::string_view word)
        {
            if (word == "full") return IntensityLevel::fromPercent(100);
            if (!word.empty() && word.back() == '%') word.remove_suffix(1);
            const auto percent = parseUnsigned(word);
            if (!percent || *percent > 100) return std::nullopt;
            return IntensityLevel::fromPercent(static_cast<int>(*percent));
        }

        bool isListSeparator(std::string_view word)
        {
            return word == "," || word == "and";
        }

        // Preserve CMD-040's singular intensity aliases. An explicit selector,
        // range prefix or following clause makes Spanish range "al" unambiguous.
        bool isRangeConnector(const Words& words, std::size_t index,
            bool allowAl, bool preferRange)
        {
            const auto& word = words[index];
            if (word == "through") return true;
            if (word != "a" && word != "al") return false;
            if (index + 1 >= words.size())
                return (word == "a" && preferRange) || (word == "al" && allowAl);
            if (!parseUnsigned(words[index + 1])) return false;
            const bool followedByClause = index + 2 < words.size()
                && (isConnector(words[index + 2]) || isListSeparator(words[index + 2])
                    || startsModification(words, index + 2));
            if (word == "al") return allowAl || followedByClause;
            return preferRange || followedByClause;
        }

        bool parseRanges(const Words& words, std::size_t& index,
            std::vector<TargetRange>& ranges, bool allowAl, bool preferRange,
            bool& incomplete, std::size_t maxRanges)
        {
            if (index < words.size() && words[index] == canonicalArticle) ++index;
            if (index < words.size() && (words[index] == "del" || words[index] == "from"))
            {
                allowAl = allowAl || words[index] == "del";
                ++index;
            }
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
                if (index == words.size() || !isListSeparator(words[index])
                    || startsModification(words, index)) return true;
                const bool comma = words[index++] == ",";
                if (comma && index < words.size() && words[index] == "and") ++index;
                if (index < words.size() && words[index] == canonicalArticle) ++index;
            }
            incomplete = true;
            return false;
        }

        CommandTextResult pending()
        {
            return {CommandTextState::pending, std::nullopt, std::nullopt};
        }

        CommandTextResult complete(SemanticCommand command,
            std::optional<TargetType> explicitTarget = std::nullopt)
        {
            return {CommandTextState::complete, std::move(command), explicitTarget};
        }

        std::optional<SelectionDirection> navigation(std::string_view word)
        {
            if (word == "next") return SelectionDirection::next;
            if (word == "previous") return SelectionDirection::previous;
            return std::nullopt;
        }

        // Editor/selection scope is mandatory: a bare clear remains invalid.
        std::optional<SemanticCommand> scopedEditorAction(const Words& words)
        {
            if (words[0] != "clear" && words[0] != canonicalScopedRemove && words[0] != "release")
                return std::nullopt;
            std::size_t index = 1;
            if (index < words.size() && words[index] == canonicalArticle) ++index;
            if (index == words.size()) return std::nullopt;
            const auto& scope = words[index++];
            if (scope == "all" && index < words.size())
            {
                if (words[index] == canonicalArticle) ++index;
                if (index == words.size() || words[index++] != "editor") return std::nullopt;
            }
            if (index != words.size()) return std::nullopt;
            if (words[0] == "release")
            {
                if (scope == "all" || scope == "editor") return ReleaseEditor{};
                return std::nullopt;
            }
            if (scope == "selection") return ClearSelection{};
            if (scope == "all" || scope == "editor") return ClearEditor{};
            return std::nullopt;
        }

        Action actionFor(std::string_view word)
        {
            if (word == "select") return Action::select;
            if (word == "release") return Action::release;
            if (word == "test") return Action::test;
            if (word == "home") return Action::home;
            if (word == canonicalHomeTo) return Action::homeTo;
            if (word == "set") return Action::intensity;
            if (word == "add") return Action::add;
            if (word == "remove" || word == canonicalScopedRemove) return Action::remove;
            return Action::none;
        }

        CommandTextResult relativeSelection(const Words& words, Action action,
            const CommandContext& context)
        {
            std::size_t index = 1;
            if (index + 1 < words.size() && words[index] == canonicalArticle
                && targetType(words[index + 1])) ++index;
            std::optional<TargetType> explicitType;
            if (index < words.size()) explicitType = targetType(words[index]);
            if (explicitType) ++index;
            const auto type = explicitType ? explicitType : context.lastExplicitTargetType;
            if (!type) return {};
            bool incomplete = false;
            TargetSelection selection{*type, {}};
            if (!parseRanges(words, index, selection.included, true, true, incomplete,
                    TargetSelection::maxTerms))
                return incomplete ? pending() : CommandTextResult{};
            if (index != words.size() || !selection.isValid()) return {};
            return complete(ModifySelection{action == Action::add
                    ? SelectionModification::add : SelectionModification::remove,
                std::move(selection)}, explicitType);
        }

    } // namespace

    SemanticCommandParser::SemanticCommandParser(SemanticParsingConfiguration configuration)
        : configuration_(std::move(configuration)) {}

    CommandTextResult SemanticCommandParser::classify(std::string_view text,
        const CommandContext& context) const
    {
        if (text.empty() || text.size() > maxCommandLength) return {};
        auto words = canonicalizeCommandText(text, configuration_.vocabulary);
        if (words.empty()) return {};
        const bool prefixLevel = words[0] == "on" || words[0] == "off" || words[0] == "full";
        const bool prefixOff = words[0] == "off";
        // A numeric sentence comma can finish intensity, but a selection's
        // terminal comma is a pending list connector. Keep that distinction in
        // the grammar rather than transcript formatting.
        auto commaCandidate = text;
        while (!commaCandidate.empty())
        {
            const auto byte = static_cast<unsigned char>(commaCandidate.back());
            if (byte >= 0x80U || std::isspace(byte) == 0) break;
            commaCandidate.remove_suffix(1);
        }
        if (!prefixLevel && !commaCandidate.empty() && commaCandidate.back() == ',')
        {
            commaCandidate.remove_suffix(1);
            while (!commaCandidate.empty())
            {
                const auto byte = static_cast<unsigned char>(commaCandidate.back());
                if (byte >= 0x80U || std::isspace(byte) == 0) break;
                commaCandidate.remove_suffix(1);
            }
            if (!commaCandidate.empty() && commaCandidate.back() >= '0' && commaCandidate.back() <= '9')
            {
                auto candidate = classify(commaCandidate, context);
                if (candidate.state == CommandTextState::complete && candidate.command
                    && std::holds_alternative<SetIntensity>(*candidate.command)) return candidate;
            }
        }
        // Prefix actions reuse selection grammar directly. Preserve its pending
        // range/list states; do not append synthetic tokens to an ASR fragment.
        if (prefixLevel) words[0] = "set";

        if (auto command = scopedEditorAction(words)) return complete(std::move(*command));

        if (words.size() == 1 && (words[0] == "test" || words[0] == "next"))
            return complete(AdvanceTest{});
        if (words.size() == 2 && ((words[0] == "test" && words[1] == "next")
                || (words[0] == "next" && words[1] == "test")))
            return complete(AdvanceTest{});

        if (words.size() == 2)
        {
            const auto directionFirst = navigation(words[0]);
            const auto targetLast = targetType(words[1]);
            if (directionFirst && targetLast)
                return complete(NavigateSelection{*targetLast, *directionFirst}, targetLast);
            const auto targetFirst = targetType(words[0]);
            const auto directionLast = navigation(words[1]);
            if (targetFirst && directionLast)
                return complete(NavigateSelection{*targetFirst, *directionLast}, targetFirst);
        }

        const auto action = actionFor(words[0]);
        if (action == Action::add || action == Action::remove)
            return relativeSelection(words, action, context);
        std::size_t index = action == Action::none ? 0 : 1;
        if (index < words.size() && words[index] == canonicalArticle) ++index;
        if (index == words.size()) return action != Action::none ? pending() : CommandTextResult{};
        const auto explicitType = targetType(words[index]);
        const auto type = explicitType ? explicitType : (prefixLevel ? configuration_.defaultTarget : std::nullopt);
        if (!type) return {};
        const bool plural = explicitType && words[index].back() == 's';
        if (explicitType) ++index;
        bool incomplete = false;
        TargetSelection selection{*type, {}};
        const bool selectionAction = action == Action::select || action == Action::release
            || action == Action::test || action == Action::home || action == Action::homeTo;
        if (index < words.size() && words[index] == "all")
        {
            selection.all = true;
            ++index;
        }
        else if (!parseRanges(words, index, selection.included, selectionAction || (prefixLevel && plural),
                plural || selectionAction, incomplete, TargetSelection::maxTerms))
            return incomplete ? pending() : CommandTextResult{};

        bool modified = false;
        bool removed = false;
        while (index < words.size() && startsModification(words, index))
        {
            if (words[index] == "and") ++index;
            if (words[index] == "me") ++index;
            const bool remove = words[index++] != "add";
            // Repeated exclusions and additions after exclusions cannot preserve
            // spoken evaluation order in one included/excluded selection.
            if (removed) return {};
            removed = remove;
            modified = true;
            if (index + 1 < words.size() && words[index] == canonicalArticle
                && targetType(words[index + 1])) ++index;
            if (index < words.size())
            {
                const auto clauseType = targetType(words[index]);
                if (clauseType)
                {
                    if (*clauseType != *type) return {};
                    ++index;
                }
            }
            auto& ranges = remove ? selection.excluded : selection.included;
            const auto otherTerms = remove ? selection.included.size() : selection.excluded.size();
            if (!parseRanges(words, index, ranges, !remove || selectionAction, true, incomplete,
                    TargetSelection::maxTerms - otherTerms))
                return incomplete ? pending() : CommandTextResult{};
        }
        if (!selection.isValid()) return {};

        if (index == words.size())
        {
            if (prefixLevel)
                return complete(SetIntensity{selection, *IntensityLevel::fromPercent(prefixOff ? 0 : 100)}, explicitType);
            switch (action)
            {
            case Action::select: return complete(SelectSelection{selection}, type);
            case Action::release: return complete(ReleaseSelection{selection}, type);
            case Action::test: return complete(TestSelection{selection}, type);
            case Action::home:
                return *type == TargetType::fixture ? complete(HomeSelection{selection}, type)
                    : CommandTextResult{};
            case Action::homeTo:
                return *type == TargetType::fixture ? pending() : CommandTextResult{};
            case Action::none:
                if (modified) return complete(SelectSelection{selection}, type);
                return pending();
            case Action::intensity: return pending();
            case Action::add:
            case Action::remove: return {};
            }
        }

        // An on/off prefix supplies the value. Reject an additional value clause
        // rather than silently overriding either part of the spoken command.
        if (prefixLevel) return {};
        const bool acceptsIntensity = action == Action::none || action == Action::intensity;
        const bool acceptsHome = *type == TargetType::fixture
            && (action == Action::none || action == Action::homeTo);
        if (words.size() - index == 1 && isConnector(words[index])
            && (acceptsIntensity || acceptsHome)) return pending();

        if (words.size() - index == 2 && isConnector(words[index]))
        {
            if (words[index + 1] == "home" && acceptsHome)
                return complete(HomeSelection{selection}, type);
            if (acceptsIntensity)
            {
                const auto level = parseLevel(words[index + 1]);
                if (level) return complete(SetIntensity{selection, *level}, explicitType);
            }
        }
        return {};
    }

    SemanticParseResult SemanticCommandParser::parse(std::string_view completeText,
        const CommandContext& context) const
    {
        auto result = classify(completeText, context);
        if (result.state != CommandTextState::complete || !result.command) return {};
        return {SemanticParseState::produced, std::move(result.command), result.explicitTargetType};
    }

    void CommandContext::observe(const SemanticParseResult& result)
    {
        if (result.state != SemanticParseState::produced || !result.command || !result.explicitTargetType)
            return;
        const auto type = *result.explicitTargetType;
        if (type == TargetType::channel || type == TargetType::fixture || type == TargetType::group)
            lastExplicitTargetType = type;
    }

    void CommandContext::reset()
    {
        lastExplicitTargetType.reset();
    }

} // namespace timmy::command
