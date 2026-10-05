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
#include <string_view>

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
        normalizer, "porcentaje cincuenta", "porcentaje cincuenta");
    passed &= expectNormalization(
        normalizer, "canal50 %descuento", "canal50 %descuento");
    passed &= expectNormalization(
        normalizer, "modelo50%plus", "modelo50%plus");
    passed &= expectNormalization(normalizer, "   \t\r\n", "");

    return passed ? 0 : 1;
}
