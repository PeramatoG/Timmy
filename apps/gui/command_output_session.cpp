/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "command_output_session.h"

#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <string_view>
#include <utility>

namespace timmy::gui {

    namespace {

        bool whitespace(char character)
        {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        }

        std::string_view trimmed(std::string_view value)
        {
            while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
            while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
            return value;
        }

        bool parseUnsigned(std::string_view text, std::uint32_t& value)
        {
            if (text.empty()) return false;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
        }

    } // namespace

    CommandOutputSession::CommandOutputSession(EmitterFactory factory)
        : factory_(std::move(factory))
    {
    }

    CommandOutputSession::~CommandOutputSession() { stop(); }

    CommandOutputStartResult CommandOutputSession::start(
        const CommandOutputConfiguration& configuration, Observer observer)
    {
        std::lock_guard lock(mutex_);
        if (armed_) return {false, "Command output is already active. Disarm it before arming again."};
        if (!configuration.enabled) return {true, {}};

        if (configuration.profile != OutputProfile::hydra2
            && configuration.profile != OutputProfile::genericOsc)
            return {false, "Select a supported command output profile."};

        const auto host = trimmed(configuration.host);
        if (host.empty()) return {false, "Command output host is required."};
        for (const auto character : host)
        {
            if (character == '\0' || whitespace(character))
                return {false, "Command output host must not contain whitespace or NUL characters."};
        }

        std::uint32_t port = 0;
        if (!parseUnsigned(configuration.port, port) || port == 0 || port > 65535)
            return {false, "Command output port must be an integer between 1 and 65535."};

        transport::Hydra2OscOptions hydraOptions;
        if (configuration.profile == OutputProfile::hydra2)
        {
            std::uint32_t hold = 0;
            std::uint32_t gap = 0;
            if (!parseUnsigned(configuration.keyHoldMs, hold) || hold > maximumHydraTimingMs)
                return {false, "Hydra key hold ms must be an integer between 0 and "
                    + std::to_string(maximumHydraTimingMs) + "."};
            if (!parseUnsigned(configuration.keyGapMs, gap) || gap > maximumHydraTimingMs)
                return {false, "Hydra key gap ms must be an integer between 0 and "
                    + std::to_string(maximumHydraTimingMs) + "."};
            hydraOptions.hold = std::chrono::milliseconds{hold};
            hydraOptions.gap = std::chrono::milliseconds{gap};
            hydraOptions.preClear = configuration.preClear;
        }

        try
        {
            if (executor_)
            {
                if (*configuration_ != configuration)
                    return {false, "Command output configuration is frozen until listening stops."};
                if (!executor_->resume(std::move(observer)))
                    return {false, "Command output is stopping."};
                armed_ = true;
                return {true, {}};
            }
            std::shared_ptr<const transport::IOscCommandProfile> profile;
            if (configuration.profile == OutputProfile::hydra2)
                profile = std::make_shared<transport::Hydra2OscCommandProfile>(std::move(hydraOptions));
            else
                profile = std::make_shared<transport::GenericOscCommandProfile>();

            transport::OscDestination destination{std::string(host), port};
            auto emitter = factory_
                ? factory_(std::move(destination), std::move(profile))
                : std::make_shared<transport::OscCommandEmitter>(
                    std::move(destination), std::move(profile));
            if (!emitter) return {false, "Command output emitter could not be created."};
            auto executor = std::make_unique<core::RecognitionCommandExecutor>(
                core::CommandDispatcher(std::move(emitter)), std::move(observer));
            configuration_ = configuration;
            executor_ = std::move(executor);
            armed_ = true;
        }
        catch (const std::exception& error)
        {
            return {false, std::string("Command output could not start: ") + error.what()};
        }
        catch (...)
        {
            return {false, "Command output could not start: unknown construction failure."};
        }
        return {true, {}};
    }

    core::RecognitionExecutionResult CommandOutputSession::submit(
        const core::RecognitionTranscript& transcript)
    {
        std::lock_guard lock(mutex_);
        return armed_ ? executor_->submit(transcript) : core::RecognitionExecutionResult{};
    }

    void CommandOutputSession::disarm()
    {
        std::lock_guard lock(mutex_);
        armed_ = false;
        if (executor_) executor_->pause();
    }

    void CommandOutputSession::stop()
    {
        std::unique_ptr<core::RecognitionCommandExecutor> executor;
        {
            std::lock_guard lock(mutex_);
            armed_ = false;
            executor = std::move(executor_);
            configuration_.reset();
        }
        if (executor) executor->shutdown();
    }

    bool CommandOutputSession::active() const
    {
        std::lock_guard lock(mutex_);
        return armed_;
    }

} // namespace timmy::gui
