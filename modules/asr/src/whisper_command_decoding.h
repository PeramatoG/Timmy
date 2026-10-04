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

namespace timmy::asr::detail {

    struct WhisperCommandDecodingProfile
    {
        bool noContext;
        bool noTimestamps;
        bool singleSegment;
        int threadCount;
        int maxTokens;
        int audioContext;
        int beamSize;
        float temperature;
        float temperatureIncrement;
        int greedyBestOf;
    };

    [[nodiscard]] WhisperCommandDecodingProfile
        makeWhisperCommandDecodingProfile(unsigned int hardwareConcurrency);

} // namespace timmy::asr::detail
