#pragma once

#include <cstddef>
#include <string>

namespace timmy::audio {

    struct AudioDeviceInfo
    {
        std::size_t runtimeIndex = 0;
        std::string name;
        bool isDefault = false;
    };

} // namespace timmy::audio