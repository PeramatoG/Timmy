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

namespace timmy::audio {

    // Identifies a device using an opaque value issued by a named backend.
    struct AudioDeviceId
    {
        std::string backend;
        std::string value;

        [[nodiscard]]
        bool empty() const noexcept
        {
            return backend.empty() || value.empty();
        }

        friend bool operator==(
            const AudioDeviceId&,
            const AudioDeviceId&) = default;
    };

} // namespace timmy::audio
