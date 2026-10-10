/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_configuration.h"

#include "timmy/asr/whisper_asr_backend.h"
#include "whisper.h"
#include "grammar-parser.h"

namespace timmy::asr {

    // Backend-owned schema: IDs and UI metadata contain no implementation types.
    const std::vector<SettingDescriptor>& whisperSettingDescriptors()
    {
        static const std::vector<SettingDescriptor> descriptors{
            {"whisper.threads", "Thread count (0 = automatic)", "Whisper / Advanced", 0, 0, 1024},
            {"whisper.max_tokens", "Max tokens", "Whisper / Advanced", 32, 1, 16384},
            {"whisper.audio_context", "Audio context", "Whisper / Advanced", 0, 0, 1500},
            {"whisper.beam_size", "Beam size", "Whisper / Advanced", 5, 1, 8},
            {"whisper.temperature", "Temperature", "Whisper / Advanced", 0.4, 0, 1},
            {"whisper.temperature_increment", "Temperature increment", "Whisper / Advanced", 1.0, 0, 1},
            {"whisper.grammar_enabled", "Vocabulary grammar", "Whisper / Advanced", false, {}, {}},
            {"whisper.grammar_penalty", "Grammar penalty", "Whisper / Advanced", 20.0, 0, 1000},
            {"whisper.compact_prompt", "Compact vocabulary prompt", "Whisper / Advanced", true, {}, {}},
            {"vad.speech_threshold", "Speech threshold", "VAD / Endpoint", 0.5, 0, 1},
            {"vad.minimum_speech_ms", "Minimum speech (ms)", "VAD / Endpoint", 250, 0, 600000},
            {"vad.minimum_silence_ms", "Minimum silence (ms)", "VAD / Endpoint", 300, 0, 600000},
            {"vad.speech_padding_ms", "Speech padding (ms)", "VAD / Endpoint", 30, 0, 600000},
            {"vad.maximum_utterance_ms", "Maximum utterance (ms)", "VAD / Endpoint", 8000, 1, 600000}
        };
        return descriptors;
    }
} // namespace timmy::asr

namespace timmy::asr::detail {

    // Validate and copy frontend values once; only the backend converts ms to PCM samples.
    WhisperConfiguration makeWhisperConfiguration(
        const SettingValues& values, unsigned int hardwareConcurrency)
    {
        const auto settings = validatedSettings(whisperSettingDescriptors(), values);
        auto integer = [&](const char* id) { return std::get<int>(settings.at(id)); };
        auto floating = [&](const char* id) { return static_cast<float>(std::get<double>(settings.at(id))); };
        auto boolean = [&](const char* id) { return std::get<bool>(settings.at(id)); };
        auto samples = [&](const char* id) { return static_cast<std::size_t>(integer(id)) * 16U; };
        auto decoding = makeWhisperCommandDecodingProfile(hardwareConcurrency);
        if (integer("whisper.threads") != 0) decoding.threadCount = integer("whisper.threads");
        decoding.maxTokens = integer("whisper.max_tokens");
        decoding.audioContext = integer("whisper.audio_context");
        decoding.beamSize = integer("whisper.beam_size");
        decoding.temperature = floating("whisper.temperature");
        decoding.temperatureIncrement = floating("whisper.temperature_increment");
        return {decoding,
            {floating("vad.speech_threshold"), samples("vad.minimum_speech_ms"),
             samples("vad.minimum_silence_ms"), samples("vad.speech_padding_ms"),
             samples("vad.maximum_utterance_ms")},
            boolean("whisper.grammar_enabled"), floating("whisper.grammar_penalty"),
            boolean("whisper.compact_prompt")};
    }

    whisper_full_params makeWhisperCommandParameters(
        const WhisperConfiguration& configuration)
    {
        const auto& profile = configuration.decoding;
        auto parameters = whisper_full_default_params(
            WHISPER_SAMPLING_BEAM_SEARCH);
        parameters.print_progress = false;
        parameters.print_realtime = false;
        parameters.print_timestamps = false;
        parameters.no_context = profile.noContext;
        parameters.no_timestamps = profile.noTimestamps;
        parameters.single_segment = profile.singleSegment;
        parameters.max_tokens = profile.maxTokens;
        parameters.n_threads = profile.threadCount;
        parameters.audio_ctx = profile.audioContext;
        parameters.temperature = profile.temperature;
        parameters.temperature_inc = profile.temperatureIncrement;
        parameters.greedy.best_of = profile.greedyBestOf;
        parameters.beam_search.beam_size = profile.beamSize;
        // Optional guidance starts neutral, including the upstream default penalty.
        parameters.grammar_penalty = 0.0F;
        return parameters;
    }

    WhisperRecognitionGuidance configuredWhisperGuidance(
        const RecognitionHints& hints, const WhisperConfiguration& configuration)
    {
        auto guidance = makeWhisperRecognitionGuidance(hints);
        if (!configuration.grammarEnabled)
        {
            guidance.grammar.clear();
            guidance.startRule.clear();
        }
        if (!configuration.compactPromptEnabled) guidance.initialPrompt.clear();
        guidance.grammarPenalty = guidance.grammar.empty() ? 0.0F : configuration.grammarPenalty;
        return guidance;
    }

    void applyWhisperRecognitionGuidance(whisper_full_params& parameters,
        const WhisperRecognitionGuidance& guidance,
        const grammar_parser::parse_state& grammar,
        std::vector<const whisper_grammar_element*>& rules)
    {
        parameters.initial_prompt = guidance.initialPrompt.empty()
            ? nullptr : guidance.initialPrompt.c_str();
        parameters.grammar_rules = nullptr;
        parameters.n_grammar_rules = 0;
        parameters.i_start_rule = 0;
        parameters.grammar_penalty = 0.0F;
        if (!guidance.grammar.empty() && !rules.empty())
        {
            parameters.grammar_rules = rules.data();
            parameters.n_grammar_rules = rules.size();
            parameters.i_start_rule = grammar.symbol_ids.at(guidance.startRule);
            parameters.grammar_penalty = guidance.grammarPenalty;
        }
    }

    SettingValues whisperGuidanceDiagnostics(
        const WhisperConfiguration& configuration, const whisper_full_params& parameters)
    {
        const bool grammarPassed = parameters.grammar_rules != nullptr && parameters.n_grammar_rules != 0;
        SettingValues diagnostics{
            {"whisper.grammar_enabled", configuration.grammarEnabled},
            {"whisper.compact_prompt", configuration.compactPromptEnabled},
            {"whisper.grammar_passed", grammarPassed},
            {"whisper.initial_prompt_passed", parameters.initial_prompt != nullptr}
        };
        if (grammarPassed) diagnostics["whisper.grammar_penalty"] = static_cast<double>(parameters.grammar_penalty);
        return diagnostics;
    }
} // namespace timmy::asr::detail
