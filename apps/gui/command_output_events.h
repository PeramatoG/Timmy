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

#include <wx/defs.h>
#include <wx/event.h>

#include <cstdint>

namespace timmy::gui {

    constexpr int commandOutputEventId = wxID_HIGHEST + 103;

    struct CommandOutputEventPayload
    {
        std::uint64_t generation;
        core::RecognitionExecutionResult result;
    };

    // The caller must join the executor before destroying the target or clearing
    // its pending events. Generation filtering belongs to the frontend handler.
    inline core::RecognitionCommandExecutor::Observer commandOutputObserver(
        wxEvtHandler& target, std::uint64_t generation)
    {
        return [&target, generation](const core::RecognitionExecutionResult& result) {
            auto* event = new wxThreadEvent(wxEVT_THREAD, commandOutputEventId);
            event->SetPayload(CommandOutputEventPayload{generation, result});
            wxQueueEvent(&target, event);
        };
    }

} // namespace timmy::gui
