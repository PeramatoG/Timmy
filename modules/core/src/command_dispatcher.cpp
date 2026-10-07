/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/command_dispatcher.h"

#include <stdexcept>
#include <utility>

namespace timmy::core {

    // Retains shared ownership of the configured transport-neutral emitter.
    CommandDispatcher::CommandDispatcher(
        std::shared_ptr<transport::ICommandEmitter> emitter, command::CommandProfile profile)
        : emitter_(std::move(emitter)), profile_(std::move(profile))
    {
        if (!emitter_)
        {
            throw std::invalid_argument("Command emitter must not be null.");
        }
    }

    // Validates and renders every intent, including directly constructed candidates.
    CommandDispatchResult CommandDispatcher::dispatch(
        const command::SemanticCommand& command)
    {
        const auto validation = command::validateIntent(command, profile_.validation);
        if (!validation.accepted)
            return {CommandDispatchState::notDispatched, validation.diagnostic};
        const auto rendered = command::renderIntent(command, profile_.rendering);
        if (!rendered.text)
            return {CommandDispatchState::notDispatched, rendered.diagnostic};
        const auto result = emitter_->emitRendered({*rendered.text, command, rendered.target, rendered.consoleKeys});
        return {
            result.state == transport::EmissionState::succeeded
                ? CommandDispatchState::succeeded
                : CommandDispatchState::failed,
            result.diagnostic,
            *rendered.text
        };
    }

    // Declines dispatch when semantic parsing did not produce a command.
    CommandDispatchResult CommandDispatcher::dispatch(
        const std::optional<command::SemanticCommand>& command)
    {
        if (!command)
        {
            return {};
        }
        return dispatch(*command);
    }

} // namespace timmy::core
