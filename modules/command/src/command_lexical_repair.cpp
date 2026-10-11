/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_lexical_repair.h"
#include "timmy/command/semantic_command_parser.h"
#include <array>
#include <charconv>
#include <cctype>
#include <span>
#include <utility>

namespace timmy::command {
    namespace {
        struct Token { std::string_view text; std::size_t begin; std::size_t end; };
        struct Root { std::string_view text; std::string_view target; bool prefix; };
        constexpr Root roots[] = {
            {"fi", "fixture", false}, {"fis", "fixture", true}, {"fix", "fixture", true},
            {"apa", "fixture", false}, {"apar", "fixture", true},
            {"cha", "channel", false}, {"che", "channel", false}, {"can", "channel", false},
            {"chan", "channel", true}, {"cana", "channel", true},
            {"gr", "group", false}, {"gru", "group", false}, {"gro", "group", false},
            {"grup", "group", true}, {"grou", "group", true}
        };
        std::string lower(std::string_view text)
        {
            std::string result(text);
            for (auto& byte : result)
                if (static_cast<unsigned char>(byte) < 128)
                    byte = static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
            return result;
        }
        std::string_view candidate(std::string_view text)
        {
            std::string_view result;
            for (const auto& root : roots) {
                if (text == root.text || (root.prefix && text.starts_with(root.text))) {
                    if (!result.empty() && result != root.target) return {}; // Unique role only.
                    result = root.target;
                }
            }
            // Word families cannot contain identifiers or punctuation.
            for (const auto byte : text)
                if (byte < 'a' || byte > 'z') return {};
            return result;
        }
        bool number(std::string_view text)
        {
            unsigned int value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            return !text.empty() && error == std::errc{} && end == text.data() + text.size() && value > 0;
        }
        bool target(std::string_view word)
        {
            return word == "channel" || word == "channels" || word == "fixture"
                || word == "fixtures" || word == "group" || word == "groups";
        }
        bool action(std::string_view word)
        {
            return word == "select" || word == "test" || word == "home" || word == "set"
                || word == "release" || word == "add" || word == "remove"
                || word == canonicalScopedRemove || word == canonicalHomeTo
                || word == "on" || word == "off" || word == "full";
        }
    }
    CommandLexicalRepair::CommandLexicalRepair(SemanticParsingConfiguration configuration)
        : configuration_(std::move(configuration)) {}

