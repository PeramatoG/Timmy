/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_processor.h"
#include "timmy/core/command_dispatcher.h"
#include "timmy/command/command_assembler.h"
#include "timmy/transport/osc_command_emitter.h"
#include "timmy/transport/osc_packet.h"
#include "timmy/transport/hydra2_osc_command_profile.h"
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace timmy;
namespace {
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    const command::SetIntensity& intensity(const command::CommandProcessingResult& result)
    {
        require(result.parsed.command.has_value(), "No intent");
        require(result.validation.accepted && result.rendering.text.has_value(), "Intent not validated/rendered");
        require(std::holds_alternative<command::SetIntensity>(*result.parsed.command), "Wrong operation");
        return std::get<command::SetIntensity>(*result.parsed.command);
    }
    class RecordingUdpSender : public transport::IUdpSender
    {
    public:
        transport::EmissionResult send(const transport::OscDestination&,
            std::span<const std::uint8_t> payload) override
        {
            packets.emplace_back(payload.begin(), payload.end());
            return {transport::EmissionState::succeeded, {}};
        }
        std::vector<std::vector<std::uint8_t>> packets;
    };
    class RecordingTextEmitter : public transport::ICommandEmitter {
    public:
        transport::EmissionResult emit(const command::SemanticCommand&) override
        { throw std::runtime_error("Dispatcher bypassed rendered boundary"); }
        transport::EmissionResult emitRendered(const transport::RenderedCommand& value) override
        { ++calls; text = value.text; return {transport::EmissionState::succeeded, {}}; }
        int calls{};
        std::string text;
    };
}
int main()
{
    try {
        command::CommandProcessor processor;
        const auto on = processor.process("  enciende   canal 1!  ");
        const auto& single = intensity(on);
        require(single.target.type == command::TargetType::channel && single.target.included.size() == 1
            && single.target.included[0].first == 1 && single.target.included[0].last == 1
            && single.level.percent() == 100, "Single intensity semantics");
        require(on.transcript.normalizedText == "enciende canal 1", "Lexical normalization");
        require(*on.rendering.text == "Channel 1 At 100", "Native renderer");
        for (const auto text : {"enciende canal 1", "full channel 1", "canal 1 a tope", "channel 1 at full"}) {
            const auto result = processor.process(text);
            const auto& equivalent = intensity(result);
            require(equivalent.target.type == single.target.type && equivalent.target.included[0].first == 1
                && equivalent.target.included[0].last == 1 && equivalent.level.percent() == 100, "Synonym equivalence");
        }
        const auto off = processor.process("apaga canal 10");
        require(intensity(off).target.included[0].first == 10 && intensity(off).level.percent() == 0, "Off intent");
        const auto elided = processor.process("apaga 10");
        require(intensity(elided).target.type == command::TargetType::channel
            && !elided.parsed.explicitTargetType, "Explicit profile default must not become context");
        command::CommandProfile noDefault;
        noDefault.parsing.defaultTarget.reset();
        require(!command::CommandProcessor{noDefault}.process("apaga 10").parsed.command, "Disabled default target");
        command::CommandProfile fixtureDefault;
        fixtureDefault.parsing.defaultTarget = command::TargetType::fixture;
        require(intensity(command::CommandProcessor{fixtureDefault}.process("apaga 10")).target.type
            == command::TargetType::fixture, "Alternate default target");
        for (const auto text : {"enciende los canales del 2 al 40", "on channels 2 through 40",
                "enciende canales 2 hasta 40", "enciende canales 2 a 40"}) {
            const auto result = processor.process(text);
            const auto& range = intensity(result);
            require(range.target.included.size() == 1 && range.target.included[0].first == 2
                && range.target.included[0].last == 40 && range.level.percent() == 100, "Range intent");
        }
        const auto singularRange = processor.process("apaga canal del 10 al 50");
        require(intensity(singularRange).target.included[0].first == 10
            && intensity(singularRange).target.included[0].last == 50
            && intensity(singularRange).level.percent() == 0, "Explicit singular range");
        command::CommandContext linguisticContext;
        linguisticContext.observe(processor.process("fixture 2 at 50").parsed);
        const auto contextualDefault = processor.process("apaga 10", linguisticContext);
        require(intensity(contextualDefault).target.type == command::TargetType::channel,
            "Profile default must remain independent of remembered target");
        linguisticContext.observe(contextualDefault.parsed);
        require(linguisticContext.lastExplicitTargetType == command::TargetType::fixture,
            "Defaulted target must not replace remembered explicit target");
        require(!command::SemanticCommandParser{}.parse("apaga 10").command,
            "Unconfigured parser must not infer a target");
        const auto valued = processor.process("canales del 2 al 40 al 50");
        require(intensity(valued).level.percent() == 50 && intensity(valued).target.included[0].last == 40, "Explicit value");
        const auto list = processor.process("channels 1, 3 and 5 at 25%");
        require(intensity(list).target.included.size() == 3 && intensity(list).level.percent() == 25, "List intent");
        command::CommandAssembler assembler{processor.profile().parsing};
        require(assembler.accept("enciende canal").state == command::AssemblyState::pending,
            "Prefix target fragment must remain pending");
        const auto assembledSingle = assembler.accept("1");
        require(assembledSingle.state == command::AssemblyState::complete
            && intensity(processor.process(assembledSingle.text)).target.included[0].first == 1,
            "Prefix target fragments must assemble");
        require(assembler.accept("enciende canales del 2 al").state == command::AssemblyState::pending,
            "Prefix range endpoint must remain pending");
        require(assembler.accept("40").state == command::AssemblyState::complete,
            "Prefix range fragments must assemble");
        require(assembler.accept("enciende canales 1,").state == command::AssemblyState::pending,
            "Prefix list comma must not execute a partial selection");
        const auto assembledList = assembler.accept("3 y 5");
        require(assembledList.state == command::AssemblyState::complete
            && intensity(processor.process(assembledList.text)).target.included.size() == 3,
            "Prefix list fragments must assemble");
        const auto all = processor.process("fuera todo");
        require(intensity(all).target.all && intensity(all).target.included.empty()
            && intensity(all).level.percent() == 0 && *all.rendering.text == "Channel All At 0", "All intent");
        require(!transport::Hydra2OscCommandProfile{}.map(*all.parsed.command).plan, "Unverified Hydra All mapping");
        command::CommandProfile limited;
        limited.validation.allowAll = false;
        limited.validation.maxIdentifier = 20;
        require(!command::CommandProcessor{limited}.process("fuera todo").validation.accepted, "All validation policy");
        require(!command::CommandProcessor{limited}.process("canales del 2 al 40 al 50").rendering.text, "Validation precedes rendering");
        command::CommandProfile alternate;
        alternate.rendering.channel = "Chan";
        require(*command::CommandProcessor{alternate}.process("enciende canal 1").rendering.text == "Chan 1 At 100", "Rendering configuration");
        alternate.parsing.vocabulary.insert(alternate.parsing.vocabulary.begin(), {"ignite", "on"});
        require(intensity(command::CommandProcessor{alternate}.process("ignite channel 1")).level.percent() == 100,
            "Profile parsing vocabulary");
        for (const auto text : {"apaga canal 0", "canales del 40 al 2 al 50", "channel 1 at 101",
                "enciende canal 1 basura", "enciende canal 1 at 50", "apaga canal 10 al 50", "apaga canal 10 a 50", "apaga canal 1 at full", "channel 1 at 50; delete all", "record cue 1", "off all except 3", ""}) {
            const auto result = processor.process(text);
            require(!result.parsed.command && !result.rendering.text, "Unsupported expression produced output");
        }
        auto emitter = std::make_shared<RecordingTextEmitter>();
        core::CommandDispatcher dispatcher{emitter, limited};
        require(dispatcher.dispatch(*on.parsed.command).state == core::CommandDispatchState::succeeded
            && emitter->text == "Channel 1 At 100", "Rendered emitter integration");
        require(dispatcher.dispatch(*all.parsed.command).state == core::CommandDispatchState::notDispatched
            && emitter->calls == 1, "Validator bypass at dispatch");
        // Current console target: semantic intent -> actual Hydra keys -> OSC
        // pulses. No model, network, hardware or artificial transport waits.
        const auto hydraProfile = command::hydra2CommandProfile(true);
        const auto hydraProcessed = command::CommandProcessor{hydraProfile}.process("enciende los canales del 2 al 40");
        require(hydraProcessed.rendering.text == "C C CH 2 THRU 4 0 AT AT", "Hydra renderer syntax");
        require(hydraProcessed.rendering.consoleKeys == std::vector<std::string>{"C", "C", "CH", "2", "THRU", "4", "0", "AT", "AT"},
            "Hydra renderer keys");
        auto sender = std::make_unique<RecordingUdpSender>();
        auto* recorded = sender.get();
        auto hydraOsc = std::make_shared<transport::OscCommandEmitter>(transport::OscDestination{"test", 8000},
            std::make_shared<transport::Hydra2OscCommandProfile>(), std::move(sender), [](auto) {});
        core::CommandDispatcher hydraDispatcher{hydraOsc, hydraProfile};
        const auto dispatched = hydraDispatcher.dispatch(*hydraProcessed.parsed.command);
        require(dispatched.state == core::CommandDispatchState::succeeded
            && dispatched.renderedCommand == *hydraProcessed.rendering.text, "Hydra rendered dispatch");
        std::vector<std::vector<std::uint8_t>> expectedPackets;
        const transport::Hydra2OscOptions hydraOptions;
        for (const auto& key : hydraProcessed.rendering.consoleKeys) {
            for (const std::int32_t edge : {1, 0}) {
                const auto packet = transport::serializeOscMessage({hydraOptions.keyAddresses.at(key), {edge}});
                require(packet.bytes.has_value(), "Hydra packet serialization");
                expectedPackets.push_back(*packet.bytes);
            }
        }
        require(recorded->packets == expectedPackets, "Hydra emitter did not consume rendered keys");
        require(hydraDispatcher.dispatch(*all.parsed.command).state == core::CommandDispatchState::notDispatched
            && recorded->packets == expectedPackets, "Unsupported Hydra all intent emitted");
        require(!transport::Hydra2OscCommandProfile{}.mapRendered({*on.rendering.text, *on.parsed.command}).plan,
            "Renderer/profile mismatch must fail explicitly");
        command::SetIntensity invalid{command::Channel{0}, *command::IntensityLevel::fromPercent(50)};
        require(!command::renderIntent(invalid).text, "Invalid intent rendering");
        require(dispatcher.dispatch(command::SemanticCommand{invalid}).state == core::CommandDispatchState::notDispatched
            && emitter->calls == 1, "Constructed invalid intent dispatched");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
