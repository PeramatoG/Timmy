/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <string_view>

namespace timmy::command {

    struct NormalizedTranscript
    {
        std::string originalText;
        std::string normalizedText;
    };

    struct LexicalNormalizationConfiguration
    {
        bool stripTerminalPunctuation{true};
    };

    class TranscriptNormalizer
    {
    public:
        explicit TranscriptNormalizer(LexicalNormalizationConfiguration configuration = {})
            : configuration_(configuration) {}
        [[nodiscard]]
        NormalizedTranscript normalize(std::string_view transcript) const;
    private:
        LexicalNormalizationConfiguration configuration_;
    };

} // namespace timmy::command
