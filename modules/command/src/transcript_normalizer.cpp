/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/transcript_normalizer.h"

#include <cctype>
#include <string>
#include <string_view>

namespace timmy::command {

    namespace {

        // Detects ASCII whitespace without interpreting UTF-8 continuation bytes.
        bool isAsciiWhitespace(char value)
        {
            const unsigned char byte = static_cast<unsigned char>(value);
            return byte < 0x80U && std::isspace(byte) != 0;
        }

        // Reports whether a byte is an ASCII decimal digit.
        bool isAsciiDigit(char value)
        {
            return value >= '0' && value <= '9';
        }

        // Reports whether punctuation can safely terminate a spoken command.
        bool isTerminalPunctuation(char value)
        {
            return value == '.' || value == ',' || value == '!'
                || value == '?' || value == ';' || value == ':';
        }

        // Trims and collapses ASCII whitespace while preserving all UTF-8 bytes.
        std::string normalizeWhitespace(std::string_view input)
        {
            std::string output;
            output.reserve(input.size());
            bool pendingSpace = false;

            for (const char value : input)
            {
                if (isAsciiWhitespace(value))
                {
                    pendingSpace = !output.empty();
                    continue;
                }

                if (pendingSpace)
                {
                    output.push_back(' ');
                    pendingSpace = false;
                }
                output.push_back(value);
            }

            return output;
        }

        // Removes spaces before ordinary sentence punctuation.
        std::string normalizePunctuationSpacing(std::string_view input)
        {
            std::string output;
            output.reserve(input.size());

            for (const char value : input)
            {
                if (isTerminalPunctuation(value)
                    && !output.empty()
                    && output.back() == ' ')
                {
                    output.pop_back();
                }
                output.push_back(value);
            }

            return output;
        }

        // Joins a numeric token to a standalone following percent sign.
        std::string normalizePercentSpacing(std::string_view input)
        {
            std::string output;
            output.reserve(input.size());

            for (std::size_t index = 0; index < input.size(); ++index)
            {
                const char value = input[index];
                const bool percentHasRightBoundary = index + 1 == input.size()
                    || isAsciiWhitespace(input[index + 1])
                    || isTerminalPunctuation(input[index + 1]);

                if (value == '%' && percentHasRightBoundary
                    && output.size() >= 2
                    && output.back() == ' '
                    && isAsciiDigit(output[output.size() - 2]))
                {
                    output.pop_back();
                }
                output.push_back(value);
            }

            return output;
        }

        // Removes sentence punctuation emitted at the end of a transcript.
        void removeTerminalPunctuation(std::string& text)
        {
            while (!text.empty() && isTerminalPunctuation(text.back()))
            {
                text.pop_back();
            }
        }

    } // namespace

    // Applies the deterministic transcript rules in their defined order.
    NormalizedTranscript TranscriptNormalizer::normalize(
        std::string_view transcript) const
    {
        NormalizedTranscript result;
        result.originalText = transcript;
        result.normalizedText = normalizeWhitespace(transcript);
        result.normalizedText = normalizePunctuationSpacing(result.normalizedText);
        result.normalizedText = normalizePercentSpacing(result.normalizedText);
        removeTerminalPunctuation(result.normalizedText);
        return result;
    }

} // namespace timmy::command
