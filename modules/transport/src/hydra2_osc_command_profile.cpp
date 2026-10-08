/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/transport/hydra2_osc_command_profile.h"
#include "timmy/command/command_processor.h"
#include <utility>

namespace timmy::transport {
    Hydra2OscCommandProfile::Hydra2OscCommandProfile(Hydra2OscOptions options)
        : options_(std::move(options)) {}

    // Compatibility entry point for callers that still hold a typed intent.
    // Console-language generation is delegated to the command renderer.
    OscMappingResult Hydra2OscCommandProfile::map(
        const command::SemanticCommand& commandValue) const
    {
        const auto rendered = command::renderHydra2Intent(commandValue, options_.preClear);
        if (!rendered.text) return {std::nullopt, rendered.diagnostic};
        return mapKeys(rendered.consoleKeys);
    }

    OscMappingResult Hydra2OscCommandProfile::mapRendered(const RenderedCommand& rendered) const
    {
        if (rendered.target != command::RenderingTarget::hydra2Keys)
            return {std::nullopt, "Hydra II requires its console-key renderer."};
        if (rendered.consoleKeys.empty())
            return {std::nullopt, "Hydra II rendered key sequence must not be empty."};
        return mapKeys(rendered.consoleKeys);
    }

    OscMappingResult Hydra2OscCommandProfile::mapKeys(const std::vector<std::string>& keys) const
    {
        OscExecutionPlan plan;
        for (const auto& key : keys)
        {
            const auto address = options_.keyAddresses.find(key);
            if (address == options_.keyAddresses.end())
                return {std::nullopt, "Hydra II key address is missing: " + key};
            plan.emplace_back(OscKeyPulse{address->second, options_.hold, options_.gap});
        }
        return {std::move(plan), {}};
    }
}
