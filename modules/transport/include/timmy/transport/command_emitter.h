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

    struct RenderedCommand
    {
        std::string text;
        command::CommandIntent intent;
        command::RenderingTarget target{command::RenderingTarget::nativeText};
        std::vector<std::string> consoleKeys{};
    };

    class ICommandEmitter
    {
    public:
        virtual ~ICommandEmitter();

        // Text transports can override this boundary. Existing typed OSC
        // adapters retain their typed compatibility contracts.
        [[nodiscard]] virtual EmissionResult emitRendered(const RenderedCommand& rendered)
        {
            return emit(rendered.intent);
        }

        [[nodiscard]]
        virtual EmissionResult emit(const command::SemanticCommand&)
        {
            return {EmissionState::failed, "This emitter requires a rendered command."};
        }
    };

} // namespace timmy::transport
