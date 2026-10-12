/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "command_output_session.h"
#include "timmy/command/semantic_command_parser.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
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

    struct EmissionRecord
    {
        std::atomic<int> liveEmitters{0};
        std::mutex mutex;
        std::vector<std::string> commands;

        std::vector<std::string> snapshot()
        {
            std::lock_guard lock(mutex);
            return commands;
        }
    };

    class FakeEmitter final : public transport::ICommandEmitter
    {
    public:
        FakeEmitter(std::shared_ptr<EmissionRecord> record,
            transport::EmissionResult result, bool throws)
            : record_(std::move(record)), result_(std::move(result)), throws_(throws)
        {
            ++record_->liveEmitters;
        }

        ~FakeEmitter() override { --record_->liveEmitters; }

        transport::EmissionResult emit(const command::SemanticCommand& semantic) override
        {
            {
                std::lock_guard lock(record_->mutex);
                record_->commands.push_back(command::describe(semantic));
            }
            if (throws_) throw std::runtime_error("injected emitter exception");
            return result_;
        }

    private:
        std::shared_ptr<EmissionRecord> record_;
        transport::EmissionResult result_;
        bool throws_;
    };

    struct FactoryProbe
    {
        int calls = 0;
        transport::OscDestination destination;
        std::shared_ptr<const transport::IOscCommandProfile> profile;
        std::shared_ptr<EmissionRecord> record = std::make_shared<EmissionRecord>();
        std::weak_ptr<transport::ICommandEmitter> emitter;
        transport::EmissionResult result{transport::EmissionState::succeeded, {}};
        bool throws = false;

        gui::CommandOutputSession::EmitterFactory factory()
        {
            return [this](auto endpoint, auto mapping) {
                ++calls;
                destination = std::move(endpoint);
                profile = std::move(mapping);
                auto created = std::make_shared<FakeEmitter>(record, result, throws);
                emitter = created;
                return created;
            };
        }
    };

    // Mirrors frontend event delivery: workers copy values into a mailbox and
    // the frontend drains them on its own thread after a bounded notification.
    class FrontendMailbox
    {
    public:
        void post(const core::RecognitionExecutionResult& result)
        {
            std::lock_guard lock(mutex_);
            REQUIRE(std::this_thread::get_id() != frontend_);
            pending_.push_back(result);
            changed_.notify_all();
        }

        void waitFor(std::size_t count)
        {
            REQUIRE(std::this_thread::get_id() == frontend_);
            std::unique_lock lock(mutex_);
            REQUIRE(changed_.wait_for(lock, 5s, [&] { return pending_.size() >= count; }));
        }

        std::vector<core::RecognitionExecutionResult> drain()
        {
            REQUIRE(std::this_thread::get_id() == frontend_);
            std::lock_guard lock(mutex_);
            return std::exchange(pending_, {});
        }

    private:
        const std::thread::id frontend_ = std::this_thread::get_id();
        std::mutex mutex_;
        std::condition_variable changed_;
        std::vector<core::RecognitionExecutionResult> pending_;
    };

    void disabledOutput()
    {
        FactoryProbe factory;
        FrontendMailbox mailbox;
        gui::CommandOutputSession session(factory.factory());
        gui::CommandOutputConfiguration configuration;
        REQUIRE(!configuration.enabled);
        // Disabled output must not require valid transport fields.
        configuration.profile = static_cast<gui::OutputProfile>(-1);
        configuration.port = "invalid";
        configuration.keyHoldMs = "invalid";
        configuration.keyGapMs = "invalid";
        REQUIRE(session.start(configuration, [&](const auto& result) { mailbox.post(result); }).succeeded);
        REQUIRE(!session.active());
        REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
        session.stop();
        REQUIRE(factory.calls == 0);
        REQUIRE(factory.record->snapshot().empty());
        REQUIRE(mailbox.drain().empty());
    }

    void invalidConfigurations()
    {
        const auto hydraLanguage = gui::commandProfileForOutput(enabled());
        REQUIRE(command::CommandProcessor{hydraLanguage}.process("enciende canal 1").rendering.text == "CH 1 AT AT");
        auto genericConfiguration = enabled();
        genericConfiguration.profile = gui::OutputProfile::genericOsc;
        REQUIRE(command::CommandProcessor{gui::commandProfileForOutput(genericConfiguration)}
            .process("enciende canal 1").rendering.text == "Channel 1 At 100");
        for (const auto& option : command::defaultTargetOptions) {
            genericConfiguration.defaultTarget = option.type;
            const auto profile = gui::commandProfileForOutput(genericConfiguration);
            REQUIRE(profile.parsing.defaultTarget == option.type);
            const auto result = command::CommandProcessor{profile}.process("20 50");
            REQUIRE(result.validation.accepted && result.parsed.command);
            REQUIRE(std::get<command::SetIntensity>(*result.parsed.command).target.type == option.type);
            REQUIRE(command::CommandProcessor{profile}.process("canal 20 50").rendering.text == "Channel 20 At 50");
        }
        REQUIRE(gui::CommandOutputConfiguration{}.defaultTarget == command::TargetType::channel);
        FactoryProbe factory;
        gui::CommandOutputSession session(factory.factory());
        const auto reject = [&](const auto& configuration, const std::string& field) {
            const auto result = session.start(configuration);
            REQUIRE(!result.succeeded);
            std::string diagnostic = result.diagnostic;
            for (auto& character : diagnostic)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            REQUIRE(diagnostic.find(field) != std::string::npos);
            REQUIRE(!session.active());
            REQUIRE(factory.calls == 0);
        };
        auto configuration = enabled();
        configuration.profile = static_cast<gui::OutputProfile>(-1);
        reject(configuration, "profile");
        for (const auto& host : {std::string{}, std::string{" \t"}, std::string{"bad host"},
                std::string{"console\0host", 12}})
        {
            configuration = enabled();
            configuration.host = host;
            reject(configuration, "host");
        }
        for (const auto* port : {"", "0", "65536", "-1", "+1", "8000x", "8000 ", "4294967296"})
        {
            configuration = enabled();
            configuration.port = port;
            reject(configuration, "port");
        }
        for (const auto* timing : {"", "-1", "+1", "bad", "30ms", "251", "4294967295", "4294967296"})
        {
            configuration = enabled();
            configuration.keyHoldMs = timing;
            reject(configuration, "hold");
            configuration = enabled();
            configuration.keyGapMs = timing;
            reject(configuration, "gap");
        }
        REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
    }

    void checkHydraPlan(const FactoryProbe& factory,
        const std::vector<std::string>& addresses,
        std::chrono::milliseconds hold, std::chrono::milliseconds gap)
    {
        REQUIRE(dynamic_cast<const transport::Hydra2OscCommandProfile*>(factory.profile.get()));
        const auto mapped = factory.profile->map(*produced().semanticCommand);
        REQUIRE(mapped.plan.has_value());
        REQUIRE(mapped.plan->size() == addresses.size());
        for (std::size_t index = 0; index < addresses.size(); ++index)
        {
            const auto* pulse = std::get_if<transport::OscKeyPulse>(&(*mapped.plan)[index]);
            REQUIRE(pulse != nullptr);
            REQUIRE(pulse->address == addresses[index]);
            REQUIRE(pulse->hold == hold);
            REQUIRE(pulse->gap == gap);
        }
    }

    void profileConfiguration()
    {
        FactoryProbe factory;
        gui::CommandOutputSession session(factory.factory());
        auto configuration = enabled();
        const transport::Hydra2OscOptions defaults;
        REQUIRE(configuration.port == "8000");
        REQUIRE(configuration.keyHoldMs == std::to_string(defaults.hold.count()));
        REQUIRE(configuration.keyGapMs == std::to_string(defaults.gap.count()));
        REQUIRE(!configuration.preClear);
        REQUIRE(session.start(configuration).succeeded);
        REQUIRE(factory.destination.host == configuration.host);
        REQUIRE(factory.destination.port == 8000);
        checkHydraPlan(factory, {"/keyCH", "/key5", "/key@", "/key5", "/key0"},
            defaults.hold, defaults.gap);
        session.stop();

        configuration.keyHoldMs = "250";
        configuration.keyGapMs = "0";
        REQUIRE(session.start(configuration).succeeded);
        checkHydraPlan(factory, {"/keyCH", "/key5", "/key@", "/key5", "/key0"}, 250ms, 0ms);
        session.stop();

        configuration.host = "  hydra.example.test \t";
        configuration.port = "9000";
        configuration.keyHoldMs = "7";
        configuration.keyGapMs = "11";
        configuration.preClear = true;
        REQUIRE(session.start(configuration).succeeded);
        REQUIRE(factory.destination.host == "hydra.example.test");
        REQUIRE(factory.destination.port == 9000);
        checkHydraPlan(factory,
            {"/keyC", "/keyC", "/keyCH", "/key5", "/key@", "/key5", "/key0"}, 7ms, 11ms);
        session.stop();

        configuration.profile = gui::OutputProfile::genericOsc;
        configuration.keyHoldMs = "invalid disabled field";
        configuration.keyGapMs = "-1";
        REQUIRE(session.start(configuration).succeeded);
        REQUIRE(dynamic_cast<const transport::GenericOscCommandProfile*>(factory.profile.get()));
        const auto mapped = factory.profile->map(*produced().semanticCommand);
        REQUIRE(mapped.plan && mapped.plan->size() == 1);
        const auto* message = std::get_if<transport::OscMessage>(&mapped.plan->front());
        REQUIRE(message != nullptr);
        REQUIRE(message->address == "/timmy/channel/intensity");
        REQUIRE(message->arguments == std::vector<transport::OscArgument>({std::int32_t{5}, std::int32_t{50}}));
        session.stop();
        REQUIRE(factory.record->snapshot().empty());
    }

    void onlyProducedCommandsExecute()
    {
        FactoryProbe factory;
        FrontendMailbox mailbox;
        gui::CommandOutputSession session(factory.factory());
        REQUIRE(session.start(enabled(), [&](const auto& result) { mailbox.post(result); }).succeeded);
        auto transcript = produced();
        for (auto state : {core::RecognitionAssemblyState::pending, core::RecognitionAssemblyState::rejected})
        {
            transcript.assemblyState = state;
            REQUIRE(session.submit(transcript).state == ExecutionState::ignored);
        }
        transcript = produced();
        for (auto state : {core::RecognitionSemanticState::notAttempted,
                core::RecognitionSemanticState::unsupportedOrInvalid})
        {
            transcript.semanticState = state;
            REQUIRE(session.submit(transcript).state == ExecutionState::ignored);
        }
        transcript = produced();
        transcript.semanticCommand.reset();
        REQUIRE(session.submit(transcript).state == ExecutionState::ignored);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        mailbox.waitFor(2);
        session.stop();
        const auto events = mailbox.drain();
        REQUIRE(events.size() == 2);
        REQUIRE(events.front().state == ExecutionState::queued);
        REQUIRE(events.back().state == ExecutionState::succeeded);
        REQUIRE(events.back().diagnostic.empty());
        REQUIRE(factory.record->snapshot() == std::vector<std::string>{"SetIntensity(Channel(5), 50%)"});
    }

    void executionFailuresReachFrontend()
    {
        for (bool throws : {false, true})
        {
            FactoryProbe factory;
            factory.result = {transport::EmissionState::failed, "console endpoint unavailable"};
            factory.throws = throws;
            FrontendMailbox mailbox;
            gui::CommandOutputSession session(factory.factory());
            REQUIRE(session.start(enabled(), [&](const auto& result) { mailbox.post(result); }).succeeded);
            REQUIRE(session.submit(produced()).state == ExecutionState::queued);
            mailbox.waitFor(2);
            session.stop();
            const auto events = mailbox.drain();
            REQUIRE(events.size() == 2);
            REQUIRE(events.front().state == ExecutionState::queued);
            REQUIRE(events.back().state == ExecutionState::failed);
            REQUIRE(events.back().diagnostic == (throws
                ? "injected emitter exception" : "console endpoint unavailable"));
        }
    }

    void lifecycleReleasesResources()
    {
        FactoryProbe factory;
        FrontendMailbox mailbox;
        std::weak_ptr<int> workerOwnership;
        {
            gui::CommandOutputSession session(factory.factory());
            for (int cycle = 0; cycle < 4; ++cycle)
            {
                auto ownership = std::make_shared<int>(cycle);
                workerOwnership = ownership;
                REQUIRE(session.start(enabled(),
                    [ownership = std::move(ownership), &mailbox](const auto& result) {
                        REQUIRE(ownership != nullptr);
                        mailbox.post(result);
                    }).succeeded);
                REQUIRE(session.active());
                REQUIRE(factory.record->liveEmitters == 1);
                auto changed = enabled();
                changed.host = "192.0.2.99";
                const auto restart = session.start(changed);
                REQUIRE(!restart.succeeded && restart.diagnostic.find("active") != std::string::npos);
                REQUIRE(factory.calls == cycle + 1);
                REQUIRE(factory.destination.host == "192.0.2.5");
                REQUIRE(session.submit(produced()).state == ExecutionState::queued);
                mailbox.waitFor(2);
                session.stop();
                session.stop();
                REQUIRE(!session.active());
                REQUIRE(factory.emitter.expired());
                REQUIRE(factory.record->liveEmitters == 0);
                REQUIRE(workerOwnership.expired());
                REQUIRE(mailbox.drain().size() == 2);
                REQUIRE(session.submit(produced()).state == ExecutionState::ignored);
                REQUIRE(mailbox.drain().empty());
            }
            auto ownership = std::make_shared<int>(0);
            workerOwnership = ownership;
            REQUIRE(session.start(enabled(),
                [ownership = std::move(ownership), &mailbox](const auto& result) {
                    REQUIRE(ownership != nullptr);
                    mailbox.post(result);
                }).succeeded);
            REQUIRE(session.submit(produced()).state == ExecutionState::queued);
            mailbox.waitFor(2);
            // Destructor must join and release an active session without Stop.
        }
        REQUIRE(factory.emitter.expired());
        REQUIRE(factory.record->liveEmitters == 0);
        REQUIRE(workerOwnership.expired());
        REQUIRE(mailbox.drain().size() == 2);
        REQUIRE(factory.record->snapshot().size() == 5);
    }

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
        void emit(std::string text)
        {
            publishResult({std::move(text), asr::AsrResultKind::final, {}});
        }

    private:
        void processAudioBlock(const audio::AudioBlockView&) override {}
        asr::AsrState state_ = asr::AsrState::stopped;
    };

    class RecognitionFrontend final : public core::IRecognitionPipelineObserver
    {
    public:
        explicit RecognitionFrontend(gui::CommandOutputSession& output) : output_(output) {}
        void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
        void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override {}
        void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
        {
            transcripts.push_back(transcript);
            REQUIRE(output_.submit(transcript).state == ExecutionState::ignored);
        }
        std::vector<core::RecognitionTranscript> transcripts;

    private:
        gui::CommandOutputSession& output_;
    };

    class BlockingEmitter final : public transport::ICommandEmitter
    {
    public:
        transport::EmissionResult emit(const command::SemanticCommand& semantic) override
        {
            std::unique_lock lock(mutex_);
            REQUIRE(!executing_);
            executing_ = true;
            commands.push_back(command::describe(semantic));
            changed_.notify_all();
            changed_.wait(lock, [&] { return released_; });
            executing_ = false;
            return {transport::EmissionState::succeeded, {}};
        }

        void waitEntered()
        {
            std::unique_lock lock(mutex_);
            REQUIRE(changed_.wait_for(lock, 5s, [&] { return executing_; }));
        }

        void release()
        {
            std::lock_guard lock(mutex_);
            released_ = true;
            changed_.notify_all();
        }

        std::vector<std::string> commands;

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        bool executing_ = false;
        bool released_ = false;
    };

    class LiveRecognitionFrontend final : public core::IRecognitionPipelineObserver
    {
    public:
        explicit LiveRecognitionFrontend(gui::CommandOutputSession& output) : output_(output) {}
        void onRecognitionStateChanged(core::RecognitionPipelineState) override {}
        void onRecognitionDiagnostic(const asr::AsrDiagnostic&) override {}
        void onRecognitionTranscript(const core::RecognitionTranscript& transcript) override
        {
            transcripts.push_back(transcript);
            submissions.push_back(output_.submit(transcript).state);
        }
        std::vector<core::RecognitionTranscript> transcripts;
        std::vector<ExecutionState> submissions;

    private:
        gui::CommandOutputSession& output_;
    };

    void waitingCommandsAreBounded()
    {
        auto emitter = std::make_shared<BlockingEmitter>();
        FrontendMailbox mailbox;
        gui::CommandOutputSession session([&](auto, auto) { return emitter; });
        REQUIRE(session.start(enabled(), [&](const auto& result) { mailbox.post(result); }).succeeded);
        REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        emitter->waitEntered();
        for (std::size_t i = 0; i < core::RecognitionCommandExecutor::defaultQueueCapacity; ++i)
            REQUIRE(session.submit(produced()).state == ExecutionState::queued);
        const auto overflow = session.submit(produced());
        REQUIRE(overflow.state == ExecutionState::failed);
        REQUIRE(overflow.diagnostic
            == "Execution queue is full (4 waiting commands); command rejected.");
        emitter->release();
        mailbox.waitFor(2 * (1 + core::RecognitionCommandExecutor::defaultQueueCapacity));
        session.stop();
        REQUIRE(emitter->commands.size() == 5);
        const auto events = mailbox.drain();
        REQUIRE(events.size() == 10);
        for (std::size_t i = 0; i < events.size(); i += 2)
        {
            REQUIRE(events[i].state == ExecutionState::queued);
            REQUIRE(events[i + 1].state == ExecutionState::succeeded);
        }
    }

    void liveArmingPreservesRecognition()
    {
        auto emitter = std::make_shared<BlockingEmitter>();
        int factoryCalls = 0;
        gui::CommandOutputSession session([&](auto, auto) {
            ++factoryCalls;
            return emitter;
        });
        FrontendMailbox firstArm;
        FrontendMailbox secondArm;
        REQUIRE(session.start({}).succeeded);
        auto backend = std::make_shared<FakeBackend>();
        auto frontend = std::make_shared<LiveRecognitionFrontend>(session);
        core::RecognitionPipeline pipeline(std::make_shared<audio::AudioChannelSelector>(), backend);
        pipeline.setObserver(frontend);
        REQUIRE(pipeline.start({}).succeeded);
        const auto speak = [&](const char* text) { backend->emit(text); };
        speak("Channel 5 At 50");
        REQUIRE(frontend->submissions.back() == ExecutionState::ignored);
        REQUIRE(factoryCalls == 0);

        auto invalid = enabled();
        invalid.host.clear();
        REQUIRE(!session.start(invalid).succeeded);
        REQUIRE(!session.active());
        REQUIRE(backend->state() == asr::AsrState::running);
        REQUIRE(session.start(enabled(), [&](const auto& result) { firstArm.post(result); }).succeeded);
        speak("Channel 5 At 50");
        REQUIRE(frontend->submissions.back() == ExecutionState::queued);
        emitter->waitEntered();
        for (std::size_t i = 0; i < core::RecognitionCommandExecutor::defaultQueueCapacity; ++i)
        {
            speak("Group 3 At 25");
            REQUIRE(frontend->submissions.back() == ExecutionState::queued);
        }
        speak("Group 9 At Full");
        REQUIRE(frontend->submissions.back() == ExecutionState::failed);
        // emit is still blocked, proving disarm does not wait for it to finish.
        session.disarm();
        session.disarm();
        REQUIRE(!session.active());
        REQUIRE(backend->state() == asr::AsrState::running);
        speak("Channel 5 At 50");
        REQUIRE(frontend->submissions.back() == ExecutionState::ignored);

        invalid = enabled();
        invalid.keyHoldMs = "251";
        const auto failedArm = session.start(invalid);
        REQUIRE(!failedArm.succeeded && failedArm.diagnostic.find("250") != std::string::npos);
        REQUIRE(!session.active());
        auto changed = enabled();
        changed.host = "192.0.2.99";
        REQUIRE(!session.start(changed).succeeded);
        REQUIRE(!session.active() && factoryCalls == 1);
        REQUIRE(backend->state() == asr::AsrState::running);

        REQUIRE(session.start(enabled(), [&](const auto& result) { secondArm.post(result); }).succeeded);
        REQUIRE(factoryCalls == 1);
        speak("Channel 6 At 60");
        REQUIRE(frontend->submissions.back() == ExecutionState::queued);
        emitter->release();
        firstArm.waitFor(2);
        secondArm.waitFor(2);
        REQUIRE(pipeline.stop().succeeded);
        session.stop();
        const auto oldEvents = firstArm.drain();
        const auto newEvents = secondArm.drain();
        REQUIRE(oldEvents.size() == 2 && oldEvents.back().state == ExecutionState::succeeded);
        REQUIRE(newEvents.size() == 2 && newEvents.back().state == ExecutionState::succeeded);
        REQUIRE(emitter->commands == std::vector<std::string>({
            "SetIntensity(Channel(5), 50%)", "SetIntensity(Channel(6), 60%)"}));
        REQUIRE(frontend->transcripts.size()
            == 5 + core::RecognitionCommandExecutor::defaultQueueCapacity);
        for (const auto& transcript : frontend->transcripts)
        {
            REQUIRE(!transcript.rawTranscript.empty());
            REQUIRE(!transcript.normalizedTranscript.empty());
            REQUIRE(transcript.assemblyState == core::RecognitionAssemblyState::complete);
            REQUIRE(transcript.semanticState == core::RecognitionSemanticState::produced);
            REQUIRE(transcript.semanticCommand.has_value());
        }
    }

    void recognitionOnlyStagesRemainVisible()
    {
        FactoryProbe factory;
        gui::CommandOutputSession session(factory.factory());
        REQUIRE(session.start({}).succeeded);
        auto backend = std::make_shared<FakeBackend>();
        auto frontend = std::make_shared<RecognitionFrontend>(session);
        core::RecognitionPipeline pipeline(std::make_shared<audio::AudioChannelSelector>(), backend);
        pipeline.setObserver(frontend);
        REQUIRE(pipeline.start({}).succeeded);
        backend->emit("  Canal 5.  ");
        backend->emit(" al 50 %. ");
        backend->emit("background noise");
        REQUIRE(frontend->transcripts.size() == 3);
        const auto& pending = frontend->transcripts[0];
        REQUIRE(pending.rawTranscript == "  Canal 5.  ");
        REQUIRE(pending.normalizedTranscript == "Canal 5");
        REQUIRE(pending.assemblyState == core::RecognitionAssemblyState::pending);
        REQUIRE(pending.assemblyText == "Canal 5");
        REQUIRE(pending.semanticState == core::RecognitionSemanticState::notAttempted);
        const auto& complete = frontend->transcripts[1];
        REQUIRE(complete.rawTranscript == " al 50 %. ");
        REQUIRE(complete.normalizedTranscript == "al 50%");
        REQUIRE(complete.assemblyState == core::RecognitionAssemblyState::complete);
        REQUIRE(complete.assemblyText == "Canal 5 al 50%");
        REQUIRE(complete.semanticState == core::RecognitionSemanticState::produced);
        REQUIRE(complete.semanticCommand.has_value());
        REQUIRE(command::describe(*complete.semanticCommand) == "SetIntensity(Channel(5), 50%)");
        REQUIRE(frontend->transcripts[2].assemblyState == core::RecognitionAssemblyState::rejected);
        REQUIRE(pipeline.stop().succeeded);
        session.stop();
        REQUIRE(factory.calls == 0);
        REQUIRE(factory.record->snapshot().empty());
    }

} // namespace

int main()
{
    disabledOutput();
    invalidConfigurations();
    profileConfiguration();
    onlyProducedCommandsExecute();
    executionFailuresReachFrontend();
    lifecycleReleasesResources();
    recognitionOnlyStagesRemainVisible();
    waitingCommandsAreBounded();
    liveArmingPreservesRecognition();
}
