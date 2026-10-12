/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/command/command_processor.h"
#include "timmy/command/command_assembler.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace timmy::command;
namespace {
    void require(bool condition, std::string_view message)
    {
        if (!condition) throw std::runtime_error(std::string(message));
    }
    bool reason(const SemanticParseResult& result, std::string_view expected)
    {
        return std::find(result.inferenceReasons.begin(), result.inferenceReasons.end(), expected)
            != result.inferenceReasons.end();
    }
    void accepts(const CommandProfile& profile, std::string_view text, std::string_view expected)
    {
        const auto result = CommandProcessor{profile}.process(text);
        require(result.parsed.command && result.validation.accepted && result.rendering.text == expected, text);
        CommandAssembler assembler{profile.parsing};
        require(assembler.accept(result.repair.repairedText).state == AssemblyState::complete, text);
        const auto classified = SemanticCommandParser{profile.parsing}.classify(result.repair.repairedText);
        require(classified.command && describe(*classified.command) == describe(*result.parsed.command), text);
        require(classified.inferenceReasons == result.parsed.inferenceReasons, text);
    }
}
int main()
{
    try {
        const CommandProfile channel;
        struct Case { const char* text; const char* expected; };
        const Case cases[] = {
            {"channel 20 10", "Channel 20 At 10"}, {"20 10", "Channel 20 At 10"},
            {"fixture 5 100", "Fixture 5 At 100"}, {"group 2 70", "Group 2 At 70"},
            {"channel 10 20 50", "Channel 10 + 20 At 50"},
            {"channel 1 3 5 70", "Channel 1 + 3 + 5 At 70"},
            {"fixture 1 3 5 70", "Fixture 1 + 3 + 5 At 70"},
            {"channel 20 0", "Channel 20 At 0"},
            {"channel 150 50", "Channel 150 At 50"},
            {"channel 1 3 at 50", "Channel 1 + 3 At 50"},
            {"channel 1 and 3 50", "Channel 1 + 3 At 50"},
            {"channel 1 through 10 50", "Channel 1 Through 10 At 50"},
            {"canal 1 a 10 50", "Channel 1 Through 10 At 50"},
            {"enciende 20", "Channel 20 At 100"}, {"apaga 20", "Channel 20 At 0"},
            {"enciende 20 10", "Channel 20 + 10 At 100"},
            {"enciende 10 20", "Channel 10 + 20 At 100"},
            {"enciende 1 3 5", "Channel 1 + 3 + 5 At 100"},
            {"apaga fixture 1 3 5", "Fixture 1 + 3 + 5 At 0"},
            {"fuera 1 3 5", "Channel 1 + 3 + 5 At 0"},
            {"on 1 200", "Channel 1 + 200 At 100"},
            {"off 1 200", "Channel 1 + 200 At 0"},
            {"enciende canal 5", "Channel 5 At 100"},
            { "apaga canal 5", "Channel 5 At 0" },
            {"apaga canal 10 a 50", "Channel 10 Through 50 At 0"},
            {"enciende fixture 1 a 10", "Fixture 1 Through 10 At 100"},
            {"apaga fixture 1 a 10", "Fixture 1 Through 10 At 0"},
            {"apaga grupo 2", "Group 2 At 0"},
            {"apaga 1 a 10", "Channel 1 Through 10 At 0"},
            {"enciende 1 al 10", "Channel 1 Through 10 At 100"},
            {"selecciona 1 3 5", "Select Channel 1 + 3 + 5"},
            {"selecciona 5", "Select Channel 5"},
            {"release 1 3 5", "Release Channel 1 + 3 + 5"},
            {"test 1 3 5", "Test Channel 1 + 3 + 5"},
            {"channel 1 through 10 at 50", "Channel 1 Through 10 At 50"},
            {"canales 1 a 10 al 50", "Channel 1 Through 10 At 50"},
            {"channel 1 and 10 at 50", "Channel 1 + 10 At 50"},
            {"fixture 1, 3 and 5 at 70", "Fixture 1 + 3 + 5 At 70"},
            {"fixtures 1, 3 y 5 al 70", "Fixture 1 + 3 + 5 At 70"},
            {"channels 1 through 10 except 5 through 8 at 50", "Channel 1 Through 10 - 5 Through 8 At 50"},
            {"canales 1 a 10 excepto 5 a 50", "Channel 1 Through 10 - 5 At 50"},
            {"canales 1 a 10 excepto 5 50", "Channel 1 Through 10 - 5 At 50"},
            {"channels 1 through 10 except 5 50", "Channel 1 Through 10 - 5 At 50"},
            {"channels 1 through 10 except 5 at 50", "Channel 1 Through 10 - 5 At 50"},
            {"channels 1 through 10 except 5 through 8 50", "Channel 1 Through 10 - 5 Through 8 At 50"},
            {"channels 1 through 10 plus 15 50", "Channel 1 Through 10 + 15 At 50"},
            {"select channels 1 through 10 except 5 8", "Select Channel 1 Through 10 - 5 - 8"},
            {"chan 20 10", "Channel 20 At 10"},
            {"enciende cha 10 20", "Channel 10 + 20 At 100"},
            {"canal veinte al diez", "Channel 20 At 10"},
            {"channel 1 3 5 70,", "Channel 1 + 3 + 5 At 70"},
            // Existing valid explicit selections remain authoritative.
            {"canales 1 a 15 excepto 5 a 8", "Select Channel 1 Through 15 - 5 Through 8"},
            {"channels 1 through 10 except 5 through 50", "Select Channel 1 Through 10 - 5 Through 50"},
            {"select channels 1 through 10 except 5 a 50", "Select Channel 1 Through 10 - 5 Through 50"},
            {"channels 1 through 10 plus 15 al 50", "Select Channel 1 Through 10 + 15 Through 50"}
        };
        for (const auto& test : cases) accepts(channel, test.text, test.expected);
        for (const auto text : {"", "50", "at 50", "channel 50", "channel 0 50", "0 50",
                "channel 1 101", "channel 1 full", "channel 1 through 0 50", "channel 10 through 1 50",
                "channel 1 through 50", "channel 1 and 50", "channel 1 , 50",
                "channel 1 through at 50", "channel 1 and at 50", "channel 1 and 3 and 50",
                "channel 1 except 0 50", "channel 1 at 50 60",
                "channel 1 at 101 50", "channel 1 through 10 except 5 except 6 50",
                "on 5 at 50", "on channel 5 al 50", "on channel 5 0", "home channel 5",
                "set channel 5 50", "blackout 1 50", "clear 1 50", "editor 1 50",
                "release all 1 50", "all 1 50", "all 50", "channels all 1 50", "select all 1 50",
                "unknown 1 50", "channel 1 fixture 5 50", "channel 4294967296 50"})
            require(!CommandProcessor{}.process(text).validation.accepted, text);

        require(channel.parsing.defaultTarget == TargetType::channel, "Channel default");
        for (const auto& option : defaultTargetOptions) {
            CommandProfile profile;
            profile.parsing.defaultTarget = option.type;
            const std::string name = option.type == TargetType::channel ? "Channel"
                : option.type == TargetType::fixture ? "Fixture" : "Group";
            accepts(profile, "20 50", name + " 20 At 50");
            accepts(profile, "enciende 1 a 10", name + " 1 Through 10 At 100");
            accepts(profile, "selecciona 5", "Select " + name + " 5");
            accepts(profile, "canal 20 50", "Channel 20 At 50");
            require(defaultTargetFromName(option.name) == option.type && defaultTargetName(option.type) == option.name,
                "Target option conversion");
        }
        require(!defaultTargetFromName("cue") && defaultTargetName(static_cast<TargetType>(99)).empty(), "Unsupported targets");
        CommandProfile fixture;
        fixture.parsing.defaultTarget = TargetType::fixture;
        accepts(fixture, "home 5", "Home Fixture 5");
        accepts(fixture, "home 1 3 5", "Home Fixture 1 + 3 + 5");
        fixture.parsing.defaultTarget.reset();
        require(!CommandProcessor{fixture}.process("20 50").validation.accepted, "No configured target");

        const auto explicitResult = CommandProcessor{}.process("channel 1 through 10 at 50");
        require(explicitResult.parsed.inferenceReasons.empty(), "Explicit parse diagnostics");
        const auto implicit = CommandProcessor{}.process("20 10");
        require(reason(implicit.parsed, "implicit_final_level") && reason(implicit.parsed, "default_target")
            && !implicit.parsed.explicitTargetType, "Implicit diagnostics/context");
        require(reason(CommandProcessor{}.process("channel 1 3 50").parsed, "bare_numeric_list"), "List diagnostics");
        require(reason(CommandProcessor{}.process("canales 1 a 10 excepto 5 a 50").parsed,
            "exclusion_level_disambiguation"), "Exclusion diagnostics");
        const auto lexical = CommandProcessor{}.process("chan 20 10");
        require(lexical.repair.applied() && reason(lexical.parsed, "implicit_final_level"), "Repair composition");
        CommandContext context;
        context.observe(SemanticCommandParser{fixture.parsing}.parse("select group 3"));
        context.observe(SemanticCommandParser{channel.parsing}.parse("selecciona 5"));
        require(context.lastExplicitTargetType == TargetType::group, "Default must not replace explicit context");
        CommandAssembler assembler{channel.parsing};
        require(assembler.accept("channel 5").state == AssemblyState::pending, "Selection pending");
        require(assembler.accept("50").state == AssemblyState::complete, "Implicit multi-utterance completion");
        require(assembler.accept("channel 5").state == AssemblyState::pending, "Next selection pending");
        require(assembler.accept("20 50").text == "channel 5 20 50", "Numeric continuation retains pending selection");
        require(assembler.accept("fixtures 1,").state == AssemblyState::pending, "Pending fixture list");
        const auto continuation = assembler.accept("3, 5 at 50");
        require(continuation.text == "fixtures 1, 3, 5 at 50", "Numeric continuation must retain fixture target");
        accepts(channel, continuation.text, "Fixture 1 + 3 + 5 At 50");
        require(assembler.accept("fixture 5").state == AssemblyState::pending, "Pending target before explicit replacement");
        require(assembler.accept("channel 20 50").text == "channel 20 50", "Explicit target supersedes pending selection");
        CommandProfile limited;
        limited.validation.maxIdentifier = 4;
        require(!CommandProcessor{limited}.process("5 50").validation.accepted, "Inference validation");
        std::string oversized = "on channel";
        for (std::size_t i = 0; i <= TargetSelection::maxTerms; ++i) oversized += " 1";
        require(!CommandProcessor{}.process(oversized).validation.accepted, "Bounded terms");
        std::cout << "Numeric-role resolution tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
