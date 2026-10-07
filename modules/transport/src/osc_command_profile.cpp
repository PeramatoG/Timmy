/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/transport/osc_command_profile.h"

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>

namespace timmy::transport {

    // Allows profiles to be destroyed through the replaceable mapping contract.
    IOscCommandProfile::~IOscCommandProfile() = default;

    // Maps the current semantic intensity commands to the generic test profile.
    OscMappingResult GenericOscCommandProfile::map(
        const command::SemanticCommand& commandValue) const
    {
        return std::visit([](const auto& operation) -> OscMappingResult {
            using Operation = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<Operation, command::SetIntensity>)
            {
                const auto singleTarget = operation.target.singleTarget();
                if (!singleTarget)
                {
                    return OscMappingResult{std::nullopt,
                        "The generic OSC profile supports only single-target intensity commands."};
                }
                return std::visit([&operation](const auto& target) {
                    using Target = std::decay_t<decltype(target)>;
                    OscMessage message;
                    const auto level = static_cast<std::int32_t>(
                        operation.level.percent());
                    if constexpr (std::is_same_v<Target, command::Channel>)
                    {
                        if (target.id > static_cast<std::uint32_t>(
                                std::numeric_limits<std::int32_t>::max()))
                        {
                            return OscMappingResult{std::nullopt,
                                "Channel identifier exceeds the generic OSC integer range."};
                        }
                        message.address = "/timmy/channel/intensity";
                        message.arguments.emplace_back(static_cast<std::int32_t>(target.id));
                        message.arguments.emplace_back(level);
                    }
                    else if constexpr (std::is_same_v<Target, command::Fixture>)
                    {
                        message.address = "/timmy/fixture/"
                            + std::to_string(target.id) + "/intensity";
                        message.arguments.emplace_back(level);
                    }
                    else
                    {
                        message.address = "/timmy/group/intensity/"
                            + std::to_string(target.id);
                        message.arguments.emplace_back(level);
                    }
                    return OscMappingResult{OscExecutionPlan{std::move(message)}, {}};
                }, *singleTarget);
            }
            return OscMappingResult{
                std::nullopt,
                "The configured OSC profile does not support this semantic command."
            };
        }, commandValue);
    }

} // namespace timmy::transport
