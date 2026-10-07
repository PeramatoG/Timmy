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

#include <atomic>
#include <utility>

namespace timmy::asr::detail {

    enum class WhisperInferenceOutcome
    {
        completed,
        failed,
        cancelled
    };

    // Owns the stop request shared by the lifecycle and inference workers.
    class WhisperInferenceCancellation
    {
    public:
        // Resets cancellation only after the previous workers have joined.
        void reset() noexcept
        {
            stopRequested_.store(false);
            abortAcknowledged_.store(false);
        }

        // Allows Whisper to abort its current computation cooperatively.
        void requestStop() noexcept
        {
            stopRequested_.store(true);
        }

        [[nodiscard]] bool stopRequested() const noexcept
        {
            return stopRequested_.load();
        }

        // Matches whisper.cpp's abort callback without exposing its types.
        static bool abortCallback(void* userData) noexcept
        {
            auto& cancellation =
                *static_cast<WhisperInferenceCancellation*>(userData);
            if (!cancellation.stopRequested()) return false;
            cancellation.abortAcknowledged_.store(true);
            return true;
        }

        // Distinguishes intentional shutdown from a recognition failure.
        template<typename Inference>
        [[nodiscard]] WhisperInferenceOutcome run(Inference&& inference) const
        {
            if (stopRequested())
            {
                return WhisperInferenceOutcome::cancelled;
            }

            const int result = std::forward<Inference>(inference)();
            if (result != 0)
            {
                // A stop request alone must not hide an unrelated error that
                // returned before Whisper actually observed its abort callback.
                return abortAcknowledged_.load()
                    ? WhisperInferenceOutcome::cancelled
                    : WhisperInferenceOutcome::failed;
            }
            if (stopRequested())
            {
                return WhisperInferenceOutcome::cancelled;
            }
            return WhisperInferenceOutcome::completed;
        }

    private:
        std::atomic<bool> stopRequested_{ false };
        std::atomic<bool> abortAcknowledged_{ false };
    };

} // namespace timmy::asr::detail
