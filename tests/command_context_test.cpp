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

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace {

    using namespace timmy::command;

    bool expect(bool condition, std::string_view diagnostic)
    {
        if (!condition) std::cerr << diagnostic << '\n';
        return condition;
    }

    bool expectCommand(const SemanticCommandParser& parser, std::string_view text,
        CommandContext& context, std::string_view description,
        std::optional<TargetType> explicitType = {})
    {
        const auto priorType = context.lastExplicitTargetType;
        const auto classified = parser.classify(text, context);
        const auto parsed = parser.parse(text, context);
        bool passed = expect(classified.state == CommandTextState::complete && classified.command
                && parsed.state == SemanticParseState::produced && parsed.command,
            "Expected a complete contextual command for: " + std::string(text));
        passed &= expect(classified.explicitTargetType == explicitType
                && parsed.explicitTargetType == explicitType,
            "Incorrect explicit target metadata for: " + std::string(text));
        passed &= expect(context.lastExplicitTargetType == priorType,
            "Parsing mutated linguistic context for: " + std::string(text));
        if (classified.command)
            passed &= expect(describe(*classified.command) == description,
                "Incorrect contextual classification for: " + std::string(text));
        if (parsed.command)
            passed &= expect(describe(*parsed.command) == description,
                "Incorrect contextual parse for: " + std::string(text));
        return passed;
    }

    bool expectRejected(const SemanticCommandParser& parser, std::string_view text,
        CommandContext& context, CommandTextState state = CommandTextState::invalid)
    {
        const auto priorType = context.lastExplicitTargetType;
        const auto classified = parser.classify(text, context);
        const auto parsed = parser.parse(text, context);
        bool passed = expect(classified.state == state && !classified.command
                && !classified.explicitTargetType,
            "Incorrect contextual rejection for: " + std::string(text));
        passed &= expect(parsed.state == SemanticParseState::unsupportedOrInvalid
                && !parsed.command && !parsed.explicitTargetType,
            "Rejected contextual text emitted a semantic command: " + std::string(text));
        context.observe(parsed);
        passed &= expect(context.lastExplicitTargetType == priorType,
            "Rejected text changed linguistic context: " + std::string(text));
        return passed;
    }

} // namespace

