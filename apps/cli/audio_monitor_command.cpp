/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "audio_monitor_command.h"

#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_device_enumerator.h"
#include "timmy/audio/audio_engine.h"
#include "timmy/audio/audio_meter.h"

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace timmy::cli {

    namespace {

        volatile std::sig_atomic_t stopRequested = 0;

        struct AudioMonitorOptions
        {
            std::optional<std::size_t> deviceIndex;
            std::size_t channelIndex = 0;
        };

        void handleInterrupt(int)
        {
            stopRequested = 1;
        }

        void printMonitorUsage()
        {
            std::cout
                << "Usage:\n"
                << "  timmy-cli audio monitor [options]\n\n"
                << "Options:\n"
                << "  --device N     Capture device runtime index.\n"
                << "                 Default: system default device.\n"
                << "  --channel N    Zero-based input channel index.\n"
                << "                 Default: 0.\n"
                << "  -h, --help     Show this help.\n";
        }

        bool parseIndex(
            std::string_view text,
            std::size_t& output)
        {
            if (text.empty())
            {
                return false;
            }

            std::size_t value = 0;

            const char* begin =
                text.data();

            const char* end =
                text.data() + text.size();

            const auto result =
                std::from_chars(
                    begin,
                    end,
                    value);

            if (result.ec != std::errc{}
                || result.ptr != end)
            {
                return false;
            }

            output = value;
            return true;
        }

        bool parseOptions(
            int argc,
            char* argv[],
            AudioMonitorOptions& options)
        {
            for (int index = 0;
                index < argc;
                ++index)
            {
                const std::string_view argument =
                    argv[index];

                if (argument == "--device")
                {
                    if (index + 1 >= argc)
                    {
                        std::cerr
                            << "Missing value for --device.\n";

                        return false;
                    }

                    std::size_t deviceIndex = 0;

                    if (!parseIndex(
                        argv[++index],
                        deviceIndex))
                    {
                        std::cerr
                            << "Invalid value for --device.\n";

                        return false;
                    }

                    options.deviceIndex =
                        deviceIndex;

                    continue;
                }

                if (argument == "--channel")
                {
                    if (index + 1 >= argc)
                    {
                        std::cerr
                            << "Missing value for --channel.\n";

                        return false;
                    }

                    std::size_t channelIndex = 0;

                    if (!parseIndex(
                        argv[++index],
                        channelIndex))
                    {
                        std::cerr
                            << "Invalid value for --channel.\n";

                        return false;
                    }

                    options.channelIndex =
                        channelIndex;

                    continue;
                }

                std::cerr
                    << "Unknown audio monitor option: "
                    << argument
                    << '\n';

                return false;
            }

            return true;
        }

    } // namespace

    int runAudioMonitor(
        int argc,
        char* argv[])
    {
        if (argc == 1)
        {
            const std::string_view argument =
                argv[0];

            if (argument == "--help"
                || argument == "-h")
            {
                printMonitorUsage();
                return 0;
            }
        }

        AudioMonitorOptions options;

        if (!parseOptions(
            argc,
            argv,
            options))
        {
            std::cerr << '\n';
            printMonitorUsage();
            return 2;
        }

        std::string deviceDescription =
            "system default";

        if (options.deviceIndex.has_value())
        {
            const auto devices =
                audio::enumerateCaptureDevices();

            if (!devices.ok())
            {
                std::cerr
                    << "Audio device discovery failed:\n"
                    << devices.diagnostic->message
                    << '\n';

                return 1;
            }

            if (*options.deviceIndex
                >= devices.devices.size())
            {
                std::cerr
                    << "Capture device index "
                    << *options.deviceIndex
                    << " is not available.\n";

                return 2;
            }

            const auto& device =
                devices.devices[
                    *options.deviceIndex];

            deviceDescription =
                "["
                + std::to_string(
                    device.runtimeIndex)
                + "] "
                + device.name;
        }

        stopRequested = 0;

        auto selector =
            std::make_shared<
            audio::AudioChannelSelector>(
                options.channelIndex);

        auto meter =
            std::make_shared<
            audio::AudioMeter>();

        selector->addConsumer(meter);

        audio::AudioEngine engine;
        engine.addConsumer(selector);

        audio::AudioCaptureConfig config;
        config.runtimeDeviceIndex =
            options.deviceIndex;

        const auto startResult =
            engine.start(config);

        if (!startResult.ok())
        {
            std::cerr
                << "Audio capture failed:\n"
                << startResult.diagnostic->message
                << '\n';

            return 1;
        }

        if (options.channelIndex
            >= startResult.format.channelCount)
        {
            std::cerr
                << "Channel index "
                << options.channelIndex
                << " is not available. "
                << "The capture device has "
                << startResult.format.channelCount
                << " channel(s).\n";

            engine.stop();
            return 2;
        }

        std::signal(
            SIGINT,
            handleInterrupt);

        std::cout
            << "TIMMY - Audio Monitor\n\n"
            << "Device: "
            << deviceDescription
            << '\n'
            << "Format: "
            << startResult.format.sampleRate
            << " Hz, "
            << startResult.format.channelCount
            << " channel(s)\n"
            << "Selected channel: "
            << options.channelIndex
            << "\n\n"
            << "Press Ctrl+C to stop.\n\n";

        std::cout
            << std::fixed
            << std::setprecision(4);

        while (!stopRequested)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(250));

            const auto snapshot =
                meter->snapshot();

            if (snapshot.channels.empty())
            {
                std::cout
                    << "\rWaiting for audio..."
                    << std::flush;

                continue;
            }

            const auto& level =
                snapshot.channels.front();

            std::cout
                << "\rPeak: "
                << level.peak
                << "  RMS: "
                << level.rms
                << "  Frames: "
                << snapshot.processedFrameCount
                << "      "
                << std::flush;
        }

        engine.stop();

        std::cout
            << "\nAudio monitor stopped.\n";

        return 0;
    }

} // namespace timmy::cli