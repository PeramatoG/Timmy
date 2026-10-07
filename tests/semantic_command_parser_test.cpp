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
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

    using namespace timmy::command;

    // Fail explicitly so these regression checks also run in Release builds.
    bool expect(bool condition, std::string_view diagnostic)
    {
        if (!condition)
        {
            std::cerr << diagnostic << '\n';
        }
        return condition;
    }

    bool expectCommand(const SemanticCommandParser& parser,
        std::string_view text, std::string_view expectedDescription)
    {
        const auto result = parser.parse(text);
        const auto classified = parser.classify(text);
        bool passed = expect(result.state == SemanticParseState::produced
                && result.command.has_value(),
            "Expected a semantic command for: " + std::string(text));
        passed &= expect(classified.state == CommandTextState::complete
                && classified.command.has_value(),
            "Expected complete shared grammar classification for: " + std::string(text));
        if (result.command)
        {
            const auto actual = describe(*result.command);
            passed &= expect(actual == expectedDescription,
                "Unexpected description for [" + std::string(text) + "]: " + actual);
        }
        if (classified.command)
        {
            passed &= expect(describe(*classified.command) == expectedDescription,
                "Shared grammar and semantic parser disagree for: " + std::string(text));
        }
        return passed;
    }

    bool expectIncomplete(const SemanticCommandParser& parser,
        std::string_view text, CommandTextState state)
    {
        const auto classified = parser.classify(text);
        const auto parsed = parser.parse(text);
        bool passed = expect(classified.state == state && !classified.command,
            "Unexpected incomplete classification for: " + std::string(text));
        passed &= expect(parsed.state == SemanticParseState::unsupportedOrInvalid
                && !parsed.command,
            "Incomplete or invalid text emitted a command: " + std::string(text));
        return passed;
    }

    struct CommandCase
    {
        std::string_view text;
        std::string_view description;
    };

} // namespace

