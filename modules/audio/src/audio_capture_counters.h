/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include "timmy/audio/audio_capture_stats.h"
#include <atomic>

namespace timmy::audio::detail {
    // Single capture writer, single dispatch reader. Reset only with both stopped.
    // All callback operations are bounded, lock-free loads/stores/fetch_add.
    class AudioCaptureCounters final : public IAudioCaptureStatsSource
    {
    public:
        void reset(std::uint32_t rate, std::uint64_t capacity) noexcept
        {
            available_.store(false);
            ++session_; // Odd while reset is in progress.
            rate_.store(rate); capacity_.store(capacity);
            captured_.store(0); dropped_.store(0); occupancy_.store(0); highWater_.store(0);
            ++session_; // Even identifies a complete capture session.
            available_.store(true);
        }
        void captured(std::uint64_t frames) noexcept { captured_.fetch_add(frames, std::memory_order_relaxed); }
        void dropped(std::uint64_t frames) noexcept { dropped_.fetch_add(frames, std::memory_order_relaxed); }
        void observeWrite(std::uint64_t occupancy) noexcept
        {
            observeRead(occupancy);
            // Only the capture writer changes highWater_ while running.
            if (occupancy > highWater_.load(std::memory_order_relaxed))
                highWater_.store(occupancy, std::memory_order_relaxed);
        }
        void observeRead(std::uint64_t occupancy) noexcept { occupancy_.store(occupancy, std::memory_order_relaxed); }
        std::optional<AudioCaptureStats> captureStats() const noexcept override
        {
            if (!available_.load(std::memory_order_acquire)) return {};
            const auto session = session_.load();
            if (session % 2 != 0) return {};
            const auto stats = AudioCaptureStats{session, rate_.load(), captured_.load(), dropped_.load(),
                occupancy_.load(), highWater_.load(), capacity_.load()};
            // Do not splice two sessions when sampling concurrently with restart.
            if (session_.load() != session || !available_.load(std::memory_order_acquire)) return {};
            return stats;
        }
    private:
        static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
        static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
        std::atomic<bool> available_{false};
        std::atomic<std::uint32_t> rate_{0};
        std::atomic<std::uint64_t> session_{0}, capacity_{0}, captured_{0}, dropped_{0}, occupancy_{0}, highWater_{0};
    };
}