int main()
{
    using namespace timmy::command;
    const SemanticCommandParser parser;
    CommandContext context;
    bool passed = true;

    const std::string_view relative[] = {
        "súmale el 4", "sumale el 4", "añade el 4", "add 4",
        "quita el 10", "saca el 10", "menos el 10", "remove 10",
        "suma del 20 al 25", "quita del 5 al 8"
    };
    for (const auto text : relative) passed &= expectRejected(parser, text, context);

    // Explicit successful commands publish metadata; the caller chooses when
    // to observe it, so preview/classification never commits conversational state.
    passed &= expectCommand(parser, "dame canales 1 al 10", context,
        "SelectSelection(Channel(1 through 10))", TargetType::channel);
    passed &= expect(!context.lastExplicitTargetType,
        "Preview established a target type before observation");
    context.observe(parser.parse("dame canales 1 al 10", context));
    passed &= expect(context.lastExplicitTargetType == TargetType::channel,
        "Explicit channel selection did not establish context");

    struct RelativeCase
    {
        std::string_view text;
        SelectionModification modification;
        std::uint32_t first;
        std::uint32_t last;
    };
    const RelativeCase modifications[] = {
        {"súmale el 4", SelectionModification::add, 4, 4},
        {"sumale el 4", SelectionModification::add, 4, 4},
        {"añade el 4", SelectionModification::add, 4, 4},
        {"add 4", SelectionModification::add, 4, 4},
        {"quita el 10", SelectionModification::remove, 10, 10},
        {"saca el 10", SelectionModification::remove, 10, 10},
        {"menos el 10", SelectionModification::remove, 10, 10},
        {"remove 10", SelectionModification::remove, 10, 10},
        {"suma del 20 al 25", SelectionModification::add, 20, 25},
        {"quita del 5 al 8", SelectionModification::remove, 5, 8},
        {"add 1 through 4294967295", SelectionModification::add, 1, 4294967295U}
    };
    for (const auto& entry : modifications)
    {
        const auto result = parser.parse(entry.text, context);
        const auto classified = parser.classify(entry.text, context);
        passed &= expect(result.command && std::holds_alternative<ModifySelection>(*result.command)
                && classified.state == CommandTextState::complete
                && !result.explicitTargetType && !classified.explicitTargetType,
            "Expected an elided relative modification for: " + std::string(entry.text));
        if (result.command && std::holds_alternative<ModifySelection>(*result.command))
        {
            const auto& modification = std::get<ModifySelection>(*result.command);
            passed &= expect(modification.modification == entry.modification
                    && modification.target.type == TargetType::channel
                    && modification.target.included.size() == 1
                    && modification.target.included.front().first == entry.first
                    && modification.target.included.front().last == entry.last
                    && modification.target.excluded.empty(),
                "Relative modification lost its included compact range: " + std::string(entry.text));
        }
        context.observe(result);
        passed &= expect(context.lastExplicitTargetType == TargetType::channel,
            "Elided follow-up changed the explicit target context");
    }

    passed &= expectCommand(parser, "quita fixture 7", context,
        "ModifySelection(Remove, Fixture(7))", TargetType::fixture);
    context.observe(parser.parse("quita fixture 7", context));
    passed &= expect(context.lastExplicitTargetType == TargetType::fixture,
        "Explicit fixture edit did not replace channel context");
    passed &= expectCommand(parser, "add 4", context, "ModifySelection(Add, Fixture(4))");

    passed &= expectCommand(parser, "grupo 3 al 50", context,
        "SetIntensity(Group(3), 50%)", TargetType::group);
    context.observe(parser.parse("grupo 3 al 50", context));
    passed &= expect(context.lastExplicitTargetType == TargetType::group,
        "Explicit group intensity did not replace fixture context");
    passed &= expectCommand(parser, "remove 10", context, "ModifySelection(Remove, Group(10))");

    // The last explicit type survives editor commands and incomplete/invalid
    // attempts mentioning another type. Ambiguous mixed types never publish it.
    const std::string_view editorCommands[] = {
        "clear selection", "clear all", "release all", "test", "next"
    };
    for (const auto text : editorCommands)
    {
        const auto result = parser.parse(text, context);
        passed &= expect(result.state == SemanticParseState::produced && result.command
                && !result.explicitTargetType,
            "Editor/test command exposed target metadata: " + std::string(text));
        context.observe(result);
        passed &= expect(context.lastExplicitTargetType == TargetType::group,
            "Editor/test command changed the last explicit target type");
    }
    passed &= expectRejected(parser, "channel 2", context, CommandTextState::pending);
    passed &= expectRejected(parser, "select fixture", context, CommandTextState::pending);
    passed &= expectRejected(parser, "fixture 0 at 50", context);
    passed &= expectRejected(parser, "select channels 1 and fixture 2", context);
    passed &= expectRejected(parser, "add 4 except 5", context);
    passed &= expectRejected(parser, "remove 4 except 5", context);

    context.observe({SemanticParseState::produced, {}, TargetType::fixture});
    passed &= expect(context.lastExplicitTargetType == TargetType::group,
        "A produced result without a command was allowed to establish context");
    context.observe({SemanticParseState::unsupportedOrInvalid,
        SemanticCommand{SelectSelection{Fixture{4}}}, TargetType::fixture});
    passed &= expect(context.lastExplicitTargetType == TargetType::group,
        "An unsuccessful result was allowed to establish context");
    context.observe({SemanticParseState::produced,
        SemanticCommand{SelectSelection{Fixture{4}}}, static_cast<TargetType>(99)});
    passed &= expect(context.lastExplicitTargetType == TargetType::group,
        "An unknown target type was allowed to establish context");
    context.reset();
    passed &= expect(!context.lastExplicitTargetType, "Context reset retained a target type");
    for (const auto text : relative) passed &= expectRejected(parser, text, context);
    passed &= expectCommand(parser, "añade canal 4", context,
        "ModifySelection(Add, Channel(4))", TargetType::channel);

    return passed ? 0 : 1;
}
