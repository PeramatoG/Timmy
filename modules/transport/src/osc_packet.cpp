/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/transport/osc_packet.h"

#include <bit>
#include <cstdint>
#include <type_traits>

namespace timmy::transport {

    namespace {

        // Appends an OSC string including its terminator and four-byte padding.
        void appendOscString(std::vector<std::uint8_t>& output,
            const std::string& value)
        {
            output.insert(output.end(), value.begin(), value.end());
            output.push_back(0);
            while (output.size() % 4U != 0U)
            {
                output.push_back(0);
            }
        }

        // Appends a 32-bit OSC value in network byte order.
        void appendWord(std::vector<std::uint8_t>& output, std::uint32_t value)
        {
            output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
            output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
            output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
            output.push_back(static_cast<std::uint8_t>(value & 0xffU));
        }

    } // namespace

    // Serializes one message according to the OSC 1.0 message wire format.
    OscPacketResult serializeOscMessage(const OscMessage& message)
    {
        if (message.address.empty() || message.address.front() != '/')
        {
            return {std::nullopt, "OSC address must start with '/'."};
        }
        if (message.address.find('\0') != std::string::npos)
        {
            return {std::nullopt, "OSC address must not contain a null byte."};
        }

        std::string tags = ",";
        for (const auto& argument : message.arguments)
        {
            tags.push_back(std::visit([](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::int32_t>)
                {
                    return 'i';
                }
                else if constexpr (std::is_same_v<Value, float>)
                {
                    return 'f';
                }
                else
                {
                    return 's';
                }
            }, argument));
        }

        std::vector<std::uint8_t> bytes;
        appendOscString(bytes, message.address);
        appendOscString(bytes, tags);
        for (const auto& argument : message.arguments)
        {
            const bool valid = std::visit([&bytes](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, std::int32_t>)
                {
                    appendWord(bytes, static_cast<std::uint32_t>(value));
                }
                else if constexpr (std::is_same_v<Value, float>)
                {
                    appendWord(bytes, std::bit_cast<std::uint32_t>(value));
                }
                else
                {
                    if (value.find('\0') != std::string::npos) return false;
                    appendOscString(bytes, value);
                }
                return true;
            }, argument);
            if (!valid)
            {
                return {std::nullopt,
                    "OSC string arguments must not contain a null byte."};
            }
        }
        return {std::move(bytes), {}};
    }

} // namespace timmy::transport
