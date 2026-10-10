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

#include "timmy/asr/configurable_settings.h"
#include "whisper_command_decoding.h"
#include "whisper_endpoint_detector.h"
#include "whisper_recognition_guidance.h"
struct whisper_full_params;
struct whisper_grammar_element;
namespace grammar_parser { struct parse_state; }

namespace timmy::asr::detail {

    struct WhisperConfiguration
    {
        WhisperCommandDecodingProfile decoding;
        WhisperEndpointOptions endpoint;
        bool grammarEnabled;
        float grammarPenalty;
        bool compactPromptEnabled;
    };

    [[nodiscard]] WhisperConfiguration makeWhisperConfiguration(
        const SettingValues& values, unsigned int hardwareConcurrency);

    // This header and all Whisper types stay private to the backend implementation.
    [[nodiscard]] whisper_full_params makeWhisperCommandParameters(
        const WhisperConfiguration& configuration);

    [[nodiscard]] WhisperRecognitionGuidance configuredWhisperGuidance(
        const RecognitionHints& hints, const WhisperConfiguration& configuration);

    // The caller retains guidance, parsed grammar and rule pointers through whisper_full().
    void applyWhisperRecognitionGuidance(whisper_full_params& parameters,
        const WhisperRecognitionGuidance& guidance,
        const grammar_parser::parse_state& grammar,
        std::vector<const whisper_grammar_element*>& rules);

    // Enabled switches come from backend configuration; passed flags come from actual parameters.
    [[nodiscard]] SettingValues whisperGuidanceDiagnostics(
        const WhisperConfiguration& configuration, const whisper_full_params& parameters);
} // namespace timmy::asr::detail
