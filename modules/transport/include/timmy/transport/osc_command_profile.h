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
#include "timmy/transport/osc_message.h"

#include <optional>
#include <string>

namespace timmy::transport {

    struct OscMappingResult
    {
        std::optional<OscMessage> message;
        std::string diagnostic;
    };

    class IOscCommandProfile
    {
    public:
        virtual ~IOscCommandProfile();

        [[nodiscard]] virtual OscMappingResult map(
            const command::SemanticCommand& command) const = 0;
    };

    class GenericOscCommandProfile final : public IOscCommandProfile
    {
    public:
        [[nodiscard]] OscMappingResult map(
            const command::SemanticCommand& command) const override;
    };

} // namespace timmy::transport
