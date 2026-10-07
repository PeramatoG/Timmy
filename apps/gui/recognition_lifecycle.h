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
#include <cstdint>
#include <exception>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace timmy::gui {

    // Lifecycle calls belong to one frontend thread. Shutdown work may touch
    // only resources kept alive until poll() completes or this owner is joined.
    class RecognitionLifecycle final
    {
    public:
        RecognitionLifecycle() = default;
        ~RecognitionLifecycle()
        {
            if (worker_.joinable()) worker_.join();
        }

        RecognitionLifecycle(const RecognitionLifecycle&) = delete;
        RecognitionLifecycle& operator=(const RecognitionLifecycle&) = delete;

        bool beginListening()
        {
            if (!canStart()) return false;
            ++generation_;
            phase_ = Phase::listening;
            diagnostic_.clear();
            return true;
        }

        // Also accepts idle cleanup, such as stopping audio before window close.
        bool beginStop(std::function<void()> shutdown)
        {
            if (stopping()) return false;
            const auto previousPhase = phase_;
            const auto previousGeneration = generation_;
            phase_ = Phase::stopping;
            ++generation_;
            completed_.store(false, std::memory_order_relaxed);
            failure_ = nullptr;
            diagnostic_.clear();

            try
            {
                worker_ = std::thread([this, shutdown = std::move(shutdown)]() mutable {
                    try
                    {
                        if (shutdown) shutdown();
                    }
                    catch (...)
                    {
                        failure_ = std::current_exception();
                    }
                    // Release task ownership before advertising completion; a
                    // captured resource destructor can perform more cleanup.
                    shutdown = {};
                    completed_.store(true, std::memory_order_release);
                });
            }
            catch (...)
            {
                // No worker was launched, so the previous session remains valid.
                phase_ = previousPhase;
                generation_ = previousGeneration;
                diagnostic_ = exceptionMessage(std::current_exception());
                return false;
            }
            return true;
        }

        // The acquire observes all shutdown work; join cannot wait for that work.
        bool poll()
        {
            if (!stopping() || !completed_.load(std::memory_order_acquire)) return false;
            worker_.join();
            phase_ = Phase::stopped;
            if (failure_) diagnostic_ = exceptionMessage(failure_);
            failure_ = nullptr;
            return true;
        }

        // Final ownership cleanup before the frontend releases worker resources.
        void join()
        {
            if (worker_.joinable()) worker_.join();
            if (stopping())
            {
                phase_ = Phase::stopped;
                if (failure_) diagnostic_ = exceptionMessage(failure_);
                failure_ = nullptr;
            }
        }

        [[nodiscard]] std::uint64_t generation() const { return generation_; }
        [[nodiscard]] bool accepts(std::uint64_t generation) const
        {
            return listening() && generation == generation_;
        }
        [[nodiscard]] bool listening() const { return phase_ == Phase::listening; }
        [[nodiscard]] bool stopping() const { return phase_ == Phase::stopping; }
        [[nodiscard]] bool canConfigure() const { return phase_ == Phase::stopped; }
        [[nodiscard]] bool canStart() const { return phase_ == Phase::stopped; }
        [[nodiscard]] const std::string& diagnostic() const { return diagnostic_; }

    private:
        enum class Phase { stopped, listening, stopping };

        static std::string exceptionMessage(const std::exception_ptr& failure)
        {
            try
            {
                std::rethrow_exception(failure);
            }
            catch (const std::exception& error)
            {
                return error.what();
            }
            catch (...)
            {
                return "Unknown recognition lifecycle failure.";
            }
        }

        Phase phase_ = Phase::stopped;
        std::uint64_t generation_ = 0;
        std::thread worker_;
        std::atomic<bool> completed_{false};
        std::exception_ptr failure_;
        std::string diagnostic_;
    };

} // namespace timmy::gui
