/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace timmy::command {

    // Private grammar markers cannot originate from spoken text: the lexer
    // rejects their non-whitespace control-byte prefix in input.
    inline constexpr std::string_view canonicalArticle{"\x1f" "article"};
    inline constexpr std::string_view canonicalHomeTo{"\x1f" "home_to"};
    inline constexpr std::string_view canonicalScopedRemove{"\x1f" "scoped_remove"};

    struct CommandAlias
    {
        std::string spoken;
        std::string canonical;
    };
    [[nodiscard]] std::vector<CommandAlias> defaultCommandVocabulary();
    [[nodiscard]] std::vector<std::string> canonicalizeCommandText(std::string_view text,
        const std::vector<CommandAlias>& vocabulary);

    // Exact spoken token/phrase aliases only; unknown words are left unchanged
    // so the semantic grammar can reject them. No typo-distance matching.
    [[nodiscard]] std::vector<std::string> canonicalizeCommandText(std::string_view text);

    // The same alias inventory guides backend-neutral recognition hints.
    [[nodiscard]] const std::vector<std::string>& spokenCommandVocabulary();

} // namespace timmy::command
