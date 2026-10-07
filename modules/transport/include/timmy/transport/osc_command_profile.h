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

#include "timmy/transport/command_emitter.h"
#include "timmy/transport/osc_message.h"

#include <optional>
#include <chrono>
#include <variant>
#include <vector>
#include <string>

namespace timmy::transport {

    // A pulse owns both edges; execution cannot advance before release.
    struct OscKeyPulse
    {
        std::string address;
        std::chrono::milliseconds hold{30};
        std::chrono::milliseconds gap{30};
    };

    using OscExecutionStep = std::variant<OscMessage, OscKeyPulse>;
    using OscExecutionPlan = std::vector<OscExecutionStep>;

    struct OscMappingResult
    {
        std::optional<OscExecutionPlan> plan;
        std::string diagnostic;
    };

    class IOscCommandProfile
    {
    public:
        virtual ~IOscCommandProfile();
        // Compatibility for generic typed OSC. Console-key profiles override
        // this boundary and consume the renderer's output without interpreting intent.
        [[nodiscard]] virtual OscMappingResult mapRendered(const RenderedCommand& rendered) const
        {
            return map(rendered.intent);
        }

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
