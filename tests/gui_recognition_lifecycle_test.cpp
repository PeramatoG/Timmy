/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "recognition_lifecycle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

namespace {

    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        std::cerr << "Line " << line << ": " << expression << '\n';
        std::abort();
    }

#define REQUIRE(expression) require((expression), #expression, __LINE__)

    class BlockedShutdown final
    {
    public:
        void run()
        {
            std::unique_lock lock(mutex_);
            thread_ = std::this_thread::get_id();
            entered_ = true;
            changed_.notify_all();
            REQUIRE(changed_.wait_for(lock, 5s, [this] { return released_; }));
            completed_ = true;
            changed_.notify_all();
        }

        void waitUntilEntered()
        {
            std::unique_lock lock(mutex_);
            REQUIRE(changed_.wait_for(lock, 5s, [this] { return entered_; }));
        }

        void release()
        {
            std::lock_guard lock(mutex_);
            released_ = true;
            changed_.notify_all();
        }

        bool completed()
        {
            std::lock_guard lock(mutex_);
            return completed_;
        }

        std::thread::id thread()
        {
            std::lock_guard lock(mutex_);
            return thread_;
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        bool entered_ = false;
        bool released_ = false;
        bool completed_ = false;
        std::thread::id thread_;
    };

    void waitUntilStopped(timmy::gui::RecognitionLifecycle& lifecycle)
    {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!lifecycle.poll())
        {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        REQUIRE(!lifecycle.stopping());
        REQUIRE(!lifecycle.poll());
    }

    void responsiveStopAndRestart()
    {
        timmy::gui::RecognitionLifecycle lifecycle;
        REQUIRE(lifecycle.canConfigure() && lifecycle.canStart());
        REQUIRE(!lifecycle.accepts(lifecycle.generation()));

        for (int cycle = 0; cycle < 3; ++cycle)
        {
            const auto previousGeneration = lifecycle.generation();
            REQUIRE(lifecycle.beginListening());
            const auto listeningGeneration = lifecycle.generation();
            REQUIRE(listeningGeneration > previousGeneration);
            REQUIRE(lifecycle.listening() && lifecycle.accepts(listeningGeneration));
            REQUIRE(!lifecycle.accepts(previousGeneration));
            REQUIRE(!lifecycle.canConfigure() && !lifecycle.canStart());
            REQUIRE(!lifecycle.beginListening());

            BlockedShutdown shutdown;
            const auto frontendThread = std::this_thread::get_id();
            REQUIRE(lifecycle.beginStop([&] { shutdown.run(); }));
            shutdown.waitUntilEntered();
            REQUIRE(shutdown.thread() != frontendThread);
            REQUIRE(lifecycle.stopping() && !lifecycle.listening());
            REQUIRE(lifecycle.generation() > listeningGeneration);
            REQUIRE(!lifecycle.accepts(listeningGeneration));
            REQUIRE(!lifecycle.accepts(lifecycle.generation()));
            REQUIRE(!lifecycle.canStart() && !lifecycle.canConfigure());
            REQUIRE(!lifecycle.beginListening());
            REQUIRE(!lifecycle.beginStop([] { REQUIRE(false); }));

            // The frontend can keep processing events while shutdown is blocked.
            for (int tick = 0; tick < 50; ++tick) REQUIRE(!lifecycle.poll());
            REQUIRE(!shutdown.completed());
            shutdown.release();
            waitUntilStopped(lifecycle);
            REQUIRE(shutdown.completed());
            REQUIRE(lifecycle.canStart() && lifecycle.canConfigure());
            REQUIRE(lifecycle.diagnostic().empty());
        }
    }

    void idleWindowCleanup()
    {
        timmy::gui::RecognitionLifecycle lifecycle;
        BlockedShutdown audioShutdown;
        REQUIRE(lifecycle.beginStop([&] { audioShutdown.run(); }));
        audioShutdown.waitUntilEntered();
        REQUIRE(lifecycle.stopping() && !lifecycle.canStart());
        REQUIRE(!lifecycle.poll());
        audioShutdown.release();
        waitUntilStopped(lifecycle);
        REQUIRE(lifecycle.canConfigure());
    }

    void safeDestruction()
    {
        auto lifecycle = std::make_unique<timmy::gui::RecognitionLifecycle>();
        REQUIRE(lifecycle->beginListening());
        BlockedShutdown shutdown;
        auto ownership = std::make_shared<int>(0);
        const std::weak_ptr<int> workerOwnership = ownership;
        REQUIRE(lifecycle->beginStop([&, ownership = std::move(ownership)] {
            REQUIRE(ownership != nullptr);
            shutdown.run();
        }));
        shutdown.waitUntilEntered();

        std::atomic<bool> destroyed{false};
        std::thread destroyer([lifecycle = std::move(lifecycle), &destroyed]() mutable {
            lifecycle.reset();
            destroyed.store(true, std::memory_order_release);
        });
        REQUIRE(!destroyed.load(std::memory_order_acquire));
        REQUIRE(!workerOwnership.expired());
        shutdown.release();
        destroyer.join();
        REQUIRE(destroyed.load(std::memory_order_acquire));
        REQUIRE(shutdown.completed() && workerOwnership.expired());
    }

    void shutdownFailuresAreContained()
    {
        timmy::gui::RecognitionLifecycle lifecycle;
        REQUIRE(lifecycle.beginListening());
        REQUIRE(lifecycle.beginStop([] { throw std::runtime_error("Shutdown failed."); }));
        waitUntilStopped(lifecycle);
        REQUIRE(lifecycle.diagnostic() == "Shutdown failed.");
        REQUIRE(lifecycle.canConfigure() && lifecycle.canStart());

        REQUIRE(lifecycle.beginListening());
        REQUIRE(lifecycle.diagnostic().empty());
        REQUIRE(lifecycle.beginStop([] { throw 1; }));
        waitUntilStopped(lifecycle);
        REQUIRE(lifecycle.diagnostic() == "Unknown recognition lifecycle failure.");

        REQUIRE(lifecycle.beginStop([] {}));
        waitUntilStopped(lifecycle);
        REQUIRE(lifecycle.diagnostic().empty());
    }

    void captureCleanupCompletesBeforePollCanJoin()
    {
        struct OwnedCleanup
        {
            explicit OwnedCleanup(BlockedShutdown& cleanup) : cleanup_(cleanup) {}
            ~OwnedCleanup() { cleanup_.run(); }
            BlockedShutdown& cleanup_;
        };

        timmy::gui::RecognitionLifecycle lifecycle;
        BlockedShutdown cleanup;
        auto ownership = std::make_shared<OwnedCleanup>(cleanup);
        REQUIRE(lifecycle.beginStop([ownership = std::move(ownership)] {
            REQUIRE(ownership != nullptr);
        }));
        cleanup.waitUntilEntered();
        REQUIRE(!lifecycle.poll());
        cleanup.release();
        waitUntilStopped(lifecycle);
        REQUIRE(cleanup.completed());
    }

} // namespace

int main()
{
    responsiveStopAndRestart();
    idleWindowCleanup();
    safeDestruction();
    shutdownFailuresAreContained();
    captureCleanupCompletesBeforePollCanJoin();
}
