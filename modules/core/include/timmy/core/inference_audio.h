/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "timmy/asr/asr_types.h"

namespace timmy::core {
    struct InferenceAudioCapture
    {
        std::filesystem::path resultPath;
        std::string sessionId, caseId;
        unsigned attempt = 1;
        asr::AttemptGeneration generation = 0;
    };
    // Deterministic relative path, injective filename escaping, no platform punctuation.
    [[nodiscard]] std::filesystem::path inferenceAudioPath(
        const InferenceAudioCapture& capture, std::uint64_t inferenceIndex);
    // Synchronous worker-only scan/save. Save errors become diagnostics; recognition
    // remains independent. The borrowed PCM is never changed or retained.
    [[nodiscard]] asr::InferenceInputDiagnostics inspectInferenceAudio(
        const asr::InferenceInputView& input,
        const std::optional<InferenceAudioCapture>& capture = std::nullopt);
}
