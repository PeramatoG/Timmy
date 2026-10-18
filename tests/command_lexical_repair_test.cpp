#include "timmy/command/command_lexical_repair.h"
#include "timmy/command/command_processor.h"
#include "timmy/command/command_assembler.h"
#include <iostream>
#include <stdexcept>

using namespace timmy::command;
namespace {
    void require(bool value, std::string_view message)
    {
        if (!value) throw std::runtime_error(std::string(message));
    }
}
int main()
{
    try {
        const CommandLexicalRepair repair;
        struct Example { const char* input; const char* repaired; const char* rendered; };
        constexpr Example examples[] = {
            {"fistur 5 a full", "fixture 5 a full", "Fixture 5 At 100"},
            {"fishtur 5 al 50", "fixture 5 al 50", "Fixture 5 At 50"},
            {"fix 5 al 50", "fixture 5 al 50", "Fixture 5 At 50"},
            {"apa 5 a full", "fixture 5 a full", "Fixture 5 At 100"},
            {"cha 13 al 50", "channel 13 al 50", "Channel 13 At 50"},
            {"chanel 13 al 50", "channel 13 al 50", "Channel 13 At 50"},
            {"can 25 al 50", "channel 25 al 50", "Channel 25 At 50"},
            {"gru 2 al 70", "group 2 al 70", "Group 2 At 70"},
            {"selecciona fishtur cinco", "selecciona fixture 5", "Select Fixture 5"},
            {"channel cinco at cincuenta", "channel 5 at 50", "Channel 5 At 50"},
            {"fixture cinco a full", "fixture 5 a full", "Fixture 5 At 100"},
            {"canal 25, 50.", "canal 25 al 50", "Channel 25 At 50"},
            {"canal 25,50", "canal 25 al 50", "Channel 25 At 50"},
            {"canal 25 , 0.", "canal 25 al 0", "Channel 25 At 0"},
            {"Canal 150, 100!", "Canal 150 al 100", "Channel 150 At 100"},
            {"canal, 35 al 50", "canal 35 al 50", "Channel 35 At 50"},
            {"canal 150, al 50", "canal 150 al 50", "Channel 150 At 50"},
            {"selecciona, canal 5", "selecciona canal 5", "Select Channel 5"},
            {"prueba, canal 1", "prueba canal 1", "Test Channel 1"},
            {"limpia, editor", "limpia editor", "Clear Editor"},
            {"selecciona, fishtur cinco", "selecciona fixture 5", "Select Fixture 5"},
            {"select the fishturs from 1 through 3", "select the fixtures from 1 through 3", "Select Fixture 1 Through 3"},
            {"apaga el can 5", "apaga el channel 5", "Channel 5 At 0"},
            {"test gru all", "test group all", "Test Group All"},
            {"home fix 5", "home fixture 5", "Home Fixture 5"},
            {"release cha 5", "release channel 5", "Release Channel 5"},
            {"select can 1 and remove can 2", "select channel 1 and remove channel 2", "Select Channel 1 - 2"}
        };
        for (const auto& example : examples) {
            const auto processed = CommandProcessor{}.process(example.input);
            require(processed.repair.repairedText == example.repaired, example.input);
            require(processed.validation.accepted && processed.rendering.text == example.rendered, example.input);
        }
        for (const auto text : {"canales 1, 5, 10", "canal 1, 5, 10", "canal 25, 50,",
                "canal 25, 101", "canal 25, 50 y 60", "canal 25, y 50",
                "channel 25, 50", "fixture 25, 50", "group 25, 50", "canal 25, 50 at 75",
                "selecciona canal 25, 50", "apaga canal 25, 50", "canal 25, 50 al 75",
                "fixtures 1, 3 y 5", "3, 5", ", 3 at 50", "and 3, 5", "del 3 al 5, 7", "Select Channels 1,3 and 5", "channel 1 through 3, 5 at 50",
                "channels 1 a 3, 5 al 50", "canal 5 al 50", "limpia editor", "cler all",
                "please fix 5", "hello remove fix 5", "fixture nonsense remove fix 5", "hello cha 13", "select chair 5", "fishtur five",
                "ch 5", "f 5", "g 5", "fi editor", "gr editor", "cl editor", "rel all", "blackout", "can123 5",
                "channel 5,", "channel 5 at 50,", "select fixture 0", "fishtur 0", "fishtur 4294967296",
                "canal 0005 al 0050", "fixture 1, and the 3", "fixture 1, and el 3"}) {
            const auto result = repair.repair(text);
            require(result.repairedText == text && !result.applied(), text);
        }
        require(repair.repair("channel 1 at 50, 60").repairedText == "channel 1 at 50 60", "intensity is not a selection list");
        require(repair.repair("fishtur 5,al 50").repairedText == "fixture 5 al 50", "attached punctuation");
        const auto notes = repair.repair("selecciona, fishtur 5");
        require(notes.repairs.size() == 2 && notes.repairs[0].category == "punctuation"
            && notes.repairs[1].original == "fishtur" && notes.repairs[1].replacement == "fixture", "repair diagnostics");
        const auto invalidZero = CommandProcessor{}.process("canal 0, 50");
        require(!invalidZero.validation.accepted, "invalid zero target remains rejected");
        const auto separator = repair.repair("canal 25, 50");
        require(separator.repairs.size() == 1 && separator.repairs[0].category == "asr_separator"
            && separator.repairs[0].replacement == "al", "bounded separator is observable");
        require(repair.repair("canal, 35, 50,").repairedText == "canal 35, 50,",
            "leading comma is neutral; trailing comma preserves a pending list");
        require(CommandAssembler{}.accept(repair.repair("canal, 35, 50,").repairedText).state == AssemblyState::pending,
            "ambiguous real list remains pending");
        const auto list = CommandProcessor{}.process("selecciona canales 1, 5, 10");
        require(list.validation.accepted && list.rendering.text == "Select Channel 1 + 5 + 10", "genuine list");
        const auto conjunction = CommandProcessor{}.process("selecciona canal 25 y 50");
        require(conjunction.validation.accepted && conjunction.rendering.text == "Select Channel 25 + 50", "explicit conjunction");
        auto configuration = SemanticParsingConfiguration{};
        configuration.vocabulary.insert(configuration.vocabulary.begin(), {"fishtur", "group"});
        require(CommandLexicalRepair{configuration}.repair("fishtur 5").repairedText == "fishtur 5", "exact custom alias wins");
        configuration.vocabulary.insert(configuration.vocabulary.begin(), {"fixture", "group"});
        require(!CommandLexicalRepair{configuration}.repair("fix 5").applied(), "conflicting role alias refuses repair");
        configuration.vocabulary.insert(configuration.vocabulary.begin(), {"canal", "group"});
        require(!CommandLexicalRepair{configuration}.repair("canal 25, 50").applied(), "exact canal alias wins");
        configuration.vocabulary.insert(configuration.vocabulary.begin(), {"al", "and"});
        require(!CommandLexicalRepair{configuration}.repair("canal 25, 50").applied(), "exact separator alias wins");
        auto profile = CommandProfile{};
        profile.validation.maxIdentifier = 4;
        const auto rejected = CommandProcessor{profile}.process("fishtur 5 al 50");
        require(rejected.repair.applied() && rejected.parsed.command && !rejected.validation.accepted
            && !rejected.rendering.text, "repair must not bypass validation");
        for (const auto text : {"fishtur 5 al 101", "home cha 5", "rel editor", "cl all"})
            require(!CommandProcessor{}.process(text).validation.accepted, text);
        const auto boundedRejected = CommandProcessor{profile}.process("canal 25, 50.");
        require(boundedRejected.repair.applied() && !boundedRejected.validation.accepted,
            "bounded separator cannot bypass validation");
        CommandAssembler assembler;
        require(assembler.accept(repair.repair("fishtur 5").repairedText).state == AssemblyState::pending, "pending repaired target");
        require(assembler.accept(repair.repair("al 50").repairedText).state == AssemblyState::complete, "pending repair preserves assembly");
        require(assembler.accept(repair.repair("fixtures 1,").repairedText).state == AssemblyState::pending, "pending list");
        require(assembler.accept(repair.repair("3, 5 at 50").repairedText).state == AssemblyState::complete, "numeric list continuation");
        require(repair.repair("canal , 35 al 50").repairedText == "canal 35 al 50", "spaced neutral comma");
        require(assembler.accept("fixture 1").state == AssemblyState::pending, "pending target");
        require(assembler.accept(repair.repair(", 3 at 50").repairedText).state == AssemblyState::complete, "leading list continuation");
        const std::string oversized(257, 'a');
        require(!repair.repair(oversized).applied(), "bounded scan");
        std::cout << "Contextual lexical repair tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
