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

#include <cstdint>

namespace timmy::audio {

    struct AudioStreamFormat
    {
        std::uint32_t sampleRate = 0;
        std::uint32_t channelCount = 0;
    };

} // namespace timmy::audio
