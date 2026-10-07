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

#include "timmy/command/semantic_command.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace timmy::command {

    enum class SemanticParseState
    {
        produced,
        unsupportedOrInvalid
    };

    struct SemanticParseResult
    {
        SemanticParseState state{SemanticParseState::unsupportedOrInvalid};
        std::optional<SemanticCommand> command;
    };

    // Both assembly and semantic parsing use this one grammar classification.
    enum class CommandTextState { complete, pending, invalid };
    struct CommandTextResult
    {
        CommandTextState state{CommandTextState::invalid};
        std::optional<SemanticCommand> command;
    };

    class SemanticCommandParser
    {
    public:
        static constexpr std::size_t maxCommandLength = 256;

        [[nodiscard]] CommandTextResult classify(std::string_view text) const;
        [[nodiscard]] SemanticParseResult parse(std::string_view completeText) const;
    };

} // namespace timmy::command
