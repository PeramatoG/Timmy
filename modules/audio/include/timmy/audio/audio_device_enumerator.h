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

#include "timmy/audio/audio_device_info.h"

#include <string>
#include <vector>

namespace timmy::audio {

    struct AudioDeviceEnumerationResult
    {
        std::vector<AudioDeviceInfo> devices;
        std::string errorMessage;

        [[nodiscard]] bool ok() const noexcept
        {
            return errorMessage.empty();
        }
    };

    [[nodiscard]]
    AudioDeviceEnumerationResult enumerateCaptureDevices();

} // namespace timmy::audio
