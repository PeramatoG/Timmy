/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "command_output_events.h"
#include "command_output_session.h"
#include "timmy/command/semantic_command_parser.h"

#include <wx/app.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace timmy;
using namespace std::chrono_literals;

namespace {

    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        std::cerr << "Line " << line << ": " << expression << '\n';
        std::abort();
    }

#define REQUIRE(expression) require((expression), #expression, __LINE__)

    using ExecutionState = core::RecognitionExecutionState;

    core::RecognitionTranscript produced()
    {
        core::RecognitionTranscript transcript;
        transcript.assemblyState = core::RecognitionAssemblyState::complete;
        transcript.semanticState = core::RecognitionSemanticState::produced;
        transcript.semanticCommand = command::SemanticCommandParser{}.parse("Channel 5 At 50").command;
        transcript.validation = command::validateIntent(*transcript.semanticCommand);
        transcript.rendering = command::renderIntent(*transcript.semanticCommand);
        REQUIRE(transcript.semanticCommand.has_value());
        return transcript;
    }

    gui::CommandOutputConfiguration enabled()
    {
        gui::CommandOutputConfiguration configuration;
        configuration.enabled = true;
        configuration.host = "192.0.2.5";
        return configuration;
    }

    class Frontend final : public wxEvtHandler
    {
    public:
        Frontend()
        {
            Bind(wxEVT_THREAD, [this](wxThreadEvent& event) {
                REQUIRE(std::this_thread::get_id() == thread);
                const auto payload = event.GetPayload<gui::CommandOutputEventPayload>();
                ++*handled;
                (payload.generation == generation ? delivered : discarded).push_back(payload);
            }, gui::commandOutputEventId);
        }

        const std::thread::id thread = std::this_thread::get_id();
        std::shared_ptr<std::size_t> handled = std::make_shared<std::size_t>(0);
        std::uint64_t generation = 1;
        std::vector<gui::CommandOutputEventPayload> delivered;
        std::vector<gui::CommandOutputEventPayload> discarded;
    };

    class PostedResults
    {
    public:
        gui::CommandOutputSession::Observer observer(Frontend& frontend)
        {
            return [this, thread = frontend.thread,
                    bridge = gui::commandOutputObserver(frontend, frontend.generation)](
                        const core::RecognitionExecutionResult& result) {
                REQUIRE(std::this_thread::get_id() != thread);
                bridge(result);
                std::lock_guard lock(mutex_);
                ++posted_;
                changed_.notify_one();
            };
        }

        void waitFor(std::size_t count)
        {
            std::unique_lock lock(mutex_);
            REQUIRE(changed_.wait_for(lock, 5s, [&] { return posted_ >= count; }));
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        std::size_t posted_ = 0;
    };

    class FakeEmitter final : public transport::ICommandEmitter
    {
    public:
        explicit FakeEmitter(transport::EmissionResult result) : result_(std::move(result)) {}
        transport::EmissionResult emit(const command::SemanticCommand&) override { return result_; }

    private:
        transport::EmissionResult result_;
    };

    class BlockingEmitter final : public transport::ICommandEmitter
    {
    public:
        transport::EmissionResult emit(const command::SemanticCommand&) override
        {
            std::unique_lock lock(mutex_);
            ++calls;
            entered_ = true;
            changed_.notify_all();
            changed_.wait(lock, [&] { return released_; });
            return {transport::EmissionState::succeeded, {}};
        }
        void waitEntered()
        {
            std::unique_lock lock(mutex_);
            REQUIRE(changed_.wait_for(lock, 5s, [&] { return entered_; }));
        }
        void release()
        {
            std::lock_guard lock(mutex_);
            released_ = true;
            changed_.notify_all();
        }
        int calls = 0;

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        bool entered_ = false;
        bool released_ = false;
    };

    void processEvents() { wxAppConsole::GetInstance()->ProcessPendingEvents(); }

    void successAndFailure()
    {
        for (const bool success : {true, false})
        {
            Frontend frontend;
            PostedResults notifications;
            const transport::EmissionResult outcome = success
                ? transport::EmissionResult{transport::EmissionState::succeeded, {}}
                : transport::EmissionResult{transport::EmissionState::failed, "console endpoint unavailable"};
            std::weak_ptr<transport::ICommandEmitter> emitter;
            gui::CommandOutputSession session([&](auto, auto) {
                auto created = std::make_shared<FakeEmitter>(outcome);
                emitter = created;
                return created;
            });
            REQUIRE(session.start(enabled(), notifications.observer(frontend)).succeeded);
            REQUIRE(session.submit(produced()).state == ExecutionState::queued);
            notifications.waitFor(2);
            REQUIRE(frontend.delivered.empty());
            session.stop();
            REQUIRE(!session.active() && emitter.expired());
            processEvents();
            REQUIRE(frontend.discarded.empty());
            REQUIRE(frontend.delivered.size() == 2);
            REQUIRE(frontend.delivered[0].generation == frontend.generation);
            REQUIRE(frontend.delivered[1].generation == frontend.generation);
            REQUIRE(frontend.delivered[0].result.state == ExecutionState::queued);
            REQUIRE(frontend.delivered[0].result.renderedCommand.empty());
            REQUIRE(frontend.delivered[1].result.state == (success
                ? ExecutionState::succeeded : ExecutionState::failed));
            REQUIRE(frontend.delivered[1].result.diagnostic == outcome.diagnostic);
            REQUIRE(frontend.delivered[1].result.renderedCommand == "CH 5 AT 5 0");
        }
    }

    void liveRearmFiltersInFlightEvents()
    {
        Frontend frontend;
        PostedResults notifications;
        auto emitter = std::make_shared<BlockingEmitter>();
        gui::CommandOutputSession session([&](auto, auto) { return emitter; });
        REQUIRE(session.start({}).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
        processEvents();
        REQUIRE(frontend.delivered.empty() && frontend.discarded.empty());

        REQUIRE(session.start(enabled(), notifications.observer(frontend)).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        emitter->waitEntered();
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        session.disarm();
        REQUIRE(!session.active());
        REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
        ++frontend.generation;
        REQUIRE(session.start(enabled(), notifications.observer(frontend)).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        emitter->release();
        notifications.waitFor(4);
        session.stop();
        processEvents();
        REQUIRE(emitter->calls == 2);
        REQUIRE(frontend.discarded.size() == 2);
        REQUIRE(frontend.delivered.size() == 2);
        for (const auto& event : frontend.discarded) REQUIRE(event.generation == 1);
        for (const auto& event : frontend.delivered) REQUIRE(event.generation == 2);
        REQUIRE(frontend.discarded.back().result.state == ExecutionState::succeeded);
        REQUIRE(frontend.delivered.back().result.state == ExecutionState::succeeded);
    }

    std::shared_ptr<std::size_t> restartAndShutdown()
    {
        Frontend frontend;
        PostedResults notifications;
        std::weak_ptr<transport::ICommandEmitter> emitter;
        gui::CommandOutputSession session([&](auto, auto) {
            auto created = std::make_shared<FakeEmitter>(
                transport::EmissionResult{transport::EmissionState::succeeded, {}});
            emitter = created;
            return created;
        });
        REQUIRE(session.start(enabled(), notifications.observer(frontend)).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        notifications.waitFor(2);
        session.stop();
        REQUIRE(emitter.expired());

        ++frontend.generation;
        REQUIRE(session.start(enabled(), notifications.observer(frontend)).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        notifications.waitFor(4);
        session.stop();
        processEvents();
        REQUIRE(frontend.discarded.size() == 2);
        REQUIRE(frontend.delivered.size() == 2);
        for (const auto& event : frontend.discarded) REQUIRE(event.generation == 1);
        for (const auto& event : frontend.delivered) REQUIRE(event.generation == 2);

        ++frontend.generation;
        auto ownership = std::make_shared<int>(0);
        const std::weak_ptr<int> workerOwnership = ownership;
        REQUIRE(session.start(enabled(),
            [ownership = std::move(ownership), observer = notifications.observer(frontend)](
                const auto& result) {
                REQUIRE(ownership != nullptr);
                observer(result);
            }).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        notifications.waitFor(6);
        // Join and destroy all worker ownership before clearing queued wx events.
        session.stop();
        REQUIRE(!session.active() && emitter.expired() && workerOwnership.expired());
        frontend.DeletePendingEvents();
        REQUIRE(frontend.delivered.size() == 2 && frontend.discarded.size() == 2);
        REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
        // The target's destructor removes its pending-handler registration,
        // just as frame destruction does before the application pumps again.
        return frontend.handled;
    }

} // namespace

int main(int argc, char** argv)
{
    // A console application initializes wx event handling without a display.
    wxAppConsole::SetInstance(new wxAppConsole);
    REQUIRE(wxEntryStart(argc, argv));
    REQUIRE(wxAppConsole::GetInstance()->CallOnInit());
    successAndFailure();
    liveRearmFiltersInFlightEvents();
    const auto handled = restartAndShutdown();
    REQUIRE(*handled == 4);
    processEvents();
    REQUIRE(*handled == 4);
    wxAppConsole::GetInstance()->OnExit();
    wxEntryCleanup();
}
