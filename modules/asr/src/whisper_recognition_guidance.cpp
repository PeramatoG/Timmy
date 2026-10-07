/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_recognition_guidance.h"

#include "grammar-parser.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace timmy::asr::detail {

    namespace {

        // The upstream grammar parser assumes valid UTF-8. Reject malformed
        // encodings, empty entries and ASCII controls before quoting literals.
        bool isValidVocabularyEntry(std::string_view value)
        {
            if (value.empty() || value.front() == ' ' || value.back() == ' ')
            {
                return false;
            }
            for (std::size_t index = 0; index < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[index++]);
                if (first < 0x80U)
                {
                    if (first < 0x20U || first == 0x7fU)
                    {
                        return false;
                    }
                    continue;
                }

                std::size_t continuationCount;
                std::uint32_t codePoint;
                std::uint32_t minimum;
                if (first >= 0xc2U && first <= 0xdfU)
                {
                    continuationCount = 1;
                    codePoint = first & 0x1fU;
                    minimum = 0x80U;
                }
                else if (first >= 0xe0U && first <= 0xefU)
                {
                    continuationCount = 2;
                    codePoint = first & 0x0fU;
                    minimum = 0x800U;
                }
                else if (first >= 0xf0U && first <= 0xf4U)
                {
                    continuationCount = 3;
                    codePoint = first & 0x07U;
                    minimum = 0x10000U;
                }
                else
                {
                    return false;
                }
                if (continuationCount > value.size() - index)
                {
                    return false;
                }
                for (std::size_t count = 0; count < continuationCount; ++count)
                {
                    const auto next = static_cast<unsigned char>(value[index++]);
                    if ((next & 0xc0U) != 0x80U)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6U) | (next & 0x3fU);
                }
                if (codePoint < minimum || codePoint > 0x10ffffU
                    || (codePoint >= 0xd800U && codePoint <= 0xdfffU))
                {
                    return false;
                }
            }
            return true;
        }

        // Retains exact entries and their supplied order without empty branches.
        std::vector<std::string> validVocabulary(
            const std::vector<std::string>& values)
        {
            std::vector<std::string> vocabulary;
            for (const auto& value : values)
            {
                if (isValidVocabularyEntry(value)
                    && std::find(vocabulary.begin(), vocabulary.end(), value)
                        == vocabulary.end())
                {
                    vocabulary.push_back(value);
                }
            }
            return vocabulary;
        }

        // GBNF metacharacters are inert inside literals except quotes and backslashes.
        std::string grammarLiteral(std::string_view value)
        {
            std::string literal = "\"";
            for (const char byte : value)
            {
                if (byte == '"' || byte == '\\')
                {
                    literal += '\\';
                }
                literal += byte;
            }
            literal += '"';
            return literal;
        }

        // This constrains recognition tokens only; semantic parsing owns order,
        // target types, ranges and command validity. Whisper requires a leading space.
        std::string commandPhraseGrammar(const std::vector<std::string>& vocabulary)
        {
            std::string grammar =
                "root ::= \" \" phrase punctuation?\n"
                "phrase ::= element (separator element)*\n"
                "element ::= ";
            if (!vocabulary.empty())
            {
                grammar += "vocabulary | ";
            }
            grammar += R"GBNF(number percent? | percent
number ::= [0-9]+
percent ::= "%"
separator ::= " "+ | " "* "," " "*
punctuation ::= "." | "!" | "?"
)GBNF";
            if (!vocabulary.empty())
            {
                grammar += "vocabulary ::= ";
                for (std::size_t index = 0; index < vocabulary.size(); ++index)
                {
                    if (index != 0)
                    {
                        grammar += " | ";
                    }
                    grammar += grammarLiteral(vocabulary[index]);
                }
                grammar += '\n';
            }
            return grammar;
        }

        // Keep inference priming bounded as profiles grow; never truncate UTF-8
        // or include the profile's representative command examples.
        std::string vocabularyPrompt(const std::vector<std::string>& vocabulary)
        {
            constexpr std::size_t maximumBytes = 256;
            constexpr std::size_t maximumEntries = 16;
            constexpr std::string_view label = "Vocabulary: ";
            std::string prompt;
            std::size_t entryCount = 0;
            for (const auto& value : vocabulary)
            {
                const auto separator = prompt.empty() ? label : std::string_view{", "};
                const auto remaining = maximumBytes - prompt.size();
                if (separator.size() + 1 > remaining
                    || value.size() > remaining - separator.size() - 1)
                {
                    continue;
                }
                prompt += separator;
                prompt += value;
                if (++entryCount == maximumEntries)
                {
                    break;
                }
            }
            if (!prompt.empty())
            {
                prompt += '.';
            }
            return prompt;
        }

    } // namespace

    // Translates backend-neutral hints into Whisper prompt and grammar data.
    WhisperRecognitionGuidance makeWhisperRecognitionGuidance(
        const RecognitionHints& hints)
    {
        WhisperRecognitionGuidance guidance;
        const auto vocabulary = validVocabulary(hints.vocabulary);
        guidance.initialPrompt = vocabularyPrompt(vocabulary);

        if (hints.constraintIntent
            == RecognitionConstraintIntent::commandPhrase)
        {
            guidance.grammar = commandPhraseGrammar(vocabulary);
            guidance.startRule = "root";
            guidance.grammarPenalty = 20.0F;
        }
        return guidance;
    }

    // Checks that optional guidance contains a parseable named grammar rule.
    bool isValidWhisperRecognitionGuidance(
        const WhisperRecognitionGuidance& guidance)
    {
        if (guidance.grammar.empty() || guidance.startRule.empty())
        {
            return guidance.grammar.empty()
                && guidance.startRule.empty()
                && guidance.grammarPenalty >= 0.0F;
        }
        if (guidance.grammarPenalty < 0.0F)
        {
            return false;
        }

        const auto parsed = grammar_parser::parse(guidance.grammar.c_str());
        return !parsed.rules.empty()
            && parsed.symbol_ids.contains(guidance.startRule);
    }

} // namespace timmy::asr::detail
