/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "asr_commands.h"

#include "asr_listen_command.h"

#include <iostream>
#include <string_view>

namespace timmy::cli {

    namespace {

        // Prints the available speech recognition subcommands.
        void printAsrUsage()
        {
            std::cout
                << "Usage:\n"
                << "  timmy-cli asr listen --model <path> [options]\n\n"
                << "ASR commands:\n"
                << "  listen    Capture and transcribe microphone audio.\n";
        }

    } // namespace

    // Dispatches the requested speech recognition subcommand.
    int runAsrCommand(int argc, char* argv[])
    {
        if (argc == 0)
        {
            printAsrUsage();
            return 0;
        }

        const std::string_view command = argv[0];
        if (command == "--help" || command == "-h")
        {
            printAsrUsage();
            return 0;
        }

        if (command == "listen")
        {
            return runAsrListen(argc - 1, argv + 1);
        }

        std::cerr << "Unknown ASR command: " << command << "\n\n";
        printAsrUsage();
        return 2;
    }

} // namespace timmy::cli
