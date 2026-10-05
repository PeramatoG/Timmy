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

    class SemanticCommandParser
    {
    public:
        [[nodiscard]] SemanticParseResult parse(std::string_view completeText) const;
    };

} // namespace timmy::command
