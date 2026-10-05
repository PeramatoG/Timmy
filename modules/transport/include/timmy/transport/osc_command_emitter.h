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
#include "timmy/transport/osc_command_profile.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace timmy::transport {

    struct OscDestination
    {
        std::string host;
        std::uint32_t port = 0;
    };

    class IUdpSender
    {
    public:
        virtual ~IUdpSender();

        [[nodiscard]] virtual EmissionResult send(
            const OscDestination& destination,
            std::span<const std::uint8_t> payload) = 0;
    };

    class OscCommandEmitter final : public ICommandEmitter
    {
    public:
        OscCommandEmitter(OscDestination destination,
            std::shared_ptr<const IOscCommandProfile> profile);
        OscCommandEmitter(OscDestination destination,
            std::shared_ptr<const IOscCommandProfile> profile,
            std::unique_ptr<IUdpSender> sender);
        ~OscCommandEmitter() override;

        [[nodiscard]] EmissionResult emit(
            const command::SemanticCommand& command) override;

    private:
        OscDestination destination_;
        std::shared_ptr<const IOscCommandProfile> profile_;
        std::unique_ptr<IUdpSender> sender_;
    };

} // namespace timmy::transport
