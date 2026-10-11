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
#include "timmy/transport/hydra2_osc_command_profile.h"

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
            std::string profile = "generic";
            transport::Hydra2OscOptions hydra;
            bool hydraOptions = false;
            bool dryRun = false;
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
                << "Use --dry-run --command <text> to inspect processing without a destination.\n"
                << "Profiles: --profile generic (default) or hydra2.\n"
                << "Hydra II: --key-hold-ms <ms> --key-gap-ms <ms> --pre-clear\n"
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
                if (option == "--dry-run") { options.dryRun = true; continue; }
                if (option == "--pre-clear")
                {
                    options.hydra.preClear = true;
                    options.hydraOptions = true;
                    continue;
                }
                if (index + 1 >= argc)
                {
                    return std::nullopt;
                }
                const std::string_view value = argv[++index];
                if (option == "--profile")
                {
                    if (value != "generic" && value != "hydra2") return std::nullopt;
                    options.profile = value;
                }
                else if (option == "--key-hold-ms" || option == "--key-gap-ms")
                {
                    const auto timing = parsePort(value);
                    if (!timing) return std::nullopt;
                    options.hydraOptions = true;
                    if (option == "--key-hold-ms")
                        options.hydra.hold = std::chrono::milliseconds{*timing};
                    else options.hydra.gap = std::chrono::milliseconds{*timing};
                }
                else if (option == "--host")
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
            if ((!options.dryRun && (options.host.empty() || options.port == 0 || options.port > 65535))
                || options.commandText.empty()
                || (options.hydraOptions && options.profile != "hydra2"))
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

        const auto languageProfile = options->profile == "hydra2"
            ? command::hydra2CommandProfile(options->hydra.preClear) : command::CommandProfile{};
        const auto processed = command::CommandProcessor{languageProfile}.process(options->commandText);
        const auto& parsed = processed.parsed;
        std::cout << "HEARD: " << processed.transcript.originalText
            << "\nNORMALIZED: " << processed.transcript.normalizedText
            << "\nREPAIRED: " << processed.repair.repairedText << '\n';
        if (parsed.command) std::cout << "PARSED INTENT: " << command::describe(*parsed.command) << '\n';
        std::cout << "VALIDATION: " << (processed.validation.accepted ? "accepted" : "rejected") << '\n';
        if (processed.rendering.text) std::cout << "RENDERED COMMAND: " << *processed.rendering.text << '\n';
        if (!parsed.command || !processed.validation.accepted || !processed.rendering.text)
        {
            std::cerr << "Command is unsupported or invalid. "
                << processed.validation.diagnostic << " " << processed.rendering.diagnostic << '\n';
            return 2;
        }

        if (options->dryRun) { std::cout << "OUTPUT/STATUS: dry run; no command emitted.\n"; return 0; }

        std::shared_ptr<const transport::IOscCommandProfile> profile;
        if (options->profile == "hydra2")
            profile = std::make_shared<transport::Hydra2OscCommandProfile>(options->hydra);
        else profile = std::make_shared<transport::GenericOscCommandProfile>();
        auto emitter = std::make_shared<transport::OscCommandEmitter>(
            transport::OscDestination{options->host, options->port}, profile);
        core::CommandDispatcher dispatcher(emitter, languageProfile);
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
