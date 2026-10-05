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

#include <string_view>
#include <vector>

namespace timmy::asr::detail {

    namespace {

        constexpr const char* commandPhraseGrammar = R"GBNF(
root ::= " " phrase punctuation?
phrase ::= element (separator element)*
element ::= word | number percent? | percent
word ::= letter (letter | "-" | "'")*
number ::= [0-9]+
percent ::= "%"
separator ::= " " | ", "
punctuation ::= "." | "!" | "?"
letter ::= [A-Za-z] | [À-Ö] | [Ø-ö] | [ø-ÿ]
)GBNF";

        // Appends non-empty hint values with stable readable separators.
        void appendValues(
            std::string& prompt,
            std::string_view label,
            const std::vector<std::string>& values)
        {
            bool hasValue = false;
            for (const auto& value : values)
            {
                if (!value.empty())
                {
                    hasValue = true;
                    break;
                }
            }
            if (!hasValue)
            {
                return;
            }

            if (!prompt.empty())
            {
                prompt += ' ';
            }
            prompt += label;
            prompt += ": ";

            bool first = true;
            for (const auto& value : values)
            {
                if (value.empty())
                {
                    continue;
                }
                if (!first)
                {
                    prompt += ", ";
                }
                prompt += value;
                first = false;
            }
            prompt += '.';
        }

    } // namespace

    // Translates backend-neutral hints into Whisper prompt and grammar data.
    WhisperRecognitionGuidance makeWhisperRecognitionGuidance(
        const RecognitionHints& hints)
    {
        WhisperRecognitionGuidance guidance;
        appendValues(guidance.initialPrompt, "Vocabulary", hints.vocabulary);
        appendValues(
            guidance.initialPrompt,
            "Representative phrases",
            hints.expectedPhrases);

        if (hints.constraintIntent
            == RecognitionConstraintIntent::commandPhrase)
        {
            guidance.grammar = commandPhraseGrammar;
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
