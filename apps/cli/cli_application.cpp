/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "cli_application.h"

#include "audio_commands.h"

#include <iostream>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace timmy::cli {

    namespace {

        void configureConsole()
        {
#ifdef _WIN32
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
#endif
        }

        void printUsage()
        {
            std::cout
                << "TIMMY - The Imaginary Man, Minding You\n\n"
                << "Usage:\n"
                << "  timmy-cli <command> [options]\n\n"
                << "Commands:\n"
                << "  audio    Audio device and monitoring tools.\n\n"
                << "Run 'timmy-cli <command> --help' "
                "for command-specific help.\n";
        }

    } // namespace

    int runCli(
        int argc,
        char* argv[])
    {
        configureConsole();

        if (argc <= 1)
        {
            printUsage();
            return 0;
        }

        const std::string_view command =
            argv[1];

        if (command == "--help"
            || command == "-h")
        {
            printUsage();
            return 0;
        }

        if (command == "audio")
        {
            return runAudioCommand(
                argc - 2,
                argv + 2);
        }

        std::cerr
            << "Unknown command: "
            << command
            << "\n\n";

        printUsage();

        return 2;
    }

} // namespace timmy::cli