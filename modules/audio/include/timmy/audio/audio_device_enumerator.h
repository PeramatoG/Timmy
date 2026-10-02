#pragma once

#include "timmy/audio/audio_device_info.h"

#include <string>
#include <vector>

namespace timmy::audio {

    struct AudioDeviceEnumerationResult
    {
        std::vector<AudioDeviceInfo> devices;
        std::string errorMessage;

        [[nodiscard]] bool ok() const noexcept
        {
            return errorMessage.empty();
        }
    };

    [[nodiscard]]
    AudioDeviceEnumerationResult enumerateCaptureDevices();

} // namespace timmy::audio