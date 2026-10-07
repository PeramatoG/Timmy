/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include "timmy/command/command_processor.h"
#include "timmy/transport/command_emitter.h"

#include <memory>
#include <optional>
#include <string>

namespace timmy::core {

    enum class CommandDispatchState
    {
        notDispatched,
        succeeded,
        failed
    };

    struct CommandDispatchResult
    {
        CommandDispatchState state{CommandDispatchState::notDispatched};
        std::string diagnostic;
        std::string renderedCommand{};
    };

    class CommandDispatcher final
    {
    public:
        explicit CommandDispatcher(
            std::shared_ptr<transport::ICommandEmitter> emitter, command::CommandProfile profile = {});

        [[nodiscard]] CommandDispatchResult dispatch(
            const command::SemanticCommand& command);
        [[nodiscard]] CommandDispatchResult dispatch(
            const std::optional<command::SemanticCommand>& command);

    private:
        std::shared_ptr<transport::ICommandEmitter> emitter_;
        command::CommandProfile profile_;
    };

} // namespace timmy::core
