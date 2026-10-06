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
#include <type_traits>
#include <utility>

namespace timmy::transport {
    Hydra2OscCommandProfile::Hydra2OscCommandProfile(Hydra2OscOptions options)
        : options_(std::move(options)) {}

    OscMappingResult Hydra2OscCommandProfile::map(
        const command::SemanticCommand& commandValue) const
    {
        std::vector<std::string> keys;
        if (options_.preClear) keys = {"C", "C"};
        std::visit([&](const command::SetIntensity& operation) {
            std::visit([&](const auto& target) {
                using Target = std::decay_t<decltype(target)>;
                if constexpr (std::is_same_v<Target, command::Channel>) keys.push_back("CH");
                else if constexpr (std::is_same_v<Target, command::Fixture>) keys.push_back("FXT");
                else keys.push_back("GRP");
                for (char digit : std::to_string(target.id)) keys.emplace_back(1, digit);
            }, operation.target);
            keys.push_back("AT");
            const int level = operation.level.percent();
            if (level == 100) keys.push_back("AT");
            else
            {
                keys.push_back(std::to_string(level / 10));
                keys.push_back(std::to_string(level % 10));
            }
        }, commandValue);
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
