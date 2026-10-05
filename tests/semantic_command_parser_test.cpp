/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/semantic_command_parser.h"

#include <cassert>
#include <string_view>

namespace {

    // Verifies one supported command and its transport-neutral description.
    void expectCommand(
        const timmy::command::SemanticCommandParser& parser,
        std::string_view text,
        std::string_view expectedDescription)
    {
        const auto result = parser.parse(text);
        assert(result.state == timmy::command::SemanticParseState::produced);
        assert(result.command.has_value());
        assert(timmy::command::describe(*result.command) == expectedDescription);
    }

    // Verifies that invalid or unsupported complete text produces no command.
    void expectRejected(
        const timmy::command::SemanticCommandParser& parser,
        std::string_view text)
    {
        const auto result = parser.parse(text);
        assert(result.state
            == timmy::command::SemanticParseState::unsupportedOrInvalid);
        assert(!result.command.has_value());
    }

} // namespace

// Exercises the deliberately bounded semantic lighting command grammar.
int main()
{
    const timmy::command::SemanticCommandParser parser;

    expectCommand(parser, "Canal 4 al 100%",
        "SetIntensity(Channel(4), 100%)");
    expectCommand(parser, "Channel 4 at 50",
        "SetIntensity(Channel(4), 50%)");
    expectCommand(parser, "Fixture 1 at full",
        "SetIntensity(Fixture(1), 100%)");
    expectCommand(parser, "Grupo 5 al 70%",
        "SetIntensity(Group(5), 70%)");
    expectCommand(parser, "Group 2 a tope",
        "SetIntensity(Group(2), 100%)");
    expectCommand(parser, "Channel 9 at 0",
        "SetIntensity(Channel(9), 0%)");
    expectCommand(parser, "Fixture 9 at 100",
        "SetIntensity(Fixture(9), 100%)");

    expectRejected(parser, "Channel 1 at 101");
    expectRejected(parser, "Channel 1 at 101%");
    expectRejected(parser, "Channel 0 at 50");
    expectRejected(parser, "Channel at 50");
    expectRejected(parser, "Channel banana at 50");
    expectRejected(parser, "Channel 1");
    expectRejected(parser, "background noise");
    expectRejected(parser, "Channel 1 through 4 at 50");
}
