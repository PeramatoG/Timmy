/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <utility>

namespace timmy::asr::detail {
    // Externally synchronized. Preserves the live backend's bounded drop-oldest policy.
    template<class T> class DiagnosticQueue
    {
    public:
        using Clock = std::chrono::steady_clock;
        struct Entry { T value; Clock::time_point queuedAt; bool finalized; };
        explicit DiagnosticQueue(std::size_t capacity) : capacity_(capacity) {}
        void push(T value, Clock::time_point now = Clock::now(), bool finalized = false)
        {
            if (entries_.size() == capacity_) { entries_.pop_front(); ++dropped_; }
            entries_.push_back({std::move(value), now, finalized});
            maximum_ = std::max(maximum_, entries_.size());
        }
        Entry pop(Clock::time_point now = Clock::now())
        {
            auto entry = std::move(entries_.front());
            entries_.pop_front();
            const auto wait = std::chrono::duration<double>(now - entry.queuedAt).count();
            waitTotal_ += wait; waitMax_ = std::max(waitMax_, wait);
            return entry;
        }
        bool empty() const { return entries_.empty(); }
        std::size_t size() const { return entries_.size(); }
        std::size_t maximum() const { return maximum_; }
        std::uint64_t dropped() const { return dropped_; }
        double waitTotal() const { return waitTotal_; }
        double waitMax() const { return waitMax_; }
        std::optional<double> oldestAge(Clock::time_point now = Clock::now()) const
        {
            if (empty()) return {};
            return std::chrono::duration<double>(now - entries_.front().queuedAt).count();
        }
        void clear() { entries_.clear(); }
        void reset() { clear(); maximum_ = 0; dropped_ = 0; waitTotal_ = waitMax_ = 0; }
    private:
        const std::size_t capacity_;
        std::deque<Entry> entries_;
        std::size_t maximum_ = 0;
        std::uint64_t dropped_ = 0;
        double waitTotal_ = 0, waitMax_ = 0;
    };
}
