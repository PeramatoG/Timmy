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

#include "timmy/core/command_dispatcher.h"
#include "timmy/core/recognition_pipeline.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace timmy::core {

    enum class RecognitionExecutionState { ignored, queued, succeeded, failed };
    struct RecognitionExecutionResult
    {
        RecognitionExecutionState state{RecognitionExecutionState::ignored};
        std::string diagnostic;
    };

    // One submission represents one semantic event. Identical submissions remain
    // independent commands. The dispatcher must be exclusively owned by this worker.
    class RecognitionCommandExecutor final
    {
    public:
        // Shared frontend default: at most four commands wait behind the one
        // currently executing. A full queue rejects new commands without retry.
        static constexpr std::size_t defaultQueueCapacity = 4;
        using Observer = std::function<void(const RecognitionExecutionResult&)>;
        // Observer runs on the execution worker, outside the queue lock. It must
        // not call shutdown or destroy the executor; exceptions are contained.
        RecognitionCommandExecutor(CommandDispatcher dispatcher,
            Observer observer = {}, std::size_t capacity = defaultQueueCapacity);
        ~RecognitionCommandExecutor();
        RecognitionCommandExecutor(const RecognitionCommandExecutor&) = delete;
        RecognitionCommandExecutor& operator=(const RecognitionCommandExecutor&) = delete;

        [[nodiscard]] RecognitionExecutionResult submit(const RecognitionTranscript& result);
        // Close acceptance and discard waiting commands without joining or
        // interrupting an in-flight emission. The idle worker remains reusable.
        void pause();
        // Reopen acceptance unless shutdown has started. Each dequeued command
        // retains its observer through completion, including across pause/resume.
        [[nodiscard]] bool resume(Observer observer = {});
        // Stops acceptance, discards waiting work, then joins the in-flight plan.
        // Safe to call repeatedly from frontend lifecycle threads.
        void shutdown();

    private:
        void run();
        static void publish(const Observer& observer,
            const RecognitionExecutionResult& result) noexcept;
        CommandDispatcher dispatcher_;
        Observer observer_;
        const std::size_t capacity_;
        std::mutex mutex_;
        std::mutex shutdownMutex_;
        std::condition_variable ready_;
        std::deque<command::SemanticCommand> queue_;
        bool accepting_{true};
        bool stopping_{false};
        std::thread worker_;
    };
}
