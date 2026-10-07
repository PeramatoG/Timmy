#include "timmy/core/recognition_command_executor.h"
#include "timmy/command/semantic_command_parser.h"
#include "timmy/transport/hydra2_osc_command_profile.h"
#include "timmy/transport/osc_command_emitter.h"
#include "timmy/transport/osc_packet.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <vector>

using namespace timmy;
using namespace std::chrono_literals;
namespace {
    void require(bool value,
        const std::source_location& location = std::source_location::current())
    {
        if (!value)
        {
            std::cerr << location.file_name() << ':' << location.line()
                << ": executor check failed\n";
            std::abort();
        }
    }
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

    class FakeBackend final : public asr::IAsrBackend
    {
    public:
        asr::AsrCapabilities capabilities() const override { return {}; }
        asr::AsrState state() const override { return state_; }
        asr::AsrOperationResult start(const asr::AsrConfiguration&) override
        {
            state_ = asr::AsrState::running;
            return {true, std::nullopt};
        }
        asr::AsrOperationResult stop() override
        {
            state_ = asr::AsrState::stopped;
            return {true, std::nullopt};
        }
        void emit(const std::string& text)
        {
            publishResult({text, asr::AsrResultKind::final, {}});
        }
    private:
        void processAudioBlock(const audio::AudioBlockView&) override {}
        asr::AsrState state_{asr::AsrState::stopped};
    };

    class ExecutionObserver final : public core::IRecognitionPipelineObserver
    {
    public:
        explicit ExecutionObserver(core::RecognitionCommandExecutor& executor)
            : executor_(executor) {}
        void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
        void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override {}
        void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
        {
            latest = transcript;
            submitted = executor_.submit(transcript);
        }
        core::RecognitionTranscript latest;
        core::RecognitionExecutionResult submitted;
    private:
        core::RecognitionCommandExecutor& executor_;
    };

    class RecordingSender final : public transport::IUdpSender
    {
    public:
        transport::EmissionResult send(const transport::OscDestination&,
            std::span<const std::uint8_t> bytes) override
        {
            std::lock_guard lock(mutex_);
            packets_.emplace_back(bytes.begin(), bytes.end());
            return {transport::EmissionState::succeeded, {}};
        }
        std::vector<std::vector<std::uint8_t>> packets()
        {
            std::lock_guard lock(mutex_);
            return packets_;
        }
    private:
        std::mutex mutex_;
        std::vector<std::vector<std::uint8_t>> packets_;
    };

