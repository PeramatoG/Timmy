/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_recognition_guidance.h"

#include "grammar-parser.h"

namespace timmy::asr::detail {

    namespace {

        constexpr const char* lightingCommandPrompt =
            "Lighting commands. Channel, fixture, group, canal, grupo, at, "
            "al, full, select, selecciona, set, pon, up, sube, down, baja. "
            "Examples: Canal 4 al 100%. Grupo 5 al 70%. Selecciona los "
            "canales del 20 al 30. Canal 1, Canal 2 y Canal 3 al 40%.";

        constexpr const char* lightingCommandGrammar = R"GBNF(
root ::= " " command punctuation?
command ::= selection | assignment | adjustment
selection ::= select-word " " article? (target-list | target-range)
assignment ::= set-prefix? (target-list | target-range) level?
adjustment ::= ((target-list | target-range) " ")? direction (" " level-value)?
set-prefix ::= set-word " "
target-list ::= target (list-separator target)*
target-range ::= plural-target " " range-prefix? number " " range-connector " " number
target ::= target-name " " number
level ::= level-separator level-value
level-value ::= full | number percent?
select-word ::= "Select" | "select" | "Selecciona" | "selecciona" | "Seleccionar" | "seleccionar"
set-word ::= "Set" | "set" | "Pon" | "pon" | "Poner" | "poner"
direction ::= "Up" | "up" | "Sube" | "sube" | "Down" | "down" | "Baja" | "baja"
target-name ::= "Channel" | "channel" | "Canal" | "canal" | "Fixture" | "fixture" | "Group" | "group" | "Grupo" | "grupo"
plural-target ::= "Channels" | "channels" | "Canales" | "canales" | "Fixtures" | "fixtures" | "Groups" | "groups" | "Grupos" | "grupos"
article ::= ("the" | "el" | "la" | "los" | "las") " "
range-prefix ::= ("from" | "del" | "de") " "
range-connector ::= "to" | "through" | "a" | "al"
list-separator ::= ", " | " and " | " y "
level-separator ::= " at " | " to " | " a " | " al " | ", at " | ", a " | ", al "
full ::= "Full" | "full" | "a tope" | "completo" | "completa"
percent ::= "%" | " percent" | " por ciento"
number ::= [0-9]+ | number-word
number-word ::= "zero" | "one" | "two" | "three" | "four" | "five" | "six" | "seven" | "eight" | "nine" | "ten" | "eleven" | "twelve" | "thirteen" | "fourteen" | "fifteen" | "sixteen" | "seventeen" | "eighteen" | "nineteen" | "twenty" | "thirty" | "forty" | "fifty" | "sixty" | "seventy" | "eighty" | "ninety" | "hundred" | "cero" | "uno" | "una" | "dos" | "tres" | "cuatro" | "cinco" | "seis" | "siete" | "ocho" | "nueve" | "diez" | "once" | "doce" | "trece" | "catorce" | "quince" | "dieciséis" | "dieciseis" | "diecisiete" | "dieciocho" | "diecinueve" | "veinte" | "treinta" | "cuarenta" | "cincuenta" | "sesenta" | "setenta" | "ochenta" | "noventa" | "cien" | "ciento"
punctuation ::= "." | "!" | "?"
)GBNF";

    } // namespace

    // Builds the fixed bilingual prompt and grammar for lighting commands.
    WhisperRecognitionGuidance makeLightingCommandRecognitionGuidance()
    {
        return {
            lightingCommandPrompt,
            lightingCommandGrammar,
            "root",
            20.0F
        };
    }

    // Checks that guidance has a prompt and a parseable named start rule.
    bool isValidWhisperRecognitionGuidance(
        const WhisperRecognitionGuidance& guidance)
    {
        if (guidance.initialPrompt.empty()
            || guidance.grammar.empty()
            || guidance.startRule.empty()
            || guidance.grammarPenalty < 0.0F)
        {
            return false;
        }

        const auto parsed = grammar_parser::parse(guidance.grammar.c_str());
        return !parsed.rules.empty()
            && parsed.symbol_ids.contains(guidance.startRule);
    }

} // namespace timmy::asr::detail
