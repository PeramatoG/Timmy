/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/transcript_normalizer.h"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

    // Verifies one transcript while also checking that the raw input is retained.
    bool expectNormalization(
        const timmy::command::TranscriptNormalizer& normalizer,
        std::string_view input,
        std::string_view expected)
    {
        const auto result = normalizer.normalize(input);
        if (result.originalText == input && result.normalizedText == expected)
        {
            return true;
        }

        std::cerr << "Input: [" << input << "]\n"
                  << "Expected: [" << expected << "]\n"
                  << "Actual: [" << result.normalizedText << "]\n"
                  << "Retained raw input: "
                  << (result.originalText == input ? "yes" : "no") << '\n';
        return false;
    }

} // namespace

// Runs deterministic normalization contract tests without external services.
int main()
{
    const timmy::command::TranscriptNormalizer normalizer;
    bool passed = true;

    passed &= expectNormalization(
        normalizer, "  Canal 4   al 100%  ", "Canal 4 al 100%");
    passed &= expectNormalization(
        normalizer, "Canal 4 al 50 %.", "Canal 4 al 50%");
    passed &= expectNormalization(
        normalizer,
        "Selecciona   los canales del 1 al 10.",
        "Selecciona los canales del 1 al 10");
    passed &= expectNormalization(
        normalizer, "Grupo 5 al 70%", "Grupo 5 al 70%");
    passed &= expectNormalization(
        normalizer, "Canal 3 al 0", "Canal 3 al 0");
    passed &= expectNormalization(
        normalizer,
        "  Selecci\xC3\xB3n   r\xC3\xA1pida. ",
        "Selecci\xC3\xB3n r\xC3\xA1pida");
    passed &= expectNormalization(
        normalizer, "porcentaje cincuenta", "porcentaje 50");
    passed &= expectNormalization(
        normalizer, "canal50 %descuento", "canal50 %descuento");
    passed &= expectNormalization(normalizer, "50 %", "50%");
    passed &= expectNormalization(normalizer, "canal50 %", "canal50 %");
    passed &= expectNormalization(normalizer, "modelo50 %", "modelo50 %");
    passed &= expectNormalization(normalizer, "abc123 %", "abc123 %");
    passed &= expectNormalization(
        normalizer, "modelo50%plus", "modelo50%plus");
    passed &= expectNormalization(
        normalizer, "  Canales 1 , 3 y 5 al 70 % . ",
        "Canales 1, 3 y 5 al 70%");
    passed &= expectNormalization(
        normalizer, "Canales del 1 al 15 excepto 5 a 8 al 50%.",
        "Canales del 1 al 15 excepto 5 a 8 al 50%");
    passed &= expectNormalization(
        normalizer, "Fixtures 1 a 6 a home!", "Fixtures 1 a 6 a home");
    passed &= expectNormalization(normalizer, "Selecciona canales 1,",
        "Selecciona canales 1,");
    passed &= expectNormalization(normalizer, "Canales 1,", "Canales 1,");
    passed &= expectNormalization(normalizer, "3,", "3,");
    passed &= expectNormalization(normalizer, "3 through 5,", "3 through 5,");
    passed &= expectNormalization(normalizer, "and 3,", "and 3,");
    passed &= expectNormalization(normalizer, "except 5,", "except 5,");
    passed &= expectNormalization(normalizer, "Canal 5 al 50,", "Canal 5 al 50,");
    passed &= expectNormalization(normalizer, "Add 4,", "Add 4,");
    passed &= expectNormalization(normalizer, "Suma del 20 al 25,", "Suma del 20 al 25,");
    passed &= expectNormalization(normalizer, "Cler all,", "Cler all");
    passed &= expectNormalization(normalizer, "Test,", "Test");
    passed &= expectNormalization(normalizer, "ordinary speech,", "ordinary speech");
    passed &= expectNormalization(normalizer, "Clear editor,", "Clear editor");
    passed &= expectNormalization(normalizer, "   \t\r\n", "");

    // Exact cardinal vocabulary, including accented and case-folded ASR forms.
    const std::pair<std::string_view, std::string_view> words[] = {
        {"cero", "0"}, {"uno", "1"}, {"dos", "2"}, {"tres", "3"}, {"cuatro", "4"},
        {"cinco", "5"}, {"seis", "6"}, {"siete", "7"}, {"ocho", "8"}, {"nueve", "9"},
        {"diez", "10"}, {"once", "11"}, {"doce", "12"}, {"trece", "13"}, {"catorce", "14"},
        {"quince", "15"}, {"dieciséis", "16"}, {"dieciseis", "16"}, {"diecisiete", "17"},
        {"dieciocho", "18"}, {"diecinueve", "19"}, {"veinte", "20"}, {"veintiuno", "21"},
        {"veintidós", "22"}, {"veintitrés", "23"}, {"veinticuatro", "24"}, {"veinticinco", "25"},
        {"veintiséis", "26"}, {"veintisiete", "27"}, {"veintiocho", "28"}, {"veintinueve", "29"},
        {"treinta", "30"}, {"cuarenta", "40"}, {"cincuenta", "50"}, {"sesenta", "60"},
        {"setenta", "70"}, {"ochenta", "80"}, {"noventa", "90"}, {"cien", "100"},
        {"doscientos", "200"}, {"trescientos", "300"}, {"cuatrocientos", "400"},
        {"quinientos", "500"}, {"seiscientos", "600"}, {"setecientos", "700"},
        {"ochocientos", "800"}, {"novecientos", "900"}, {"VEINTIDÓS", "22"},
        {"zero", "0"}, {"one", "1"}, {"two", "2"}, {"three", "3"}, {"four", "4"},
        {"five", "5"}, {"six", "6"}, {"seven", "7"}, {"eight", "8"}, {"nine", "9"},
        {"ten", "10"}, {"eleven", "11"}, {"twelve", "12"}, {"thirteen", "13"},
        {"fourteen", "14"}, {"fifteen", "15"}, {"sixteen", "16"}, {"seventeen", "17"},
        {"eighteen", "18"}, {"nineteen", "19"}, {"twenty", "20"}, {"thirty", "30"},
        {"forty", "40"}, {"fifty", "50"}, {"sixty", "60"}, {"seventy", "70"},
        {"eighty", "80"}, {"ninety", "90"}, {"FIFTEEN", "15"}
    };
    for (const auto& [input, expected] : words)
        passed &= expectNormalization(normalizer, input, expected);

    const std::pair<std::string_view, std::string_view> commands[] = {
        {"Enciende canal uno", "Enciende canal 1"},
        {"grupo dos al cincuenta", "grupo 2 al 50"},
        {"Canal ciento cinco al cincuenta", "Canal 105 al 50"},
        {"Canal quinientos quince al cincuenta", "Canal 515 al 50"},
        {"Channel thirteen at fifty", "Channel 13 at 50"},
        {"Channel seventeen at seventy", "Channel 17 at 70"},
        {"canal quince al veinticinco", "canal 15 al 25"},
        {"canal treinta y cinco al cien", "canal 35 al 100"},
        {"canal ciento quince al setenta", "canal 115 al 70"},
        {"canal ciento cincuenta al cincuenta", "canal 150 al 50"},
        {"canal quinientos cincuenta al cincuenta", "canal 550 al 50"},
        {"canal novecientos noventa y nueve al cero", "canal 999 al 0"},
        {"channel twenty five at thirty", "channel 25 at 30"},
        {"channel one hundred five at fifty", "channel 105 at 50"},
        {"channel one hundred fifteen at seventy", "channel 115 at 70"},
        {"channel five hundred fifty at fifty", "channel 550 at 50"},
        {"channel nine hundred and ninety nine at zero", "channel 999 at 0"},
        {"canales del uno al quince al cincuenta", "canales del 1 al 15 al 50"},
        {"channels one through twenty five at fifty", "channels 1 through 25 at 50"},
        {"canales uno, quince y treinta y cinco al setenta", "canales 1, 15 y 35 al 70"},
        {"channels one, five and fifteen at seventy", "channels 1, 5 and 15 at 70"},
        {"channels one hundred and five and seven at fifty", "channels 105 and 7 at 50"},
        {"canales del uno al quince excepto cinco a ocho al cincuenta%", "canales del 1 al 15 excepto 5 a 8 al 50%"},
        {"channels one through fifteen except five to eight at fifty %.", "channels 1 through 15 except 5 to 8 at 50%"},
        {"canal ciento cinco al 50", "canal 105 al 50"},
        {"channel 15 at fifty", "channel 15 at 50"},
        {"canales 1, cinco y 15 al cincuenta", "canales 1, 5 y 15 al 50"},
        {"canal 00105 al 050%", "canal 00105 al 050%"},
        {"channels 1-5, 15 and 105 except 3 at 50%", "channels 1-5, 15 and 105 except 3 at 50%"},
        {"uno-cinco", "1-5"},
        {"  Canal CIENTO   CINCO al CINCUENTA % ! ", "Canal 105 al 50%"},
        {"uno,", "1,"}, {"and five,", "and 5,"}, {"except quince,", "except 15,"}
    };
    for (const auto& [input, expected] : commands)
    {
        passed &= expectNormalization(normalizer, input, expected);
        passed &= expectNormalization(normalizer, expected, expected);
    }

    // Malformed runs stay intact: no partial additive or digit interpretation.
    for (const std::string_view input : {
        "ciento", "cien cinco", "ciento cero", "treinta cinco", "treinta y cero",
        "treinta y", "treinta y quince", "ciento ciento cinco", "quinientos cien",
        "uno cinco", "one five", "twenty five six", "twenty zero", "hundred five",
        "zero hundred five", "ten hundred five", "one hundred hundred", "one hundred and",
        "one hundred zero", "one hundred and zero", "one hundred twenty five six",
        "one hundred 5", "ciento 5", "one 5", "105 cinco", "ciento five",
        "thirty cinco", "treinta y five", "one thousand five", "mil cinco",
        "dos millones cinco", "one point five", "uno coma cinco",
        "canal ciento cinco cinco al 50", "channel one hundred hundred at 50"})
        passed &= expectNormalization(normalizer, input, input);

    // Whole-token matching preserves unrelated words, articles and identifiers.
    for (const std::string_view input : {
        "someone stone money ninguno", "un canal una luz", "modeloquince", "channelone",
        "one_fixture", "five50", "fiftyish", "one's", "veintiunox", "abc123", "1.5", "50%"})
        passed &= expectNormalization(normalizer, input, input);

    // Number replacement preserves punctuation; existing terminal cleanup is optional.
    const timmy::command::TranscriptNormalizer keepPunctuation{{false}};
    passed &= expectNormalization(keepPunctuation,
        "(ciento cinco), [quince]; cincuenta%: one hundred five!",
        "(105), [15]; 50%: 105!");
    passed &= expectNormalization(keepPunctuation, "uno,cinco;quince.", "1,5;15.");
    passed &= expectNormalization(keepPunctuation, "ciento, cinco", "ciento, 5");
    passed &= expectNormalization(keepPunctuation,
        "'ciento cinco', \"fifty\"!", "'105', \"50\"!");

    return passed ? 0 : 1;
}