// Verifies the bounded, shared selection grammar without a model or console.
int main()
{
    using namespace timmy::command;
    const SemanticCommandParser parser;
    bool passed = true;

    const CommandCase commands[] = {
        {"Canal 4 al 100%", "SetIntensity(Channel(4), 100%)"},
        {"Channel 4 at 50", "SetIntensity(Channel(4), 50%)"},
        {"Fixture 1 at full", "SetIntensity(Fixture(1), 100%)"},
        {"Grupo 5 al 70%", "SetIntensity(Group(5), 70%)"},
        {"Group 2 a tope", "SetIntensity(Group(2), 100%)"},
        {"Channel 9 at 0", "SetIntensity(Channel(9), 0%)"},
        {"Fixture 9 at 100", "SetIntensity(Fixture(9), 100%)"},
        {"Canal 5 al 50", "SetIntensity(Channel(5), 50%)"},
        {"Canal 5 al 50,", "SetIntensity(Channel(5), 50%)"},
        {"Channel 5 at 50 , ", "SetIntensity(Channel(5), 50%)"},
        {"Canal 5 a 50", "SetIntensity(Channel(5), 50%)"},
        {"Pon canal 5 al 50", "SetIntensity(Channel(5), 50%)"},
        {"Sube canal 5 al 50", "SetIntensity(Channel(5), 50%)"},
        {"Baja canal 5 al 20", "SetIntensity(Channel(5), 20%)"},
        {"Canales 1 a 10 al 50", "SetIntensity(Channel(1 through 10), 50%)"},
        {"Canales del 1 al 10 al 50", "SetIntensity(Channel(1 through 10), 50%)"},
        {"Canal 1 thru 10 al 50", "SetIntensity(Channel(1 through 10), 50%)"},
        {"Canales 1 hasta 10 al 50", "SetIntensity(Channel(1 through 10), 50%)"},
        {"Channels 1 through 10 at 50", "SetIntensity(Channel(1 through 10), 50%)"},
        {"Canales 1, 3 y 5 al 70", "SetIntensity(Channel(1, 3, 5), 70%)"},
        {"Set channels 1, 3 and 5 at 70", "SetIntensity(Channel(1, 3, 5), 70%)"},
        {"Canales 1 a 10 excepto 5 al 50", "SetIntensity(Channel(1 through 10 except 5), 50%)"},
        {"Canales 1 a 15 excepto 5 a 8 al 50", "SetIntensity(Channel(1 through 15 except 5 through 8), 50%)"},
        {"Channels 1 through 15 except 5 through 8 at full", "SetIntensity(Channel(1 through 15 except 5 through 8), 100%)"},
        {"Fixtures 1 a 6 al 50", "SetIntensity(Fixture(1 through 6), 50%)"},
        {"Groups 1 and 3 at 25", "SetIntensity(Group(1, 3), 25%)"},
        {"Selecciona canal 5", "SelectSelection(Channel(5))"},
        {"Selecciona canales 1 a 10", "SelectSelection(Channel(1 through 10))"},
        {"Selecciona fixtures 1, 3 y 5", "SelectSelection(Fixture(1, 3, 5))"},
        {"Select channels 1 through 10", "SelectSelection(Channel(1 through 10))"},
        {"Select groups 1 through 4 except 2", "SelectSelection(Group(1 through 4 except 2))"},
        {"Libera canal 5", "ReleaseSelection(Channel(5))"},
        {"Suelta canal 5", "ReleaseSelection(Channel(5))"},
        {"Libera canales 1 a 10", "ReleaseSelection(Channel(1 through 10))"},
        {"Release channel 5", "ReleaseSelection(Channel(5))"},
        {"Release channels 1 through 10", "ReleaseSelection(Channel(1 through 10))"},
        {"Limpia editor", "ClearEditor()"},
        {"Borra editor", "ClearEditor()"},
        {"Borra todo el editor", "ClearEditor()"},
        {"Clear editor", "ClearEditor()"},
        {"Test canal 1", "TestSelection(Channel(1))"},
        {"Prueba canal 1", "TestSelection(Channel(1))"},
        {"Comprueba canal 1", "TestSelection(Channel(1))"},
        {"Test canales 1 a 10", "TestSelection(Channel(1 through 10))"},
        {"Test fixture 5", "TestSelection(Fixture(5))"},
        {"Prueba fixtures 1 a 6", "TestSelection(Fixture(1 through 6))"},
        {"Test grupo 3", "TestSelection(Group(3))"},
        {"Test canal 21", "TestSelection(Channel(21))"},
        {"test", "AdvanceTest()"},
        {"siguiente", "AdvanceTest()"},
        {"next", "AdvanceTest()"},
        {"siguiente test", "AdvanceTest()"},
        {"test siguiente", "AdvanceTest()"},
        {"next test", "AdvanceTest()"},
        {"Siguiente canal", "NavigateSelection(Channel, Next)"},
        {"Canal siguiente", "NavigateSelection(Channel, Next)"},
        {"Next channel", "NavigateSelection(Channel, Next)"},
        {"Canal anterior", "NavigateSelection(Channel, Previous)"},
        {"Anterior canal", "NavigateSelection(Channel, Previous)"},
        {"Previous channel", "NavigateSelection(Channel, Previous)"},
        {"Next fixture", "NavigateSelection(Fixture, Next)"},
        {"Fixture anterior", "NavigateSelection(Fixture, Previous)"},
        {"Siguiente grupo", "NavigateSelection(Group, Next)"},
        {"Previous group", "NavigateSelection(Group, Previous)"},
        {"Prev fixture", "NavigateSelection(Fixture, Previous)"},
        {"Home fixture 5", "HomeSelection(Fixture(5))"},
        {"Fixture 5 a home", "HomeSelection(Fixture(5))"},
        {"Lleva fixture 5 a home", "HomeSelection(Fixture(5))"},
        {"Home fixtures 1 a 6", "HomeSelection(Fixture(1 through 6))"},
        {"Fixtures 1 a 6 a home", "HomeSelection(Fixture(1 through 6))"},
        {"Home fixtures 1, 3 and 5", "HomeSelection(Fixture(1, 3, 5))"},
    };
    for (const auto& entry : commands)
    {
        passed &= expectCommand(parser, entry.text, entry.description);
    }


    const CommandCase naturalCommands[] = {
        {"dame los canales del 1 al 10", "SelectSelection(Channel(1 through 10))"},
        {"dame los canales del 1 al 10 y me quitas el 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"canales 1 al 10 excepto 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"canales 1 al 10 menos el 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"canales 1 al 10 salvo el 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"dame canales 1 al 10 menos 5 al 8", "SelectSelection(Channel(1 through 10 except 5 through 8))"},
        {"channels 1 through 10 except 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"channels 1 through 10 but not the 6", "SelectSelection(Channel(1 through 10 except 6))"},
        {"dame canales 1 al 10 y añade el 15", "SelectSelection(Channel(1 through 10, 15))"},
        {"channels 1 through 10 plus 15", "SelectSelection(Channel(1 through 10, 15))"},
        {"channels 1 through 10 plus 15 except 6", "SelectSelection(Channel(1 through 10, 15 except 6))"},
        {"select channels 1 and add channels 20 al 25", "SelectSelection(Channel(1, 20 through 25))"},
        {"Canales 1 a 15 excepto 5", "SelectSelection(Channel(1 through 15 except 5))"},
        {"Canales 1 a 15 excepto 5 a 8", "SelectSelection(Channel(1 through 15 except 5 through 8))"},
        {"añade canal 4", "ModifySelection(Add, Channel(4))"},
        {"quita fixture 7", "ModifySelection(Remove, Fixture(7))"},
        {"add channels del 20 al 25", "ModifySelection(Add, Channel(20 through 25))"},
        {"remove groups 5 through 8", "ModifySelection(Remove, Group(5 through 8))"},
        {"add channels 1 through 4294967295", "ModifySelection(Add, Channel(1 through 4294967295))"},
        {"Canales 1 a 10 y añade 15 al 50", "SelectSelection(Channel(1 through 10, 15 through 50))"},
    };
    for (const auto& entry : naturalCommands)
        passed &= expectCommand(parser, entry.text, entry.description);

    const std::string_view selectors[] = {"selecciona", "dame", "coge", "toma", "marca", "select"};
    for (const auto alias : selectors)
        passed &= expectCommand(parser, std::string(alias) + " los canales del 1 al 10",
            "SelectSelection(Channel(1 through 10))");

    const std::string_view additions[] = {
        "añade", "anade", "agrega", "mete", "incluye", "suma", "súmale", "sumale", "add", "plus", "include"
    };
    for (const auto alias : additions)
    {
        passed &= expectCommand(parser, std::string(alias) + " canal 4", "ModifySelection(Add, Channel(4))");
        passed &= expectCommand(parser, "channels 1 through 10 and " + std::string(alias) + " the 15",
            "SelectSelection(Channel(1 through 10, 15))");
    }
    const std::string_view removals[] = {
        "quita", "saca", "resta", "excluye", "menos", "excepto", "salvo", "remove", "subtract", "exclude", "except", "but not"
    };
    for (const auto alias : removals)
    {
        passed &= expectCommand(parser, std::string(alias) + " fixture 7", "ModifySelection(Remove, Fixture(7))");
        passed &= expectCommand(parser, "channels 1 through 10 " + std::string(alias) + " the 6",
            "SelectSelection(Channel(1 through 10 except 6))");
    }
    const CommandCase scopedCommands[] = {
        {"limpia selección", "ClearSelection()"}, {"limpia la selección", "ClearSelection()"},
        {"limpia seleccion", "ClearSelection()"}, {"borra selección", "ClearSelection()"},
        {"quita la selección", "ClearSelection()"}, {"clear selection", "ClearSelection()"},
        {"limpia todo", "ClearEditor()"}, {"limpia el editor", "ClearEditor()"},
        {"borra todo", "ClearEditor()"}, {"borra el editor", "ClearEditor()"},
        {"quita todo", "ClearEditor()"}, {"vacía el editor", "ClearEditor()"},
        {"vacia el editor", "ClearEditor()"}, {"clear all", "ClearEditor()"},
        {"cler all", "ClearEditor()"}, {"clear editor", "ClearEditor()"},
        {"libera todo", "ReleaseEditor()"}, {"suelta todo", "ReleaseEditor()"},
        {"libera el editor", "ReleaseEditor()"}, {"release all", "ReleaseEditor()"},
        {"release editor", "ReleaseEditor()"},
        {"AÑADE CANAL 4", "ModifySelection(Add, Channel(4))"},
        {"VACÍA EL EDITOR", "ClearEditor()"}, {"LIMPIA LA SELECCIÓN", "ClearSelection()"},
    };
    for (const auto& entry : scopedCommands)
        passed &= expectCommand(parser, entry.text, entry.description);

    const std::string_view incompleteRanges[] = {
        "Selecciona canales del 1 al", "dame canales 1 al", "add channel del 20 al",
        "remove fixtures del 5 al", "channels 1 through 10 plus", "channels 1 through 10 and me remove"
    };
    for (const auto text : incompleteRanges)
        passed &= expectIncomplete(parser, text, CommandTextState::pending);

    const std::string_view invalidNatural[] = {
        "Canal 5 al 50,,", "Channel 5 at 50, ,", "cler", "cleer all", "clera all", "limpa todo", "reset all", "clear editor now",
        "except all", "but not all", "subtract all", "exclude editor", "remove all", "menos todo",
        "salvo editor", "resta todo", "saca todo", "quitas todo", "exclude selection",
        "select article channel 4", "home_to fixture 4", "select me channel 4", "me remove channel 4",
        "select the the channel 4", "add the the 4", "select channels 1 plus the the 4",
        "channels 1 through 10 plus fixture 15", "channels 1 through 10 except 6 plus 15",
        "channels 1 through 10 except 6 except 7", "add channel 4 except 2",
        "remove channel 4 plus 2", "add channel 0", "remove fixture 9 through 2",
        "add channel 4294967296", "remove fixture 5 through 4294967296", "remove group 1 at 50"
    };
    for (const auto text : invalidNatural)
        passed &= expectIncomplete(parser, text, CommandTextState::invalid);

    const std::string_view pending[] = {
        "Canal 5", "Canales 1, 3 y 5", "Canales 1 a 10",
        "Canales del 1 al 10", "Fixtures 1 a 6", "Group 3",
        "Channel 1 through 4294967295", "Canales 1 a", "Canal 5 al",
        "Channels 1,", "Canales 1 a 10 excepto", "Pon canal 5",
        "Selecciona", "Test canales", "select channel 5,", "add channel 5,", "remove fixture 7,"
    };
    for (const auto text : pending)
    {
        passed &= expectIncomplete(parser, text, CommandTextState::pending);
    }

    const std::string_view invalid[] = {
        "", "background noise", "Channel 1 at 101", "Channel 1 at 101%",
        "Channel 1 at -1", "Channel 1 at 50.5", "Channel 1 at 50%%",
        "Channel 1 at banana", "Channel 1 at 4294967296",
        "Channel 0 at 50", "Channel -1 at 50", "Channel 4294967296 at 50",
        "Channel at 50", "Channel banana at 50", "Channel 1 at 50 extra",
        "Channels 10 through 1 at 50", "Channels 1 through 0 at 50",
        "Channels 1 through 4294967296 at 50", "Channels 1,,3 at 50",
        "Channels 1 and and 3 at 50", "Channels ,1 at 50",
        "Channels 1 through 2 through 3 at 50", "Channels 1 except 0 at 50",
        "Channels 1 except 8 through 5 at 50", "Channels 1, fixture 2 at 50",
        "Channels 1 except 2 except 3 at 50", "Select channel 0",
        "Release channel -1", "Test channel 4294967296", "Test channel 1 at 50",
        "Home channel 5", "Home group 5", "Channel 5 a home",
        "Home fixture 5 position", "Home fixture 5 color", "Home fixture 5 gobo",
        "reset", "borra", "clear", "limpia", "reset editor",
 "Clear editor now", "Borra todo el show",
        "Next channel 5", "Previous channel 5", "Next cue", "Previous cue",
        "GO", "BACK", "PAUSE", "Record cue 1", "Update cue 1",
        "Delete cue 1", "Fixture 5 color red", "Macro 1", "Sync"
    };
    for (const auto text : invalid)
    {
        passed &= expectIncomplete(parser, text, CommandTextState::invalid);
    }
    passed &= expectIncomplete(parser,
        std::string(SemanticCommandParser::maxCommandLength + 1, 'x'),
        CommandTextState::invalid);
    const std::string longestTarget = "Channel "
        + std::string(SemanticCommandParser::maxCommandLength - 9, '0') + "1";
    passed &= expectIncomplete(parser, longestTarget, CommandTextState::pending);
    passed &= expectIncomplete(parser, longestTarget + "0", CommandTextState::invalid);
    const std::string longestCommand = "Channel "
        + std::string(SemanticCommandParser::maxCommandLength - 15, '0') + "1 at 50";
    passed &= expectCommand(parser, longestCommand, "SetIntensity(Channel(1), 50%)");
    passed &= expectIncomplete(parser, longestCommand + " ", CommandTextState::invalid);

    // A large range has one compact span, never an expanded identifier list.
    const auto large = parser.parse("Channels 1 through 4294967295 at 50");
    passed &= expect(large.command.has_value(), "Maximum uint32 range was rejected");
    if (large.command && std::holds_alternative<SetIntensity>(*large.command))
    {
        const auto& selection = std::get<SetIntensity>(*large.command).target;
        passed &= expect(selection.included.size() == 1
                && selection.included.front().first == 1
                && selection.included.front().last == std::numeric_limits<std::uint32_t>::max()
                && selection.excluded.empty() && !selection.singleTarget(),
            "Large range did not remain a compact selection");
    }
    else
    {
        passed &= expect(false, "Large range did not produce SetIntensity");
    }

    const auto relativeLarge = parser.parse("add channels 1 through 4294967295");
    if (relativeLarge.command && std::holds_alternative<ModifySelection>(*relativeLarge.command))
    {
        const auto& selection = std::get<ModifySelection>(*relativeLarge.command).target;
        passed &= expect(selection.included.size() == 1
                && selection.included.front().first == 1
                && selection.included.front().last == std::numeric_limits<std::uint32_t>::max()
                && selection.excluded.empty(), "A relative operation expanded a compact range");
    }
    else passed &= expect(false, "Large relative operation was not produced");

    std::string relativeList = "add channels ";
    for (std::size_t index = 1; index <= TargetSelection::maxTerms; ++index)
    {
        if (index != 1) relativeList += ',';
        relativeList += std::to_string(index);
    }
    passed &= expect(parser.parse(relativeList).command.has_value(),
        "Maximum relative selection term count was rejected");
    relativeList += ",33";
    passed &= expectIncomplete(parser, relativeList, CommandTextState::invalid);

    // Bound the semantic term count independently of the command text length.
    std::string list = "Select channels ";
    for (std::size_t index = 1; index <= TargetSelection::maxTerms; ++index)
    {
        if (index != 1) list += ',';
        list += std::to_string(index);
    }
    const auto bounded = parser.parse(list);
    passed &= expect(bounded.command.has_value(), "Maximum selection term count was rejected");
    if (bounded.command && std::holds_alternative<SelectSelection>(*bounded.command))
    {
        passed &= expect(std::get<SelectSelection>(*bounded.command).target.included.size()
                == TargetSelection::maxTerms,
            "The selection term limit changed the requested list");
    }
    list += ",33";
    passed &= expectIncomplete(parser, list, CommandTextState::invalid);

    std::string exclusions = "Select channels 1 through 32 except ";
    for (std::size_t index = 1; index < TargetSelection::maxTerms; ++index)
    {
        if (index != 1) exclusions += ',';
        exclusions += std::to_string(index);
    }
    const auto boundedExclusions = parser.parse(exclusions);
    passed &= expect(boundedExclusions.command.has_value(),
        "Maximum combined included and excluded term count was rejected");
    exclusions += ",32";
    passed &= expectIncomplete(parser, exclusions, CommandTextState::invalid);

    TargetSelection mixed{TargetType::channel, {{1, 10}}, {}};
    for (std::size_t index = 1; index < TargetSelection::maxTerms; ++index)
    {
        mixed.excluded.push_back({static_cast<std::uint32_t>(index),
            static_cast<std::uint32_t>(index)});
    }
    passed &= expect(mixed.isValid(), "Inclusive and excluded terms did not share the limit");
    mixed.excluded.push_back({32, 32});
    passed &= expect(!mixed.isValid(), "The semantic term bound was not enforced");

    const TargetSelection channel{Channel{5}};
    const TargetSelection fixture{Fixture{7}};
    const TargetSelection group{LightingTarget{Group{9}}};
    passed &= expect(channel.isValid() && channel.singleTarget()
            && std::holds_alternative<Channel>(*channel.singleTarget())
            && std::get<Channel>(*channel.singleTarget()).id == 5,
        "Legacy Channel conversion lost its single target");
    passed &= expect(fixture.isValid() && fixture.singleTarget()
            && std::holds_alternative<Fixture>(*fixture.singleTarget())
            && std::get<Fixture>(*fixture.singleTarget()).id == 7,
        "Legacy Fixture conversion lost its single target");
    passed &= expect(group.isValid() && group.singleTarget()
            && std::holds_alternative<Group>(*group.singleTarget())
            && std::get<Group>(*group.singleTarget()).id == 9,
        "Legacy Group conversion lost its single target");
    passed &= expect(!TargetSelection{}.isValid(), "An empty selection was valid");
    passed &= expect(!TargetSelection{Channel{0}}.isValid(), "A zero identifier was valid");
    passed &= expect(!TargetSelection{TargetType::channel, {{10, 1}}}.isValid(),
        "A descending range was valid");
    passed &= expect(!TargetSelection{TargetType::channel, {{1, 10}}, {{5, 0}}}.isValid(),
        "An invalid excluded range was valid");
    passed &= expect(!TargetSelection{static_cast<TargetType>(99), {{1, 1}}}.isValid(),
        "An unknown target type was valid");
    passed &= expect(!TargetSelection{TargetType::channel, {{1, 1}, {3, 3}}}.singleTarget(),
        "A list incorrectly became a Generic OSC single target");
    passed &= expect(!TargetSelection{TargetType::channel, {{1, 1}}, {{3, 3}}}.singleTarget(),
        "An excluded selection incorrectly became a Generic OSC single target");
    passed &= expect(describe(TargetSelection{TargetType::fixture,
                {{1, 3}, {5, 5}}, {{2, 2}, {6, 8}}})
            == "Fixture(1 through 3, 5 except 2, 6 through 8)",
        "Mixed selection description lost ranges or exclusions");
    const auto zero = IntensityLevel::fromPercent(0);
    const auto full = IntensityLevel::fromPercent(100);
    passed &= expect(!IntensityLevel::fromPercent(-1)
            && !IntensityLevel::fromPercent(101)
            && zero && zero->percent() == 0 && full && full->percent() == 100,
        "Semantic intensity bounds changed");

    return passed ? 0 : 1;
}
