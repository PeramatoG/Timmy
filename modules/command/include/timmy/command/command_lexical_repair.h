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

#include "timmy/command/command_profile.h"
#include <string>
#include <string_view>
#include <vector>

namespace timmy::command {
    struct CommandRepair
    {
        std::string original;
        std::string replacement;
        std::string category;
        std::string reason;
    };
    struct RepairedTranscript
    {
        std::string repairedText;
        std::vector<CommandRepair> repairs;
        [[nodiscard]] bool applied() const { return !repairs.empty(); }
    };
    // Stateless fragment transformation; never owns assembly or command context.
    class CommandLexicalRepair
    {
    public:
        explicit CommandLexicalRepair(SemanticParsingConfiguration configuration = {});
        [[nodiscard]] RepairedTranscript repair(std::string_view normalizedText) const;
    private:
        SemanticParsingConfiguration configuration_;
    };
}