    RepairedTranscript CommandLexicalRepair::repair(std::string_view text) const
    {
        RepairedTranscript result{std::string(text), {}};
        // Match the parser's input bound; never make oversized input acceptable.
        if (text.empty() || text.size() > SemanticCommandParser::maxCommandLength) return result;
        std::array<Token, SemanticCommandParser::maxCommandLength> tokenStorage{};
        std::size_t tokenCount = 0;
        bool suspicious = false;
        for (std::size_t i = 0; i < text.size();) {
            if (std::isspace(static_cast<unsigned char>(text[i]))) { ++i; continue; }
            const auto begin = i;
            if (text[i] == ',') { ++i; suspicious = true; }
            else while (i < text.size() && text[i] != ','
                && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const auto word = text.substr(begin, i - begin);
            const auto folded = lower(word);
            if (!candidate(folded).empty()) {
                bool known = false;
                for (const auto& alias : configuration_.vocabulary)
                    if (alias.spoken == folded) { known = true; break; }
                suspicious = suspicious || !known;
            }
            tokenStorage[tokenCount++] = {word, begin, i};
        }
        if (!suspicious) return result;
        const std::span<const Token> tokens{tokenStorage.data(), tokenCount};
        // Canonical tokens are only used to inspect roles. Preserve original bytes.
        std::vector<std::string> words;
        words.reserve(tokens.size());
        for (const auto& token : tokens) {
            const auto canonical = canonicalizeCommandText(token.text, configuration_.vocabulary);
            if (canonical.size() != 1) return result;
            words.push_back(canonical.front());
        }
        std::vector<std::string> replacements(tokens.size());
        std::vector<bool> removed(tokens.size(), false);
        bool expectTarget = true;
        bool selection = false;
        bool preferRange = false;
        bool allowAl = false;
        bool selectionAction = false;
        auto nextMeaningful = [&](std::size_t i) {
            while (i < words.size() && words[i] == ",") ++i;
            return i;
        };
        // Numeric/range/list continuations may belong to a pending selection.
        // Preserve their punctuation without consulting or changing assembler state.
        auto first = std::size_t{0};
        if (words[first] == "and" || words[first] == ",") first = nextMeaningful(first + 1);
        if (first < words.size() && words[first] == canonicalArticle) first = nextMeaningful(first + 1);
        if (first < words.size() && (words[first] == "del" || words[first] == "from"))
            first = nextMeaningful(first + 1);
        if (first < words.size() && number(words[first])) {
            selection = true;
            preferRange = true;
            allowAl = true;
            expectTarget = false;
        }
        for (std::size_t i = 0; i < words.size(); ++i) {
            auto& word = words[i];
            if (word == ",") {
                // Terminal punctuation remains owned by normalization/parser.
                if (i + 1 == words.size()) continue;
                auto next = i + 1;
                if (words[next] == "and") ++next;
                if (next < words.size() && words[next] == canonicalArticle) ++next;
                const bool list = selection && (i == 0 || number(words[i - 1]))
                    && next < words.size() && number(words[next]);
                if (!list) {
                    removed[i] = true;
                    result.repairs.push_back({",", "", "punctuation", "neutral internal comma"});
                }
                continue;
            }
            if (expectTarget && word == canonicalArticle) continue;
            const auto folded = lower(tokens[i].text);
            const auto next = nextMeaningful(i + 1);
            auto selectionStart = next;
            if (selectionStart < words.size() && words[selectionStart] == canonicalArticle)
                selectionStart = nextMeaningful(selectionStart + 1);
            if (selectionStart < words.size()
                && (words[selectionStart] == "del" || words[selectionStart] == "from"))
                selectionStart = nextMeaningful(selectionStart + 1);
            if (expectTarget && selectionStart < words.size()
                && (number(words[selectionStart]) || words[selectionStart] == "all")) {
                bool known = false;
                for (const auto& alias : configuration_.vocabulary)
                    if (alias.spoken == folded) { known = true; break; }
                const auto match = known ? std::string_view{} : candidate(word);
                if (!match.empty()) {
                    const auto canonical = canonicalizeCommandText(match, configuration_.vocabulary);
                    if (canonical.size() == 1 && canonical.front() == match) {
                        // Only a full-length family with a plural suffix establishes plurality.
                        auto replacement = std::string(match);
                        if (word.size() > 3 && word.back() == 's') replacement += 's';
                        const auto replacementRole = canonicalizeCommandText(replacement, configuration_.vocabulary);
                        if (replacementRole.size() != 1 || replacementRole.front() != replacement) continue;
                        replacements[i] = replacement;
                        result.repairs.push_back({std::string(tokens[i].text), replacement,
                            "target", "unique root in target slot before selection"});
                        word = std::move(replacement);
                    }
                }
            }
            if (expectTarget && target(word)) {
                selection = true;
                preferRange = selectionAction || word.back() == 's';
                allowAl = selectionAction || ((words[0] == "on" || words[0] == "off"
                    || words[0] == "full") && word.back() == 's');
                expectTarget = false;
                continue;
            }
            if (action(word) && (i == 0 || (selection && (word == "add" || word == "remove"
                || word == canonicalScopedRemove)))) {
                expectTarget = true;
                selectionAction = word != "set" && word != "on" && word != "off" && word != "full";
                // Relative modifications and omitted-target prefix commands still select numbers.
                selection = word == "add" || word == "remove" || word == canonicalScopedRemove
                    || ((word == "on" || word == "off" || word == "full") && configuration_.defaultTarget);
                preferRange = selectionAction;
                allowAl = selectionAction;
                continue;
            }
            expectTarget = false;
            if (word == "del") allowAl = true;
            if (word == "at") selection = false;
            if ((word == "a" || word == "al") && selection) {
                const auto afterNumber = next < words.size() ? next + 1 : words.size();
                const bool followedByClause = afterNumber < words.size()
                    && (words[afterNumber] == "," || words[afterNumber] == "and"
                        || words[afterNumber] == "a" || words[afterNumber] == "al"
                        || words[afterNumber] == "at" || words[afterNumber] == "add"
                        || words[afterNumber] == "remove" || words[afterNumber] == canonicalScopedRemove);
                if (!(next < words.size() && number(words[next])
                    && ((word == "a" ? preferRange : allowAl) || followedByClause))) selection = false;
            }
            if (selection && !number(word) && word != "all" && word != "del" && word != "from"
                && word != "through" && word != "and" && word != "me"
                && word != canonicalArticle && word != "a" && word != "al") selection = false;
        }
        if (!result.applied()) return result;
        std::string repaired;
        std::size_t copied = 0;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            if (!removed[i] && replacements[i].empty()) continue;
            const auto& token = tokens[i];
            repaired.append(text.substr(copied, token.begin - copied));
            if (removed[i]) {
                // Replace attached commas with whitespace; avoid doubling existing spaces.
                if (!repaired.empty() && !std::isspace(static_cast<unsigned char>(repaired.back()))
                    && token.end < text.size() && !std::isspace(static_cast<unsigned char>(text[token.end])))
                    repaired += ' ';
            }
            else repaired += replacements[i];
            copied = token.end;
            if (removed[i] && (repaired.empty() || std::isspace(static_cast<unsigned char>(repaired.back()))))
                while (copied < text.size() && std::isspace(static_cast<unsigned char>(text[copied]))) ++copied;
        }
        repaired.append(text.substr(copied));
        result.repairedText = std::move(repaired);
        return result;
    }
}
