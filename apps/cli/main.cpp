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