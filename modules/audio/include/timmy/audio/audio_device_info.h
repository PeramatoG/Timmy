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
#include <string>

namespace timmy::audio {

    struct AudioDeviceInfo
    {
        // No value means the backend identity has not yet been mapped.
        std::optional<AudioDeviceId> id;

        // This index is ephemeral and valid only for the associated enumeration.
        std::size_t runtimeIndex = 0;
        std::string name;
        bool isDefault = false;
    };

} // namespace timmy::audio
