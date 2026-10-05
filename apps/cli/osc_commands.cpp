/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "osc_commands.h"

#include "timmy/command/semantic_command_parser.h"
#include "timmy/core/command_dispatcher.h"
#include "timmy/transport/osc_command_emitter.h"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace timmy::cli {

    namespace {

        struct OscSendOptions
        {
            std::string host;
            std::uint32_t port = 0;
            std::string commandText;
        };

        // Prints help for the explicit OSC test-send path.
        void printOscUsage()
        {
            std::cout
                << "Usage:\n"
                << "  timmy-cli osc send --host <host> --port <port> "
                   "--command \"Channel 1 At 50\"\n\n"
                << "This path sends one explicitly supplied semantic command; "
                   "recognized voice commands are not sent.\n";
        }

        // Parses a complete unsigned decimal port value.
        std::optional<std::uint32_t> parsePort(std::string_view text)
        {
            std::uint32_t value = 0;
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (text.empty() || parsed.ec != std::errc{}
                || parsed.ptr != text.data() + text.size())
            {
                return std::nullopt;
            }
            return value;
        }

        // Parses the required named options without interpreting command syntax.
        std::optional<OscSendOptions> parseSendOptions(int argc, char* argv[])
        {
            OscSendOptions options;
            for (int index = 1; index < argc; ++index)
            {
                const std::string_view option = argv[index];
                if (index + 1 >= argc)
                {
                    return std::nullopt;
                }
                const std::string_view value = argv[++index];
                if (option == "--host")
                {
                    options.host = value;
                }
                else if (option == "--port")
                {
                    const auto port = parsePort(value);
                    if (!port) return std::nullopt;
                    options.port = *port;
                }
                else if (option == "--command")
                {
                    options.commandText = value;
                }
                else
                {
                    return std::nullopt;
                }
            }
            if (options.host.empty() || options.port == 0
                || options.commandText.empty())
            {
                return std::nullopt;
            }
            return options;
        }

    } // namespace

    // Parses and dispatches one explicit command through Core and OSC transport.
    int runOscCommand(int argc, char* argv[])
    {
        if (argc == 1 && (std::string_view(argv[0]) == "--help"
            || std::string_view(argv[0]) == "-h"))
        {
            printOscUsage();
            return 0;
        }
        if (argc < 1 || std::string_view(argv[0]) != "send")
        {
            printOscUsage();
            return 2;
        }
        const auto options = parseSendOptions(argc, argv);
        if (!options)
        {
            std::cerr << "Invalid OSC send options.\n";
            printOscUsage();
            return 2;
        }

        const command::SemanticCommandParser parser;
        const auto parsed = parser.parse(options->commandText);
        if (!parsed.command)
        {
            std::cerr << "Command is unsupported or invalid.\n";
            return 2;
        }

        auto profile = std::make_shared<transport::GenericOscCommandProfile>();
        auto emitter = std::make_shared<transport::OscCommandEmitter>(
            transport::OscDestination{options->host, options->port}, profile);
        core::CommandDispatcher dispatcher(emitter);
        const auto result = dispatcher.dispatch(*parsed.command);
        if (result.state != core::CommandDispatchState::succeeded)
        {
            std::cerr << "OSC send failed: " << result.diagnostic << '\n';
            return 1;
        }
        std::cout << "OSC command sent.\n";
        return 0;
    }

} // namespace timmy::cli
