/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_lexicon.h"
#include "timmy/command/semantic_command_parser.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using namespace timmy::command;

    bool expect(bool condition, std::string_view diagnostic)
    {
        if (!condition) std::cerr << diagnostic << '\n';
        return condition;
    }

    struct LexicalCase
    {
        std::string_view spoken;
        std::vector<std::string> canonical;
    };

} // namespace

int main()
{
    using namespace timmy::command;
    bool passed = true;
    const std::string article{canonicalArticle};
    const std::string homeTo{canonicalHomeTo};
    const std::string scopedRemove{canonicalScopedRemove};

    // Canonicalization preserves the grammar's structure and identifier text.
    const LexicalCase cases[] = {
        {"Dame los canales del 1 al 10 y me quitas el 6",
            {"select", article, "channels", "del", "1", "al", "10",
                "and", "me", "remove", article, "6"}},
        {"Select the channels from 1 through 10 but not 6",
            {"select", article, "channels", "from", "1", "through", "10",
                "remove", "6"}},
        {"Coge toma marca selecciona", {"select", "select", "select", "select"}},
        {"Añade anade agrega mete incluye suma súmale sumale add plus include",
            {"add", "add", "add", "add", "add", "add", "add", "add", "add", "add", "add"}},
        {"Quita saca resta excluye menos excepto salvo remove subtract exclude except",
            {scopedRemove, "remove", "remove", "remove", "remove", "remove", "remove",
                "remove", "remove", "remove", "remove"}},
        {"AÑADE canal 4", {"add", "channel", "4"}},
        {"SÚMALE el 4", {"add", article, "4"}},
        {"VACÍA la SELECCIÓN", {"clear", article, "selection"}},
        {"vacia la seleccion", {"clear", article, "selection"}},
        {"Cler all", {"clear", "all"}},
        {"Suelta los grupos", {"release", article, "groups"}},
        {"Lleva fixture 5 a home", {homeTo, "fixture", "5", "a", "home"}},
        {"Pon canal 5 al 50%", {"set", "channel", "5", "al", "50%"}},
        {"Prueba fixture 5 siguiente anterior", {"test", "fixture", "5", "next", "previous"}},
        {"Canales 1 hasta 10 thru 15 to 20", {"channels", "1", "through", "10",
            "through", "15", "through", "20"}},
        {"\tChannels 1,3\nAND 5  ", {"channels", "1", ",", "3", "and", "5"}},
        {"clearer clera clre borar CANALESX", {"clearer", "clera", "clre", "borar", "canalesx"}},
        {"but never not", {"but", "never", "not"}},
        {"ÁÉÍÓÚÑ", {"áéíóúñ"}},
        {"", {}},
        {" \t\n", {}}
    };
    for (const auto& entry : cases)
    {
        passed &= expect(canonicalizeCommandText(entry.spoken) == entry.canonical,
            "Unexpected lexical canonicalization for: " + std::string(entry.spoken));
    }

    // Unrecognized punctuation survives lexical processing and is rejected by
    // the grammar; only commas act as selection-list separators here.
    passed &= expect(canonicalizeCommandText("select channel 1.")
            == std::vector<std::string>{"select", "channel", "1."},
        "The lexicon silently removed an unsupported period");
    const SemanticCommandParser parser;
    const std::string_view malformed[] = {
        "select channel 1.", "select channel 1;", "select channel (1)",
        "select channels 1/3", "select channels 1,,3", "clre all", "clera all",
        "clearer all", "borar todo", "channels 1 through 10 but never not 6",
        "select article channel 4", "home_to fixture 4", "scoped_remove all",
        "remove all", "except editor", "saca todo", "but not selection", "quitas todo"
    };
    for (const auto text : malformed)
    {
        const auto result = parser.classify(text);
        passed &= expect(result.state == CommandTextState::invalid && !result.command,
            "Unknown spelling or punctuation emitted a command: " + std::string(text));
    }
    passed &= expect(parser.classify("select channels 1,3").state == CommandTextState::complete,
        "Comma-separated selection stopped working after lexical canonicalization");
    passed &= expect(parser.classify("cler all").state == CommandTextState::complete,
        "The explicit cler alias was rejected");

    // Internal markers cannot be injected through the spoken-text boundary,
    // even if a caller knows their spelling and control-byte prefix.
    const std::string injectedArticle = "select " + article + " channel 4";
    const std::string injectedHomeTo = homeTo + " fixture 4 a home";
    const std::string injectedScopedRemove = scopedRemove + " all";
    for (const auto& injected : {injectedArticle, injectedHomeTo, injectedScopedRemove})
    {
        const auto result = parser.classify(injected);
        passed &= expect(result.state == CommandTextState::invalid && !result.command,
            "An injected private grammar marker emitted a command");
        passed &= expect(canonicalizeCommandText(injected).empty(),
            "The lexicon accepted an unsupported control-byte prefix");
    }

    // Recognition consumes the same spoken inventory, including phrase aliases
    // and both supported Spanish spellings rather than just canonical tokens.
    const auto& vocabulary = spokenCommandVocabulary();
    const std::string_view required[] = {
        "dame", "añade", "anade", "súmale", "sumale", "vacía", "vacia",
        "selección", "seleccion", "cler", "but not", "me", "quitas", "home", "thru"
    };
    for (const auto word : required)
    {
        passed &= expect(std::find(vocabulary.begin(), vocabulary.end(), word) != vocabulary.end(),
            "Recognition vocabulary omitted spoken alias: " + std::string(word));
    }
    passed &= expect(&vocabulary == &spokenCommandVocabulary(),
        "Recognition vocabulary did not provide a stable immutable inventory");

    return passed ? 0 : 1;
}
