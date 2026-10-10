/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <cstdint>
#include <optional>

namespace timmy::audio {
    // Frames are device frames, independent of channel count and ASR resampling.
    // Counts/high-water span one capture session; occupancy is a sampled gauge.
    struct AudioCaptureStats
    {
        std::uint64_t session = 0;
        std::uint32_t sampleRate = 0;
        std::uint64_t capturedFrames = 0; // Includes frames lost before dispatch.
        std::uint64_t droppedCaptureFrames = 0;
        std::uint64_t ringOccupancyFrames = 0;
        std::uint64_t ringHighWaterFrames = 0; // Producer-sampled maximum occupancy.
        std::uint64_t ringCapacityFrames = 0;
    };
    class IAudioCaptureStatsSource
    {
    public:
        virtual ~IAudioCaptureStatsSource() = default;
        // nullopt means capture accounting is unavailable (never a fabricated zero).
        virtual std::optional<AudioCaptureStats> captureStats() const noexcept = 0;
    };
}
