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

#include "timmy/core/recognition_command_executor.h"
#include "timmy/transport/hydra2_osc_command_profile.h"
#include "timmy/transport/osc_command_emitter.h"

#include <functional>
#include <memory>
#include <string>

namespace timmy::gui {

    enum class OutputProfile { hydra2, genericOsc };

    struct CommandOutputConfiguration
    {
        bool enabled = false;
        OutputProfile profile = OutputProfile::hydra2;
        std::string host;
        std::string port = "8000";
        std::string keyHoldMs = std::to_string(transport::Hydra2OscOptions{}.hold.count());
        std::string keyGapMs = std::to_string(transport::Hydra2OscOptions{}.gap.count());
        bool preClear = false;
    };

    struct CommandOutputStartResult
    {
        bool succeeded = false;
        std::string diagnostic;
    };

    // All lifecycle calls belong to one frontend thread. Observers run on the
    // executor worker and must marshal results without stopping this session.
    class CommandOutputSession final
    {
    public:
        using Observer = core::RecognitionCommandExecutor::Observer;
        using EmitterFactory = std::function<std::shared_ptr<transport::ICommandEmitter>(
            transport::OscDestination, std::shared_ptr<const transport::IOscCommandProfile>)>;

        explicit CommandOutputSession(EmitterFactory factory = {});
        ~CommandOutputSession();
        CommandOutputSession(const CommandOutputSession&) = delete;
        CommandOutputSession& operator=(const CommandOutputSession&) = delete;

        [[nodiscard]] CommandOutputStartResult start(
            const CommandOutputConfiguration& configuration, Observer observer = {});
        [[nodiscard]] core::RecognitionExecutionResult submit(
            const core::RecognitionTranscript& transcript);
        void stop();
        [[nodiscard]] bool active() const;

    private:
        EmitterFactory factory_;
        std::unique_ptr<core::RecognitionCommandExecutor> executor_;
    };

} // namespace timmy::gui
