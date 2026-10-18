/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "timmy/core/recognition_pipeline.h"

namespace timmy::core {
    struct RecognitionReplayResult
    {
        asr::AsrOperationResult operation;
        std::optional<RecognitionTranscript> transcript;
    };
    // Reads a saved mono float32/16 kHz inference WAV and uses the normal backend
    // worker and command pipeline. Opens no physical audio device. Timeout cancels
    // and joins the backend before returning; callers choose a CPU-appropriate limit.
    [[nodiscard]] RecognitionReplayResult replayInferenceWav(
        std::shared_ptr<asr::IAsrBackend> backend, const std::filesystem::path& path,
        const asr::AsrConfiguration& configuration, command::CommandProfile profile = {},
        std::chrono::milliseconds timeout = std::chrono::minutes(2));
}
