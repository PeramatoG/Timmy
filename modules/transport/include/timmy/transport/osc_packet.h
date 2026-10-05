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

#include "timmy/transport/osc_message.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timmy::transport {

    struct OscPacketResult
    {
        std::optional<std::vector<std::uint8_t>> bytes;
        std::string diagnostic;
    };

    [[nodiscard]] OscPacketResult serializeOscMessage(const OscMessage& message);

} // namespace timmy::transport
