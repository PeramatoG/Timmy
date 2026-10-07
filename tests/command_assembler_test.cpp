/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_assembler.h"
#include "timmy/command/semantic_command_parser.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

    bool expectResult(const timmy::command::AssemblyResult& result,
        timmy::command::AssemblyState expectedState,
        std::string_view expectedText)
    {
        if (result.state == expectedState && result.text == expectedText)
        {
            return true;
        }
        std::cerr << "Unexpected assembly result [" << result.text
            << "] expected [" << expectedText << "]\n";
        return false;
    }

    bool expectPending(const timmy::command::CommandAssembler& assembler,
        bool expected)
    {
        if (assembler.hasPending() == expected)
        {
            return true;
        }
        std::cerr << "Unexpected retained pending assembly state\n";
        return false;
    }

    // Every completed assembly must be understood by the same semantic grammar.
    bool expectComplete(const timmy::command::AssemblyResult& result,
        std::string_view expectedText, std::string_view expectedDescription)
    {
        bool passed = expectResult(result,
            timmy::command::AssemblyState::complete, expectedText);
        const auto parsed = timmy::command::SemanticCommandParser{}.parse(result.text);
        if (!parsed.command
            || timmy::command::describe(*parsed.command) != expectedDescription)
        {
            std::cerr << "Completed assembly did not produce ["
                << expectedDescription << "] for [" << result.text << "]\n";
            passed = false;
        }
        return passed;
    }

} // namespace

