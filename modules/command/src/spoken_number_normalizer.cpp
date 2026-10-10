/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "spoken_number_normalizer.h"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace timmy::command::detail {
    namespace {
        enum class Language { spanish, english, none };
        struct NumberWord { std::string_view text; int value; };
        constexpr NumberWord spanish[] = {
            {"cero", 0}, {"uno", 1}, {"dos", 2}, {"tres", 3}, {"cuatro", 4},
            {"cinco", 5}, {"seis", 6}, {"siete", 7}, {"ocho", 8}, {"nueve", 9},
            {"diez", 10}, {"once", 11}, {"doce", 12}, {"trece", 13}, {"catorce", 14},
            {"quince", 15}, {"dieciséis", 16}, {"dieciseis", 16}, {"diecisiete", 17},
            {"dieciocho", 18}, {"diecinueve", 19}, {"veinte", 20}, {"veintiuno", 21},
            {"veintidós", 22}, {"veintidos", 22}, {"veintitrés", 23}, {"veintitres", 23},
            {"veinticuatro", 24}, {"veinticinco", 25}, {"veintiséis", 26}, {"veintiseis", 26},
            {"veintisiete", 27}, {"veintiocho", 28}, {"veintinueve", 29},
            {"treinta", 30}, {"cuarenta", 40}, {"cincuenta", 50}, {"sesenta", 60},
            {"setenta", 70}, {"ochenta", 80}, {"noventa", 90},
            {"cien", 100}, {"ciento", 100}, {"doscientos", 200}, {"trescientos", 300},
            {"cuatrocientos", 400}, {"quinientos", 500}, {"seiscientos", 600},
            {"setecientos", 700}, {"ochocientos", 800}, {"novecientos", 900}
        };
        constexpr NumberWord english[] = {
            {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4},
            {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9},
            {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13}, {"fourteen", 14},
            {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18},
            {"nineteen", 19}, {"twenty", 20}, {"thirty", 30}, {"forty", 40},
            {"fifty", 50}, {"sixty", 60}, {"seventy", 70}, {"eighty", 80},
            {"ninety", 90}, {"hundred", 100}
        };
        // Recognize unsupported scales/decimal markers as part of a numeric run
        // so a future extension cannot silently normalize just their neighbors.
        constexpr std::string_view unsupported[] = {
            "mil", "millón", "millon", "millones", "thousand", "million", "billion",
            "punto", "coma", "point"
        };

        struct Token
        {
            std::size_t begin;
            std::size_t end;
            std::string word;
            Language language{Language::none};
            int value{-1};
            bool numeric{false};
        };

        bool isWordByte(unsigned char byte)
        {
            return byte >= 0x80U || (byte >= 'a' && byte <= 'z')
                || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')
                || byte == '_';
        }

        std::string lowerNumberWord(std::string_view word)
        {
            std::string result(word);
            for (std::size_t i = 0; i < result.size(); ++i)
            {
                const auto byte = static_cast<unsigned char>(result[i]);
                if (byte >= 'A' && byte <= 'Z') result[i] = static_cast<char>(byte + 32U);
                else if (byte == 0xc3U && i + 1 < result.size())
                {
                    const auto next = static_cast<unsigned char>(result[i + 1]);
                    if (next == 0x81U || next == 0x89U || next == 0x8dU
                        || next == 0x93U || next == 0x9aU)
                        result[++i] = static_cast<char>(next + 0x20U);
                }
            }
            return result;
        }

        std::vector<Token> tokenize(std::string_view text)
        {
            std::vector<Token> tokens;
            for (std::size_t i = 0; i < text.size();)
            {
                if (!isWordByte(static_cast<unsigned char>(text[i]))) { ++i; continue; }
                const auto begin = i++;
                // Preserve contractions/possessives as whole words, while
                // treating surrounding quotation marks as punctuation.
                while (i < text.size())
                {
                    if (isWordByte(static_cast<unsigned char>(text[i]))) { ++i; continue; }
                    if (text[i] == '\'' && i + 1 < text.size()
                        && isWordByte(static_cast<unsigned char>(text[i + 1]))) { ++i; continue; }
                    break;
                }
                Token token{begin, i, lowerNumberWord(text.substr(begin, i - begin))};
                for (const auto language : {Language::spanish, Language::english})
                {
                    const std::span<const NumberWord> vocabulary = language == Language::spanish
                        ? std::span<const NumberWord>{spanish} : std::span<const NumberWord>{english};
                    for (const auto& entry : vocabulary)
                        if (entry.text == token.word)
                        {
                            token.language = language;
                            token.value = entry.value;
                            token.numeric = true;
                            break;
                        }
                    if (token.numeric) break;
                }
                token.numeric = token.numeric
                    || std::all_of(token.word.begin(), token.word.end(), [](char c) { return c >= '0' && c <= '9'; })
                    || std::find(std::begin(unsupported), std::end(unsupported), token.word) != std::end(unsupported);
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        bool isConjunction(const Token& token) { return token.word == "y" || token.word == "and"; }

        std::optional<int> belowHundred(std::span<const Token> phrase, Language language)
        {
            if (phrase.empty()) return std::nullopt;
            const int first = phrase.front().value;
            if (first < 0 || first >= 100) return std::nullopt;
            if (phrase.size() == 1) return first;
            const bool tens = first >= (language == Language::spanish ? 30 : 20) && first % 10 == 0;
            const bool correctShape = language == Language::spanish
                ? phrase.size() == 3 && phrase[1].word == "y" : phrase.size() == 2;
            const int unit = phrase.back().value;
            if (tens && correctShape && unit > 0 && unit < 10) return first + unit;
            return std::nullopt;
        }

        // Strict, bounded composition rather than additive word accumulation.
        // Extend this hierarchy explicitly when larger identifiers are needed.
        std::optional<int> parseNumber(std::span<const Token> phrase)
        {
            const auto language = phrase.front().language;
            if (language == Language::none) return std::nullopt;
            for (const auto& token : phrase)
                if (!isConjunction(token) && token.language != language) return std::nullopt;

            if (language == Language::spanish && phrase.front().value >= 100)
            {
                const auto& head = phrase.front();
                if (head.word == "cien") return phrase.size() == 1 ? std::optional<int>{100} : std::nullopt;
                if (phrase.size() == 1) return head.word == "ciento" ? std::nullopt : std::optional<int>{head.value};
                const auto remainder = belowHundred(phrase.subspan(1), language);
                if (remainder && *remainder > 0) return head.value + *remainder;
                return std::nullopt;
            }
            if (language == Language::english && phrase.size() >= 2 && phrase[1].word == "hundred")
            {
                const int multiplier = phrase.front().value;
                if (multiplier < 1 || multiplier > 9) return std::nullopt;
                if (phrase.size() == 2) return multiplier * 100;
                const std::size_t suffix = phrase[2].word == "and" ? 3 : 2;
                const auto remainder = belowHundred(phrase.subspan(suffix), language);
                if (remainder && *remainder > 0) return multiplier * 100 + *remainder;
                return std::nullopt;
            }
            return belowHundred(phrase, language);
        }

        bool belongsToNumber(const Token& previous, const Token& conjunction)
        {
            return (conjunction.word == "y" && previous.language == Language::spanish
                    && previous.value >= 30 && previous.value < 100 && previous.value % 10 == 0)
                || (conjunction.word == "and" && previous.word == "hundred");
        }
    }

    std::string normalizeSpokenNumbers(std::string_view text)
    {
        const auto tokens = tokenize(text);
        std::string output;
        output.reserve(text.size());
        std::size_t copied = 0;
        for (std::size_t i = 0; i < tokens.size();)
        {
            if (!tokens[i].numeric) { ++i; continue; }
            const auto begin = i++;
            // Keep adjacent numeric words/digits together. An invalid run is
            // retained whole, never salvaged as several invented identifiers.
            while (i < tokens.size() && text.substr(tokens[i - 1].end, tokens[i].begin - tokens[i - 1].end) == " ")
            {
                if (isConjunction(tokens[i]) && !belongsToNumber(tokens[i - 1], tokens[i])) break;
                if (!tokens[i].numeric && !isConjunction(tokens[i])) break;
                ++i;
            }
            const auto value = parseNumber(std::span<const Token>{tokens}.subspan(begin, i - begin));
            if (!value) continue;
            output.append(text.substr(copied, tokens[begin].begin - copied));
            output.append(std::to_string(*value));
            copied = tokens[i - 1].end;
        }
        output.append(text.substr(copied));
        return output;
    }
}
