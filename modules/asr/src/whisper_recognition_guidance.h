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

#include "timmy/asr/asr_types.h"

#include <string>

namespace timmy::asr::detail {

    struct WhisperRecognitionGuidance
    {
        std::string initialPrompt;
        std::string grammar;
        std::string startRule;
        float grammarPenalty = 0.0F;
    };

    [[nodiscard]] WhisperRecognitionGuidance
        makeWhisperRecognitionGuidance(const RecognitionHints& hints);
    [[nodiscard]] bool isValidWhisperRecognitionGuidance(
        const WhisperRecognitionGuidance& guidance);

} // namespace timmy::asr::detail