    // Exercises spoken commands through the real pipeline and asynchronous
    // dispatcher, including exact OSC key edges without a network endpoint.
    void naturalSelectionWorkflow()
    {
        using State = core::RecognitionExecutionState;
        transport::Hydra2OscOptions options;
        options.preClear = true;
        auto sender = std::make_unique<RecordingSender>();
        auto* recorded = sender.get();
        auto emitter = std::make_shared<transport::OscCommandEmitter>(
            transport::OscDestination{"test", 8000},
            std::make_shared<transport::Hydra2OscCommandProfile>(options),
            std::move(sender), [](auto) {});
        Events events;
        core::RecognitionCommandExecutor executor(core::CommandDispatcher{emitter},
            [&](const auto& event) { events.observe(event); });
        auto backend = std::make_shared<FakeBackend>();
        auto observer = std::make_shared<ExecutionObserver>(executor);
        core::RecognitionPipeline pipeline(
            std::make_shared<audio::AudioChannelSelector>(), backend);
        pipeline.setObserver(observer);
        require(pipeline.start({}).succeeded);

        std::size_t accepted = 0;
        std::vector<std::vector<std::uint8_t>> expectedPackets;
        const auto expectIgnored = [&](const std::string& phrase) {
            backend->emit(phrase);
            require(!observer->latest.semanticCommand);
            require(observer->submitted.state == State::ignored);
            require(recorded->packets() == expectedPackets);
        };
        const auto expectPlan = [&](const std::string& phrase,
                                    const std::vector<std::string>& addresses) {
            backend->emit(phrase);
            require(observer->latest.assemblyState == core::RecognitionAssemblyState::complete);
            require(observer->latest.semanticState == core::RecognitionSemanticState::produced);
            require(observer->latest.semanticCommand.has_value());
            require(observer->submitted.state == State::queued);
            events.wait(2 * ++accepted);
            for (const auto& address : addresses)
            {
                for (const std::int32_t edge : {1, 0})
                {
                    const auto packet = transport::serializeOscMessage({address, {edge}});
                    require(packet.bytes.has_value());
                    expectedPackets.push_back(*packet.bytes);
                }
            }
            require(recorded->packets() == expectedPackets);
        };

        expectIgnored("súmale el 4");
        expectPlan("  Dame los canales del 1 al 10 y me quitas el 6.  ",
            {"/keyC", "/keyC", "/keyCH", "/key1", "/keyTHRU", "/key1", "/key0",
             "/keyEXCP", "/key6", "/keyOK"});
        require(observer->latest.normalizedTranscript
            == "Dame los canales del 1 al 10 y me quitas el 6");
        const auto& selection = std::get<command::SelectSelection>(
            *observer->latest.semanticCommand).target;
        require(selection.included.size() == 1 && selection.included.front().first == 1
            && selection.included.front().last == 10);
        require(selection.excluded.size() == 1 && selection.excluded.front().first == 6);

        expectPlan("súmale el 4", {"/keyOK", "/keyCH", "/key4", "/keyOK"});
        const auto& add = std::get<command::ModifySelection>(*observer->latest.semanticCommand);
        require(add.modification == command::SelectionModification::add);
        require(add.target.type == command::TargetType::channel);
        expectPlan("suma del 20 al 25",
            {"/keyOK", "/keyCH", "/key2", "/key0", "/keyTHRU", "/key2", "/key5", "/keyOK"});
        const auto& range = std::get<command::ModifySelection>(
            *observer->latest.semanticCommand).target;
        require(range.included.size() == 1 && range.included.front().first == 20
            && range.included.front().last == 25);
        expectPlan("quita del 5 al 8",
            {"/keyEXCP", "/keyCH", "/key5", "/keyTHRU", "/key8", "/keyOK"});
        const auto& remove = std::get<command::ModifySelection>(*observer->latest.semanticCommand);
        require(remove.modification == command::SelectionModification::remove);
        require(remove.target.included.size() == 1 && remove.target.included.front().first == 5
            && remove.target.included.front().last == 8);

        expectPlan("limpia la selección", {"/keyC", "/keyC"});
        require(std::holds_alternative<command::ClearSelection>(*observer->latest.semanticCommand));
        expectPlan("release all", {"/keyREL", "/keyREL"});
        require(std::holds_alternative<command::ReleaseEditor>(*observer->latest.semanticCommand));
        expectPlan("cler all", {"/keyRST"});
        require(std::holds_alternative<command::ClearEditor>(*observer->latest.semanticCommand));
        for (const auto& phrase : {"clear", "borra", "limpia", "reset", "clerr all"})
            expectIgnored(phrase);

        // Frontend disarming resets the linguistic session before rearming.
        executor.pause();
        pipeline.discardPendingAssembly();
        require(executor.resume([&](const auto& event) { events.observe(event); }));
        expectIgnored("remove 10");
        expectPlan("select fixture 7", {"/keyC", "/keyC", "/keyFXT", "/key7", "/keyOK"});
        expectPlan("add 4", {"/keyOK", "/keyFXT", "/key4", "/keyOK"});
        require(std::get<command::ModifySelection>(*observer->latest.semanticCommand).target.type
            == command::TargetType::fixture);
        require(pipeline.stop().succeeded);
        require(pipeline.start({}).succeeded);
        expectIgnored("quita el 10");
        require(pipeline.stop().succeeded);
        executor.shutdown();
        require(events.results.size() == 2 * accepted);
        for (std::size_t index = 0; index < events.results.size(); index += 2)
        {
            require(events.results[index].state == State::queued);
            require(events.results[index + 1].state == State::succeeded);
        }
    }
}
int main()
{
    naturalSelectionWorkflow();
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
