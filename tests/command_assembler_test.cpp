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

#include <iostream>
#include <string>
#include <string_view>

namespace {

    // Checks an assembly result and prints a focused failure diagnostic.
    bool expectResult(
        const timmy::command::AssemblyResult& result,
        timmy::command::AssemblyState expectedState,
        std::string_view expectedText)
    {
        if (result.state == expectedState && result.text == expectedText)
        {
            return true;
        }
        std::cerr << "Unexpected assembly result text: [" << result.text << "]\n";
        return false;
    }

} // namespace

// Runs deterministic command assembly tests without models or external services.
int main()
{
    using timmy::command::AssemblyState;
    using timmy::command::CommandAssembler;

    CommandAssembler assembler;
    bool passed = true;

    passed &= expectResult(
        assembler.accept("Canal 4"), AssemblyState::pending, "Canal 4");
    passed &= expectResult(
        assembler.accept("al 100%"),
        AssemblyState::complete,
        "Canal 4 al 100%");
    passed &= !assembler.hasPending();

    passed &= expectResult(
        assembler.accept("Fixture 1 At Full"),
        AssemblyState::complete,
        "Fixture 1 At Full");
    passed &= !assembler.hasPending();

    passed &= expectResult(
        assembler.accept("Group 2"), AssemblyState::pending, "Group 2");
    assembler.reset();
    passed &= !assembler.hasPending();
    passed &= expectResult(
        assembler.accept("At 25%"), AssemblyState::rejected, "");

    passed &= expectResult(assembler.accept(""), AssemblyState::rejected, "");
    passed &= !assembler.hasPending();
    passed &= expectResult(
        assembler.accept("not a command"), AssemblyState::rejected, "");
    passed &= !assembler.hasPending();

    passed &= expectResult(
        assembler.accept("Channel 5"), AssemblyState::pending, "Channel 5");
    passed &= expectResult(assembler.accept(""), AssemblyState::rejected, "");
    passed &= assembler.hasPending();
    passed &= expectResult(
        assembler.accept("At 50"),
        AssemblyState::complete,
        "Channel 5 At 50");
    passed &= !assembler.hasPending();

    passed &= expectResult(
        assembler.accept("Canal 7"), AssemblyState::pending, "Canal 7");
    const std::string oversized(CommandAssembler::maxPendingLength + 1, 'x');
    passed &= expectResult(
        assembler.accept(oversized), AssemblyState::rejected, "");
    passed &= assembler.hasPending();
    passed &= expectResult(
        assembler.accept("al 70%"),
        AssemblyState::complete,
        "Canal 7 al 70%");

    for (int index = 0; index < 100; ++index)
    {
        passed &= expectResult(
            assembler.accept("unusable fragment"),
            AssemblyState::rejected,
            "");
    }
    passed &= !assembler.hasPending();

    return passed ? 0 : 1;
}
