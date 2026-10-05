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

#include <string>

namespace timmy::transport {

    enum class EmissionState
    {
        succeeded,
        failed
    };

    struct EmissionResult
    {
        EmissionState state{EmissionState::failed};
        std::string diagnostic;
    };

    class ICommandEmitter
    {
    public:
        virtual ~ICommandEmitter();

        [[nodiscard]]
        virtual EmissionResult emit(
            const command::SemanticCommand& command) = 0;
    };

} // namespace timmy::transport
