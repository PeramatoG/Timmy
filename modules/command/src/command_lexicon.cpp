/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_lexicon.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace timmy::command {

    namespace {

        struct Alias
        {
            std::string_view spoken;
            std::string_view canonical;
        };

        // Vocabulary is language-level only. Console keys and transport
        // addresses belong to the selected transport profile.
        constexpr Alias aliases[] = {
            {"canal", "channel"}, {"canales", "channels"},
            {"channel", "channel"}, {"channels", "channels"},
            {"fixture", "fixture"}, {"fixtures", "fixtures"},
            {"grupo", "group"}, {"grupos", "groups"},
            {"group", "group"}, {"groups", "groups"},
            {"selecciona", "select"}, {"dame", "select"}, {"coge", "select"},
            {"toma", "select"}, {"marca", "select"}, {"select", "select"},
            {"añade", "add"}, {"anade", "add"}, {"agrega", "add"},
            {"mete", "add"}, {"incluye", "add"}, {"suma", "add"},
            {"súmale", "add"}, {"sumale", "add"}, {"add", "add"},
            {"plus", "add"}, {"include", "add"},
            {"quita", canonicalScopedRemove}, {"quitas", "remove"}, {"saca", "remove"},
            {"resta", "remove"}, {"excluye", "remove"}, {"menos", "remove"},
            {"excepto", "remove"}, {"salvo", "remove"}, {"remove", "remove"},
            {"subtract", "remove"}, {"exclude", "remove"}, {"except", "remove"},
            {"but not", "remove"},
            {"limpia", "clear"}, {"borra", "clear"}, {"vacía", "clear"},
            {"vacia", "clear"}, {"clear", "clear"}, {"cler", "clear"},
            {"libera", "release"}, {"suelta", "release"}, {"release", "release"},
            {"selección", "selection"}, {"seleccion", "selection"},
            {"selection", "selection"}, {"todo", "all"}, {"all", "all"},
            {"editor", "editor"},
            {"el", canonicalArticle}, {"la", canonicalArticle}, {"los", canonicalArticle},
            {"las", canonicalArticle}, {"the", canonicalArticle}, {"me", "me"},
            {"y", "and"}, {"and", "and"},
            {"a", "a"}, {"al", "al"}, {"at", "at"},
            {"del", "del"}, {"from", "from"}, {"hasta", "through"},
            {"thru", "through"}, {"through", "through"}, {"to", "through"},
            {"pon", "set"}, {"sube", "set"}, {"baja", "set"}, {"set", "set"},
            {"prueba", "test"}, {"comprueba", "test"}, {"test", "test"},
            {"home", "home"}, {"lleva", canonicalHomeTo},
            {"siguiente", "next"}, {"next", "next"},
            {"anterior", "previous"}, {"prev", "previous"}, {"previous", "previous"},
            {"full", "full"}, {"tope", "full"}
        };

        std::string lowerKnownCase(std::string_view text)
        {
            std::string lowered(text);
            for (std::size_t index = 0; index < lowered.size(); ++index)
            {
                const auto byte = static_cast<unsigned char>(lowered[index]);
                if (byte < 0x80U)
                {
                    lowered[index] = static_cast<char>(std::tolower(byte));
                }
                else if (byte == 0xc3U && index + 1 < lowered.size())
                {
                    // Explicit UTF-8 Spanish uppercase letters, not general
                    // Unicode folding or removal of arbitrary diacritics.
                    const auto next = static_cast<unsigned char>(lowered[index + 1]);
                    if (next == 0x81U || next == 0x89U || next == 0x8dU
                        || next == 0x93U || next == 0x9aU || next == 0x91U
                        || next == 0x9cU)
                        lowered[++index] = static_cast<char>(next + 0x20U);
                }
            }
            return lowered;
        }

        std::vector<std::string> splitWords(std::string_view text)
        {
            std::vector<std::string> words;
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
                words.emplace_back(text.substr(start, index - start));
            }
            return words;
        }

    } // namespace

    std::vector<std::string> canonicalizeCommandText(std::string_view text)
    {
        for (const char value : text)
        {
            const auto byte = static_cast<unsigned char>(value);
            if ((byte < 0x20U && std::isspace(byte) == 0) || byte == 0x7fU) return {};
        }
        const auto words = splitWords(lowerKnownCase(text));
        std::vector<std::string> canonical;
        canonical.reserve(words.size());
        for (std::size_t index = 0; index < words.size(); ++index)
        {
            // Longest explicit phrase aliases precede individual word aliases.
            // Currently the only two-word alias is the exclusion "but not".
            const auto phrase = index + 1 < words.size()
                ? words[index] + " " + words[index + 1] : std::string{};
            const auto alias = std::find_if(std::begin(aliases), std::end(aliases),
                [&](const Alias& candidate) { return candidate.spoken == phrase; });
            if (!phrase.empty() && alias != std::end(aliases))
            {
                canonical.emplace_back(alias->canonical);
                ++index;
                continue;
            }
            const auto wordAlias = std::find_if(std::begin(aliases), std::end(aliases),
                [&](const Alias& candidate) { return candidate.spoken == words[index]; });
            canonical.emplace_back(wordAlias == std::end(aliases)
                ? std::string_view(words[index]) : wordAlias->canonical);
        }
        return canonical;
    }

    const std::vector<std::string>& spokenCommandVocabulary()
    {
        static const std::vector<std::string> vocabulary = [] {
            std::vector<std::string> result;
            result.reserve(std::size(aliases));
            for (const auto& alias : aliases) result.emplace_back(alias.spoken);
            return result;
        }();
        return vocabulary;
    }

} // namespace timmy::command
