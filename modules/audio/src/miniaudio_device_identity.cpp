/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "miniaudio_device_identity.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace timmy::audio::detail {

    namespace {

        constexpr char kHexDigits[] = "0123456789abcdef";

        // Encodes fixed binary data without relying on the device ID union layout.
        std::string encodeHex(
            const ma_uint8* bytes,
            std::size_t byteCount)
        {
            std::string output;
            output.reserve(byteCount * 2);

            for (std::size_t index = 0; index < byteCount; ++index)
            {
                const ma_uint8 byte = bytes[index];
                output.push_back(kHexDigits[(byte >> 4U) & 0x0FU]);
                output.push_back(kHexDigits[byte & 0x0FU]);
            }

            return output;
        }

        // Encodes a 32-bit integer in fixed-width hexadecimal form.
        std::string encodeUint32(ma_uint32 value)
        {
            std::string output(8, '0');

            for (std::size_t index = 0; index < output.size(); ++index)
            {
                const auto shift = static_cast<unsigned int>(
                    (output.size() - index - 1) * 4);
                output[index] = kHexDigits[(value >> shift) & 0x0FU];
            }

            return output;
        }

        // Encodes the WASAPI UTF-16 identifier as fixed-width code units.
        std::string encodeWasapiId(
            const ma_wchar_win32* value,
            std::size_t capacity)
        {
            std::string output;

            for (std::size_t index = 0;
                index < capacity && value[index] != 0;
                ++index)
            {
                const ma_uint32 codeUnit =
                    static_cast<ma_uint32>(value[index]);

                for (int shift = 12; shift >= 0; shift -= 4)
                {
                    output.push_back(
                        kHexDigits[(codeUnit >> shift) & 0x0FU]);
                }
            }

            return output;
        }

        // Copies a native string without reading beyond its fixed-size field.
        std::string copyBoundedString(
            const char* value,
            std::size_t capacity)
        {
            std::size_t length = 0;

            while (length < capacity && value[length] != '\0')
            {
                ++length;
            }

            return std::string(value, length);
        }

        // Creates an identity only when both canonical components are present.
        std::optional<AudioDeviceId> makeIdentity(
            std::string_view backend,
            std::string value)
        {
            if (value.empty())
            {
                return std::nullopt;
            }

            return AudioDeviceId{ std::string(backend), std::move(value) };
        }

    } // namespace

    // Converts a supported miniaudio device identity to Timmy's canonical form.
    std::optional<AudioDeviceId> makeAudioDeviceId(
        ma_backend backend,
        const ma_device_id& nativeId)
    {
        switch (backend)
        {
            case ma_backend_wasapi:
                return makeIdentity(
                    "wasapi",
                    encodeWasapiId(nativeId.wasapi, 64));
            case ma_backend_dsound:
                return makeIdentity("directsound", encodeHex(nativeId.dsound, 16));
            case ma_backend_winmm:
                return makeIdentity("winmm", encodeUint32(nativeId.winmm));
            case ma_backend_coreaudio:
                return makeIdentity(
                    "coreaudio",
                    copyBoundedString(nativeId.coreaudio, 256));
            case ma_backend_pulseaudio:
                return makeIdentity(
                    "pulseaudio",
                    copyBoundedString(nativeId.pulse, 256));
            case ma_backend_alsa:
                return makeIdentity(
                    "alsa",
                    copyBoundedString(nativeId.alsa, 256));
            default:
                return std::nullopt;
        }
    }

    // Finds a native device by an exact canonical backend and value match.
    const ma_device_id* findCaptureDeviceId(
        ma_backend backend,
        const ma_device_info* devices,
        ma_uint32 deviceCount,
        const AudioDeviceId& requestedId)
    {
        for (ma_uint32 index = 0; index < deviceCount; ++index)
        {
            const auto candidate = makeAudioDeviceId(backend, devices[index].id);

            if (candidate.has_value() && *candidate == requestedId)
            {
                return &devices[index].id;
            }
        }

        return nullptr;
    }

} // namespace timmy::audio::detail
