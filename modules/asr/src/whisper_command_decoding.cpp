/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_command_decoding.h"

#include <algorithm>

namespace timmy::asr::detail {

    // Builds the command-oriented decoding profile for available CPU cores.
    WhisperCommandDecodingProfile makeWhisperCommandDecodingProfile(
        unsigned int hardwareConcurrency)
    {
        constexpr unsigned int maximumThreadCount = 4;
        const unsigned int threadCount = std::max(
            1U, std::min(maximumThreadCount, hardwareConcurrency));

        return {
            true,
            true,
            true,
            static_cast<int>(threadCount),
            32,
            0,
            5,
            0.4F,
            1.0F,
            5
        };
    }

} // namespace timmy::asr::detail
