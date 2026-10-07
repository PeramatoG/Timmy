/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/recognition_command_executor.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace timmy::core {
    RecognitionCommandExecutor::RecognitionCommandExecutor(
        CommandDispatcher dispatcher, Observer observer, std::size_t capacity)
        : dispatcher_(std::move(dispatcher)), observer_(std::move(observer)), capacity_(capacity)
    {
        if (capacity == 0) throw std::invalid_argument("Execution queue capacity must be positive.");
        worker_ = std::thread([this] { run(); });
    }

    RecognitionCommandExecutor::~RecognitionCommandExecutor() { shutdown(); }

    RecognitionExecutionResult RecognitionCommandExecutor::submit(const RecognitionTranscript& result)
    {
        if (result.semanticState != RecognitionSemanticState::produced
            || !result.semanticCommand
            || result.assemblyState != RecognitionAssemblyState::complete)
            return {};
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return {RecognitionExecutionState::failed, "Execution is stopped."};
            if (!accepting_) return {};
            if (queue_.size() >= capacity_)
                return {RecognitionExecutionState::failed, "Execution queue is full."};
            queue_.push_back(*result.semanticCommand);
        }
        ready_.notify_one();
        return {RecognitionExecutionState::queued, {}};
    }

    void RecognitionCommandExecutor::pause()
    {
        std::lock_guard lock(mutex_);
        accepting_ = false;
        queue_.clear();
    }

    bool RecognitionCommandExecutor::resume(Observer observer)
    {
        {
            std::lock_guard lock(mutex_);
            if (stopping_) return false;
            observer_ = std::move(observer);
            accepting_ = true;
        }
        ready_.notify_one();
        return true;
    }

    void RecognitionCommandExecutor::shutdown()
    {
        std::lock_guard shutdownLock(shutdownMutex_);
        {
            std::lock_guard lock(mutex_);
            accepting_ = false;
            stopping_ = true;
            queue_.clear();
        }
        ready_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void RecognitionCommandExecutor::publish(const Observer& observer,
        const RecognitionExecutionResult& result) noexcept
    {
        try { if (observer) observer(result); }
        catch (...) { /* Frontend exceptions must not terminate execution or shutdown. */ }
    }

    void RecognitionCommandExecutor::run()
    {
        for (;;)
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || (accepting_ && !queue_.empty()); });
            if (stopping_) return;
            auto command = std::move(queue_.front());
            queue_.pop_front();
            const auto observer = observer_;
            lock.unlock();
            publish(observer, {RecognitionExecutionState::queued, {}});
            RecognitionExecutionResult execution;
            try
            {
                const auto result = dispatcher_.dispatch(command);
                execution = {result.state == CommandDispatchState::succeeded
                    ? RecognitionExecutionState::succeeded : RecognitionExecutionState::failed,
                    result.diagnostic};
            }
            catch (const std::exception& error)
            {
                execution = {RecognitionExecutionState::failed, error.what()};
            }
            catch (...)
            {
                execution = {RecognitionExecutionState::failed, "Emitter threw an unknown exception."};
            }
            publish(observer, execution);
        }
    }
}
