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

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace timmy::gui {

    enum class OutputProfile { hydra2, genericOsc };

    // Bound each Hydra pulse hold/gap to 250 ms to avoid accidental multi-minute
    // executions. Normal 30 ms defaults remain unchanged. In-flight plans still
    // finish safely; frontends must perform final joining away from their UI.
    constexpr std::uint32_t maximumHydraTimingMs = 250;

    struct CommandOutputConfiguration
    {
        bool enabled = false;
        OutputProfile profile = OutputProfile::hydra2;
        std::string host;
        std::string port = "8000";
        std::string keyHoldMs = std::to_string(transport::Hydra2OscOptions{}.hold.count());
        std::string keyGapMs = std::to_string(transport::Hydra2OscOptions{}.gap.count());
        bool preClear = false;

        bool operator==(const CommandOutputConfiguration&) const = default;
    };

    [[nodiscard]] command::CommandProfile commandProfileForOutput(const CommandOutputConfiguration& configuration);

    struct CommandOutputStartResult
    {
        bool succeeded = false;
        std::string diagnostic;
    };

    // Start/disarm/submit belong to one frontend thread. Final stop may run on an
    // owned lifecycle worker after disarm; do not start again until it completes.
    // Observers run on the executor worker and must marshal results without
    // stopping this session. A disarmed worker retains its frozen configuration.
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
        // Immediate, nonblocking acceptance closure; waiting commands are
        // discarded and the in-flight plan completes without interruption.
        void disarm();
        // Final resource cleanup joins the execution worker and may block.
        void stop();
        [[nodiscard]] bool active() const;

    private:
        EmitterFactory factory_;
        mutable std::mutex mutex_;
        std::unique_ptr<core::RecognitionCommandExecutor> executor_;
        std::optional<CommandOutputConfiguration> configuration_;
        bool armed_ = false;
    };

} // namespace timmy::gui
