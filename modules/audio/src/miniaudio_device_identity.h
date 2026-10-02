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

#include <miniaudio.h>

#include <optional>

namespace timmy::audio::detail {

    // Converts a supported miniaudio device identity to Timmy's canonical form.
    [[nodiscard]]
    std::optional<AudioDeviceId> makeAudioDeviceId(
        ma_backend backend,
        const ma_device_id& nativeId);

    // Finds a native device by an exact canonical backend and value match.
    [[nodiscard]]
    const ma_device_id* findCaptureDeviceId(
        ma_backend backend,
        const ma_device_info* devices,
        ma_uint32 deviceCount,
        const AudioDeviceId& requestedId);

} // namespace timmy::audio::detail
