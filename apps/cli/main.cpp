#include "timmy/audio/audio_device_enumerator.h"

#include <iostream>

int main()
{
    std::cout
        << "TIMMY - The Imaginary Man, Minding You\n\n";

    const auto result =
        timmy::audio::enumerateCaptureDevices();

    if (!result.ok())
    {
        std::cerr
            << "Audio device discovery failed:\n"
            << result.errorMessage
            << '\n';

        return 1;
    }

    std::cout << "Capture devices:\n";

    if (result.devices.empty())
    {
        std::cout << "  No capture devices found.\n";
        return 0;
    }

    for (const auto& device : result.devices)
    {
        std::cout
            << "  ["
            << device.runtimeIndex
            << "] "
            << device.name;

        if (device.isDefault)
        {
            std::cout << " [default]";
        }

        std::cout << '\n';
    }

    return 0;
}