#include "timmy/core/recognition_command_executor.h"
#include "timmy/command/semantic_command_parser.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

using namespace timmy;
using namespace std::chrono_literals;
namespace {
    void require(bool value) { if (!value) std::abort(); }
    core::RecognitionTranscript produced(const char* text = "Channel 5 At 50")
    {
        core::RecognitionTranscript result;
        result.assemblyState = core::RecognitionAssemblyState::complete;
        result.semanticState = core::RecognitionSemanticState::produced;
        result.semanticCommand = command::SemanticCommandParser{}.parse(text).command;
        require(result.semanticCommand.has_value());
        return result;
    }
    struct RecordingEmitter : transport::ICommandEmitter
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::string> commands;
        bool blocked = true;
        bool entered = false;
        bool active = false;
        bool concurrent = false;
        bool throws = false;
        transport::EmissionResult result{transport::EmissionState::succeeded, {}};
        transport::EmissionResult emit(const command::SemanticCommand& command) override
        {
            std::unique_lock lock(mutex);
            concurrent |= active;
            active = entered = true;
            commands.push_back(command::describe(command));
            changed.notify_all();
            changed.wait(lock, [this] { return !blocked; });
            active = false;
            if (throws) throw std::runtime_error("emitter exception");
            return result;
        }
        void waitEntered()
        {
            std::unique_lock lock(mutex);
            require(changed.wait_for(lock, 5s, [this] { return entered; }));
        }
        void release()
        {
            std::lock_guard lock(mutex);
            blocked = false;
            changed.notify_all();
        }
    };
    struct Events
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<core::RecognitionExecutionResult> results;
        void observe(const core::RecognitionExecutionResult& result)
        {
            std::lock_guard lock(mutex);
            results.push_back(result);
            changed.notify_all();
        }
        void wait(std::size_t size)
        {
            std::unique_lock lock(mutex);
            require(changed.wait_for(lock, 5s, [&] { return results.size() >= size; }));
        }
    };
}
int main()
{
    using State = core::RecognitionExecutionState;
    {
        auto emitter = std::make_shared<RecordingEmitter>();
        Events events;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& e) { events.observe(e); }, 2);
        auto invalid = produced();
        for (auto assembly : {core::RecognitionAssemblyState::pending, core::RecognitionAssemblyState::rejected})
        {
            invalid.assemblyState = assembly;
            require(executor.submit(invalid).state == State::ignored);
        }
        invalid = produced();
        invalid.semanticState = core::RecognitionSemanticState::unsupportedOrInvalid;
        require(executor.submit(invalid).state == State::ignored);
        invalid.semanticState = core::RecognitionSemanticState::notAttempted;
        require(executor.submit(invalid).state == State::ignored);
        invalid = produced(); invalid.semanticCommand.reset();
        require(executor.submit(invalid).state == State::ignored);
        require(executor.submit(produced()).state == State::queued);
        emitter->waitEntered();
        require(executor.submit(produced("Group 3 At 25")).state == State::queued);
        require(executor.submit(produced()).state == State::queued); // Intentional repeat.
        const auto overflow = executor.submit(produced());
        require(overflow.state == State::failed && overflow.diagnostic
            == "Execution queue is full (2 waiting commands); command rejected.");
        emitter->release();
        events.wait(6);
        executor.shutdown();
        require(emitter->commands.size() == 3 && !emitter->concurrent);
        require(emitter->commands[0] == command::describe(*produced().semanticCommand));
        require(emitter->commands[1] == command::describe(*produced("Group 3 At 25").semanticCommand));
        require(emitter->commands[2] == emitter->commands[0]);
        for (std::size_t i = 0; i < 6; i += 2)
        {
            require(events.results[i].state == State::queued);
            require(events.results[i+1].state == State::succeeded);
        }
        require(executor.submit(produced()).diagnostic == "Execution is stopped.");
        require(!executor.resume());
    }
    {
        // Both frontends use this bounded default. The in-flight command does
        // not consume a waiting slot, and overflow is never replayed later.
        require(core::RecognitionCommandExecutor::defaultQueueCapacity == 4);
        auto emitter = std::make_shared<RecordingEmitter>();
        Events events;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& event) { events.observe(event); });
        require(executor.submit(produced()).state == State::queued);
        emitter->waitEntered();
        for (std::size_t i = 0; i < core::RecognitionCommandExecutor::defaultQueueCapacity; ++i)
            require(executor.submit(produced("Group 3 At 25")).state == State::queued);
        const auto overflow = executor.submit(produced("Channel 6 At 60"));
        require(overflow.state == State::failed && overflow.diagnostic
            == "Execution queue is full (4 waiting commands); command rejected.");
        emitter->release();
        events.wait(2 * (1 + core::RecognitionCommandExecutor::defaultQueueCapacity));
        executor.shutdown();
        require(emitter->commands.size() == 5 && !emitter->concurrent);
        require(emitter->commands.front() == command::describe(*produced().semanticCommand));
        for (std::size_t i = 1; i < emitter->commands.size(); ++i)
            require(emitter->commands[i] == command::describe(*produced("Group 3 At 25").semanticCommand));
        require(events.results.size() == 10);
    }
    {
        auto emitter = std::make_shared<RecordingEmitter>();
        Events events;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& e) { events.observe(e); }, 1);
        require(executor.submit(produced()).state == State::queued);
        emitter->waitEntered();
        require(executor.submit(produced("Group 3 At 25")).state == State::queued);
        auto stopped = std::async(std::launch::async, [&] { executor.shutdown(); });
        // Acceptance is observable without releasing the active emitter.
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (executor.submit(produced()).diagnostic != "Execution is stopped.")
        {
            require(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        require(stopped.wait_for(0ms) == std::future_status::timeout);
        emitter->release();
        require(stopped.wait_for(5s) == std::future_status::ready);
        stopped.get();
        require(emitter->commands.size() == 1);
        require(events.results.size() == 2 && events.results.back().state == State::succeeded);
        executor.shutdown();
    }
    {
        auto emitter = std::make_shared<RecordingEmitter>();
        Events firstArm;
        Events secondArm;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& event) { firstArm.observe(event); });
        require(executor.submit(produced()).state == State::queued);
        emitter->waitEntered();
        for (std::size_t i = 0; i < core::RecognitionCommandExecutor::defaultQueueCapacity; ++i)
            require(executor.submit(produced("Group 3 At 25")).state == State::queued);
        require(executor.submit(produced()).state == State::failed);
        // Pause must return while emit is blocked and discard waiting work.
        executor.pause();
        executor.pause();
        require(executor.submit(produced()).state == State::ignored);
        require(executor.resume([&](const auto& event) { secondArm.observe(event); }));
        require(executor.submit(produced("Channel 6 At 60")).state == State::queued);
        emitter->release();
        firstArm.wait(2);
        secondArm.wait(2);
        executor.shutdown();
        require(emitter->commands.size() == 2 && !emitter->concurrent);
        require(emitter->commands[1] == command::describe(*produced("Channel 6 At 60").semanticCommand));
        require(firstArm.results.size() == 2 && firstArm.results.back().state == State::succeeded);
        require(secondArm.results.size() == 2 && secondArm.results.back().state == State::succeeded);
    }
    for (bool throws : {false, true})
    {
        auto emitter = std::make_shared<RecordingEmitter>();
        emitter->blocked = false;
        emitter->throws = throws;
        emitter->result = {transport::EmissionState::failed, "endpoint unavailable"};
        Events events;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& e) { events.observe(e); });
        require(executor.submit(produced()).state == State::queued);
        events.wait(2);
        executor.shutdown();
        require(events.results.back().state == State::failed);
        require(events.results.back().diagnostic == (throws ? "emitter exception" : "endpoint unavailable"));
        require(emitter->commands.size() == 1);
    }
}
