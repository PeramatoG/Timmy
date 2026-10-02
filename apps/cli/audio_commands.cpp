/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "audio_commands.h"

#include "timmy/audio/audio_device_enumerator.h"
#include "audio_monitor_command.h"

#include <iostream>
#include <string_view>

namespace timmy::cli {

    namespace {

        void printAudioUsage()
        {
            std::cout
                << "Usage:\n"
                << "  timmy-cli audio devices\n"
                << "  timmy-cli audio monitor [options]\n\n"
                << "Audio commands:\n"
                << "  devices    List available capture devices.\n"
                << "  monitor    Monitor audio input levels.\n";
        }

        int runAudioDevices()
        {
            const auto result =
                audio::enumerateCaptureDevices();

            if (!result.ok())
            {
                std::cerr
                    << "Audio device discovery failed:\n"
                    << result.diagnostic->message
                    << '\n';

                return 1;
            }

            std::cout << "Capture devices:\n";

            if (result.devices.empty())
            {
                std::cout
                    << "  No capture devices found.\n";

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

    } // namespace

    int runAudioCommand(
        int argc,
        char* argv[])
    {
        if (argc == 0)
        {
            printAudioUsage();
            return 0;
        }

        const std::string_view command =
            argv[0];

        if (command == "--help"
            || command == "-h")
        {
            printAudioUsage();
            return 0;
        }

        if (command == "devices")
        {
            return runAudioDevices();
        }

        if (command == "monitor")
        {
            return runAudioMonitor(
                argc - 1,
                argv + 1);
        }

        std::cerr
            << "Unknown audio command: "
            << command
            << "\n\n";

        printAudioUsage();

        return 2;
    }

} // namespace timmy::cli