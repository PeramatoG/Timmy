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

#include <cstddef>
#include <optional>

namespace timmy::audio {

    struct AudioCaptureConfig
    {
        // No value means: use the current system default capture device.
        //
        // A runtime device index is intentionally ephemeral and must come
        // from a recent enumerateCaptureDevices() call.
        std::optional<std::size_t> runtimeDeviceIndex;
    };

} // namespace timmy::audio
