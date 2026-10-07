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

#include "timmy/command/semantic_command_parser.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace timmy::command {

    enum class AssemblyState
    {
        pending,
        complete,
        rejected
    };

    struct AssemblyResult
    {
        AssemblyState state{AssemblyState::rejected};
        std::string text;
    };

    class CommandAssembler
    {
    public:
        static constexpr std::size_t maxPendingLength = SemanticCommandParser::maxCommandLength;

        [[nodiscard]]
        AssemblyResult accept(std::string_view normalizedFragment);

        void reset();

        [[nodiscard]]
        bool hasPending() const;

    private:
        std::string pending_;
    };

} // namespace timmy::command
