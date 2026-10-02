/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_device_enumerator.h"

#include <miniaudio.h>

#include <string>
#include <utility>

namespace timmy::audio {

    namespace {

        std::string makeMiniaudioError(
            const std::string& operation,
            ma_result result)
        {
            return operation
                + " failed (miniaudio error "
                + std::to_string(static_cast<int>(result))
                + ")";
        }

    } // namespace

    AudioDeviceEnumerationResult enumerateCaptureDevices()
    {
        AudioDeviceEnumerationResult output;

        ma_context context{};

        const ma_result initResult =
            ma_context_init(nullptr, 0, nullptr, &context);

        if (initResult != MA_SUCCESS)
        {
            output.errorMessage =
                makeMiniaudioError(
                    "Audio context initialization",
                    initResult);

            return output;
        }

        ma_device_info* playbackDevices = nullptr;
        ma_uint32 playbackDeviceCount = 0;

        ma_device_info* captureDevices = nullptr;
        ma_uint32 captureDeviceCount = 0;

        const ma_result enumerationResult =
            ma_context_get_devices(
                &context,
                &playbackDevices,
                &playbackDeviceCount,
                &captureDevices,
                &captureDeviceCount);

        if (enumerationResult != MA_SUCCESS)
        {
            output.errorMessage =
                makeMiniaudioError(
                    "Audio device enumeration",
                    enumerationResult);

            ma_context_uninit(&context);
            return output;
        }

        output.devices.reserve(captureDeviceCount);

        for (ma_uint32 index = 0;
            index < captureDeviceCount;
            ++index)
        {
            AudioDeviceInfo info;

            info.runtimeIndex =
                static_cast<std::size_t>(index);

            info.name =
                captureDevices[index].name;

            info.isDefault =
                captureDevices[index].isDefault == MA_TRUE;

            output.devices.push_back(std::move(info));
        }

        ma_context_uninit(&context);

        return output;
    }

} // namespace timmy::audio
