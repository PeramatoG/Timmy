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

#include "timmy/audio/audio_device_id.h"

#include <cstddef>
#include <optional>

namespace timmy::audio {

    struct AudioCaptureConfig
    {
        // Preferred backend-scoped selector; no value means no persistent selector.
        std::optional<AudioDeviceId> deviceId;

        // Compatibility selector used only when deviceId is absent. With neither
        // selector set, capture uses the current system default device. A runtime
        // index is ephemeral and must come from a recent device enumeration.
        std::optional<std::size_t> runtimeDeviceIndex;
    };

} // namespace timmy::audio
