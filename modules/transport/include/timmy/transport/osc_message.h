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

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace timmy::transport {

    using OscArgument = std::variant<std::int32_t, float, std::string>;

    struct OscMessage
    {
        std::string address;
        std::vector<OscArgument> arguments;
    };

} // namespace timmy::transport