// Exercises bounded deterministic fragments and TEST progression precedence.
int main()
{
    using timmy::command::AssemblyState;
    using timmy::command::CommandAssembler;
    CommandAssembler assembler;
    bool passed = true;

    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("al 100%"),
        "Canal 4 al 100%", "SetIntensity(Channel(4), 100%)");
    passed &= expectPending(assembler, false);
    passed &= expectComplete(assembler.accept("Fixture 1 At Full"),
        "Fixture 1 At Full", "SetIntensity(Fixture(1), 100%)");
    passed &= expectPending(assembler, false);

    passed &= expectResult(assembler.accept("Canales 1 a 10"),
        AssemblyState::pending, "Canales 1 a 10");
    passed &= expectComplete(assembler.accept("al 50"),
        "Canales 1 a 10 al 50", "SetIntensity(Channel(1 through 10), 50%)");
    passed &= expectPending(assembler, false);

    // A range and its action can arrive across several recognition fragments.
    passed &= expectResult(assembler.accept("Canales 1 a"),
        AssemblyState::pending, "Canales 1 a");
    passed &= expectResult(assembler.accept("10"),
        AssemblyState::pending, "Canales 1 a 10");
    passed &= expectResult(assembler.accept("al"),
        AssemblyState::pending, "Canales 1 a 10 al");
    passed &= expectComplete(assembler.accept("50"),
        "Canales 1 a 10 al 50", "SetIntensity(Channel(1 through 10), 50%)");
    passed &= expectPending(assembler, false);

    passed &= expectResult(assembler.accept("Canales 1,"),
        AssemblyState::pending, "Canales 1,");
    passed &= expectResult(assembler.accept("3 y 5"),
        AssemblyState::pending, "Canales 1, 3 y 5");
    passed &= expectComplete(assembler.accept("al 70"),
        "Canales 1, 3 y 5 al 70", "SetIntensity(Channel(1, 3, 5), 70%)");

    passed &= expectResult(assembler.accept("Canales 1 a 10"),
        AssemblyState::pending, "Canales 1 a 10");
    passed &= expectResult(assembler.accept("excepto"),
        AssemblyState::pending, "Canales 1 a 10 excepto");
    passed &= expectResult(assembler.accept("5 a 8"),
        AssemblyState::pending, "Canales 1 a 10 excepto 5 a 8");
    passed &= expectComplete(assembler.accept("al 50"),
        "Canales 1 a 10 excepto 5 a 8 al 50",
        "SetIntensity(Channel(1 through 10 except 5 through 8), 50%)");

    passed &= expectResult(assembler.accept("Pon canal 5"),
        AssemblyState::pending, "Pon canal 5");
    passed &= expectComplete(assembler.accept("al 50"),
        "Pon canal 5 al 50", "SetIntensity(Channel(5), 50%)");
    passed &= expectResult(assembler.accept("Selecciona"),
        AssemblyState::pending, "Selecciona");
    passed &= expectComplete(assembler.accept("canales 1 a 10"),
        "Selecciona canales 1 a 10", "SelectSelection(Channel(1 through 10))");
    passed &= expectResult(assembler.accept("Test canales"),
        AssemblyState::pending, "Test canales");
    passed &= expectComplete(assembler.accept("1 a 10"),
        "Test canales 1 a 10", "TestSelection(Channel(1 through 10))");

    passed &= expectResult(assembler.accept("Canales del 1 al 10"),
        AssemblyState::pending, "Canales del 1 al 10");
    passed &= expectComplete(assembler.accept("al 50"),
        "Canales del 1 al 10 al 50", "SetIntensity(Channel(1 through 10), 50%)");
    passed &= expectResult(assembler.accept("Channels 1, 3 and 5"),
        AssemblyState::pending, "Channels 1, 3 and 5");
    passed &= expectComplete(assembler.accept("at 70"),
        "Channels 1, 3 and 5 at 70", "SetIntensity(Channel(1, 3, 5), 70%)");
    passed &= expectResult(assembler.accept("Canales 1 a 15 excepto 5 a 8"),
        AssemblyState::pending, "Canales 1 a 15 excepto 5 a 8");
    passed &= expectComplete(assembler.accept("a tope"),
        "Canales 1 a 15 excepto 5 a 8 a tope",
        "SetIntensity(Channel(1 through 15 except 5 through 8), 100%)");

    passed &= expectResult(assembler.accept("Canal 10"),
        AssemblyState::pending, "Canal 10");
    passed &= expectComplete(assembler.accept("test"),
        "test Canal 10", "TestSelection(Channel(10))");
    passed &= expectPending(assembler, false);
    passed &= expectComplete(assembler.accept("test"), "test", "AdvanceTest()");
    passed &= expectComplete(assembler.accept("siguiente"), "siguiente", "AdvanceTest()");
    passed &= expectComplete(assembler.accept("siguiente"), "siguiente", "AdvanceTest()");
    passed &= expectComplete(assembler.accept("next test"), "next test", "AdvanceTest()");
    passed &= expectResult(assembler.accept("Fixtures 1 a 6"),
        AssemblyState::pending, "Fixtures 1 a 6");
    passed &= expectComplete(assembler.accept("test"),
        "test Fixtures 1 a 6", "TestSelection(Fixture(1 through 6))");
    passed &= expectResult(assembler.accept("Group 3"),
        AssemblyState::pending, "Group 3");
    passed &= expectComplete(assembler.accept("test"),
        "test Group 3", "TestSelection(Group(3))");

    passed &= expectResult(assembler.accept("Fixtures 1 a 6"),
        AssemblyState::pending, "Fixtures 1 a 6");
    passed &= expectComplete(assembler.accept("a home"),
        "Fixtures 1 a 6 a home", "HomeSelection(Fixture(1 through 6))");

    // An independently complete action supersedes an unfinished selection.
    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("Release channel 5"),
        "Release channel 5", "ReleaseSelection(Channel(5))");
    passed &= expectPending(assembler, false);
    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("Select channels 1 through 10"),
        "Select channels 1 through 10", "SelectSelection(Channel(1 through 10))");
    passed &= expectPending(assembler, false);
    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("Clear editor"),
        "Clear editor", "ClearEditor()");
    passed &= expectPending(assembler, false);
    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("Next fixture"),
        "Next fixture", "NavigateSelection(Fixture, Next)");
    passed &= expectPending(assembler, false);
    passed &= expectResult(assembler.accept("Canal 4"),
        AssemblyState::pending, "Canal 4");
    passed &= expectComplete(assembler.accept("Channel 8 at 25"),
        "Channel 8 at 25", "SetIntensity(Channel(8), 25%)");
    passed &= expectPending(assembler, false);

    // A new unfinished target replaces the previous one deterministically.
    passed &= expectResult(assembler.accept("Channel 5"),
        AssemblyState::pending, "Channel 5");
    passed &= expectResult(assembler.accept("Group 2"),
        AssemblyState::pending, "Group 2");
    passed &= expectComplete(assembler.accept("At 25%"),
        "Group 2 At 25%", "SetIntensity(Group(2), 25%)");
    passed &= expectResult(assembler.accept("Group 2"),
        AssemblyState::pending, "Group 2");
    assembler.reset();
    passed &= expectPending(assembler, false);
    passed &= expectResult(assembler.accept("At 25%"), AssemblyState::rejected, "");

    const std::string_view rejected[] = {
        "", "not a command", "Channel 0", "Channel 1 at 101",
        "Channels 10 through 1", "Home channel 5", "reset", "borra", "clear"
    };
    for (const auto fragment : rejected)
    {
        passed &= expectResult(assembler.accept(fragment), AssemblyState::rejected, "");
        passed &= expectPending(assembler, false);
    }

    // Invalid fragments cannot execute or erase a previously valid target.
    passed &= expectResult(assembler.accept("Channel 5"),
        AssemblyState::pending, "Channel 5");
    passed &= expectResult(assembler.accept(""), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    passed &= expectResult(assembler.accept("at 101"), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    passed &= expectResult(assembler.accept("reset"), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    passed &= expectComplete(assembler.accept("At 50"),
        "Channel 5 At 50", "SetIntensity(Channel(5), 50%)");
    passed &= expectPending(assembler, false);

    passed &= expectResult(assembler.accept("Canal 7"),
        AssemblyState::pending, "Canal 7");
    const std::string oversized(CommandAssembler::maxPendingLength + 1, 'x');
    passed &= expectResult(assembler.accept(oversized), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    const std::string boundedButInvalid(CommandAssembler::maxPendingLength, 'x');
    passed &= expectResult(assembler.accept(boundedButInvalid), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    for (int index = 0; index < 100; ++index)
    {
        passed &= expectResult(assembler.accept("unusable fragment"),
            AssemblyState::rejected, "");
        passed &= expectPending(assembler, true);
    }
    passed &= expectComplete(assembler.accept("al 70%"),
        "Canal 7 al 70%", "SetIntensity(Channel(7), 70%)");
    passed &= expectPending(assembler, false);

    const std::string longestTarget = "Channel "
        + std::string(CommandAssembler::maxPendingLength - 9, '0') + "1";
    passed &= expectResult(assembler.accept(longestTarget),
        AssemblyState::pending, longestTarget);
    passed &= expectResult(assembler.accept("at 50"), AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    passed &= expectResult(assembler.accept(longestTarget + "0"),
        AssemblyState::rejected, "");
    passed &= expectPending(assembler, true);
    passed &= expectComplete(assembler.accept("Channel 2 at 50"),
        "Channel 2 at 50", "SetIntensity(Channel(2), 50%)");
    passed &= expectPending(assembler, false);

    // The acceptance workflow remains stateless after every completed action.
    passed &= expectComplete(assembler.accept("Test canal 21"),
        "Test canal 21", "TestSelection(Channel(21))");
    passed &= expectComplete(assembler.accept("Siguiente"), "Siguiente", "AdvanceTest()");
    passed &= expectComplete(assembler.accept("Siguiente"), "Siguiente", "AdvanceTest()");
    passed &= expectComplete(assembler.accept("Canales 30 a 40 al 50"),
        "Canales 30 a 40 al 50", "SetIntensity(Channel(30 through 40), 50%)");
    passed &= expectComplete(assembler.accept("Libera canales 30 a 40"),
        "Libera canales 30 a 40", "ReleaseSelection(Channel(30 through 40))");
    passed &= expectComplete(assembler.accept("Fixtures 1 a 6 a home"),
        "Fixtures 1 a 6 a home", "HomeSelection(Fixture(1 through 6))");
    passed &= expectComplete(assembler.accept("Limpia editor"),
        "Limpia editor", "ClearEditor()");
    passed &= expectPending(assembler, false);

    return passed ? 0 : 1;
}
