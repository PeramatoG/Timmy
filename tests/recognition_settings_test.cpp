/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/core/asr_engine_factory.h"
#include "timmy/core/command_recognition_profile.h"
#include "whisper_configuration.h"
#include "whisper.h"
#include "grammar-parser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
    using namespace timmy;
    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        std::cerr << "Line " << line << ": " << expression << '\n';
        std::abort();
    }
#define REQUIRE(expression) require((expression), #expression, __LINE__)
    template<class Action> void rejects(Action action)
    {
        try { action(); }
        catch (const std::invalid_argument&) { return; }
        REQUIRE(false);
    }

    void defaults()
    {
        const auto& descriptors = core::asrSettingDescriptors(core::AsrEngineId::whisperCpp);
        REQUIRE(descriptors.size() == 14);
        const auto values = asr::defaultSettings(descriptors);
        REQUIRE(values.size() == descriptors.size());
        REQUIRE(values == asr::validatedSettings(descriptors, {}));
        REQUIRE(std::get<int>(values.at("vad.minimum_silence_ms")) == 300);
        REQUIRE(!std::get<bool>(values.at("whisper.grammar_enabled")));
        for (const auto cores : {0U, 1U, 2U, 4U, 16U})
        {
            const auto configuration = asr::detail::makeWhisperConfiguration({}, cores);
            const auto& decoding = configuration.decoding;
            REQUIRE(decoding.threadCount == (cores == 0 ? 1 : static_cast<int>(std::min(cores, 4U))));
            REQUIRE(decoding.noContext && decoding.noTimestamps && decoding.singleSegment);
            REQUIRE(decoding.maxTokens == 32 && decoding.audioContext == 0);
            REQUIRE(decoding.beamSize == 5 && decoding.greedyBestOf == 5);
            REQUIRE(decoding.temperature == 0.4F && decoding.temperatureIncrement == 1.0F);
            const auto& endpoint = configuration.endpoint;
            REQUIRE(endpoint.speechThreshold == 0.5F);
            REQUIRE(endpoint.minimumSpeechSamples == 4000);
            REQUIRE(endpoint.minimumSilenceSamples == 4800);
            REQUIRE(endpoint.speechPaddingSamples == 480);
            REQUIRE(endpoint.maximumUtteranceSamples == 128000);
            REQUIRE(!configuration.grammarEnabled && configuration.compactPromptEnabled);
            REQUIRE(configuration.grammarPenalty == 20.0F);
            const auto parameters = asr::detail::makeWhisperCommandParameters(configuration);
            REQUIRE(parameters.strategy == WHISPER_SAMPLING_BEAM_SEARCH);
            REQUIRE(parameters.no_context && parameters.no_timestamps && parameters.single_segment);
            REQUIRE(!parameters.print_progress && !parameters.print_realtime && !parameters.print_timestamps);
            REQUIRE(parameters.n_threads == decoding.threadCount);
            REQUIRE(parameters.max_tokens == 32 && parameters.audio_ctx == 0);
            REQUIRE(parameters.beam_search.beam_size == 5 && parameters.greedy.best_of == 5);
            REQUIRE(parameters.temperature == 0.4F && parameters.temperature_inc == 1.0F);
        }
    }

    void validation()
    {
        const auto& descriptors = core::asrSettingDescriptors(core::AsrEngineId::whisperCpp);
        for (const auto& descriptor : descriptors)
        {
            REQUIRE(!descriptor.id.empty() && !descriptor.label.empty() && !descriptor.section.empty());
            asr::SettingValue wrong = std::holds_alternative<bool>(descriptor.defaultValue)
                ? asr::SettingValue(1) : asr::SettingValue(true);
            rejects([&] { (void)asr::validatedSettings(descriptors, {{descriptor.id, wrong}}); });
            auto verifyRange = [&](double number, bool accepted) {
                const asr::SettingValue value = std::holds_alternative<int>(descriptor.defaultValue)
                    ? asr::SettingValue(static_cast<int>(number)) : asr::SettingValue(number);
                if (accepted)
                    REQUIRE(asr::validatedSettings(descriptors, {{descriptor.id, value}}).at(descriptor.id) == value);
                else rejects([&] { (void)asr::validatedSettings(descriptors, {{descriptor.id, value}}); });
            };
            if (descriptor.minimum) { verifyRange(*descriptor.minimum, true); verifyRange(*descriptor.minimum - 1, false); }
            if (descriptor.maximum) { verifyRange(*descriptor.maximum, true); verifyRange(*descriptor.maximum + 1, false); }
        }
        rejects([&] { (void)asr::validatedSettings(descriptors, {{"unknown", 3}}); });
        for (const double value : {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
            rejects([&] { (void)asr::validatedSettings(descriptors, {{"whisper.temperature", value}}); });
        const std::vector<asr::SettingDescriptor> unbounded{
            {"integer", "Integer", "General", 1, {}, {}},
            {"real", "Real", "General", 0.0, {}, {}},
            {"boolean", "Boolean", "General", false, {}, {}}
        };
        const asr::SettingValues values{{"integer", -200}, {"real", -200.25}, {"boolean", true}};
        REQUIRE(asr::validatedSettings(unbounded, values) == values);
        rejects([&] { (void)core::createAsrBackend(core::AsrEngineId::whisperCpp, {}, {{"whisper.beam_size", 9}}); });
        rejects([&] { (void)core::createAsrBackend(core::AsrEngineId::whisperCpp, {}, {{"vad.minimum_speech_ms", -1}}); });
        rejects([&] { (void)core::asrSettingDescriptors(static_cast<core::AsrEngineId>(99)); });
        rejects([&] { (void)core::createAsrBackend(static_cast<core::AsrEngineId>(99), {}); });
    }

    void configuredEndpointBehavior()
    {
        const asr::SettingValues values{
            {"vad.speech_threshold", 0.75}, {"vad.minimum_speech_ms", 48},
            {"vad.minimum_silence_ms", 32}, {"vad.speech_padding_ms", 16},
            {"vad.maximum_utterance_ms", 128}
        };
        const auto configuration = asr::detail::makeWhisperConfiguration(values, 1);
        asr::detail::WhisperEndpointDetector detector(configuration.endpoint);
        const std::vector<float> block(256, 0.1F);
        // Below the configured threshold is retained only as padding.
        REQUIRE(!detector.process(block, 0.7F));
        for (int index = 0; index < 3; ++index) REQUIRE(!detector.process(block, 0.8F));
        REQUIRE(!detector.process(block, 0.1F));
        const auto utterance = detector.process(block, 0.1F);
        REQUIRE(utterance && utterance->size() == 1280); // 256 padding + 768 speech + 256 padding.
        detector.reset();
        REQUIRE(!detector.process(block, 0.8F));
        REQUIRE(!detector.process(block, 0.1F));
        REQUIRE(!detector.process(block, 0.1F)); // Too short for the configured minimum speech.
        detector.reset();
        for (int index = 0; index < 7; ++index) REQUIRE(!detector.process(block, 0.8F));
        const auto maximum = detector.process(block, 0.8F);
        REQUIRE(maximum && maximum->size() == 2048);
    }

    void translationAndSnapshot()
    {
        asr::SettingValues values{
            {"whisper.threads", 6}, {"whisper.max_tokens", 48}, {"whisper.audio_context", 512},
            {"whisper.beam_size", 3}, {"whisper.temperature", 0.2}, {"whisper.temperature_increment", 0.3},
            {"whisper.grammar_enabled", false}, {"whisper.grammar_penalty", 12.5},
            {"whisper.compact_prompt", false}, {"vad.speech_threshold", 0.6},
            {"vad.minimum_speech_ms", 333}, {"vad.minimum_silence_ms", 127},
            {"vad.speech_padding_ms", 41}, {"vad.maximum_utterance_ms", 9123}
        };
        const auto configuration = asr::detail::makeWhisperConfiguration(values, 2);
        const auto& decoding = configuration.decoding;
        REQUIRE(decoding.threadCount == 6 && decoding.maxTokens == 48 && decoding.audioContext == 512);
        REQUIRE(decoding.beamSize == 3 && decoding.greedyBestOf == 5);
        REQUIRE(decoding.temperature == 0.2F && decoding.temperatureIncrement == 0.3F);
        REQUIRE(decoding.noContext && decoding.noTimestamps && decoding.singleSegment);
        const auto& endpoint = configuration.endpoint;
        REQUIRE(endpoint.speechThreshold == 0.6F);
        REQUIRE(endpoint.minimumSpeechSamples == 5328);
        REQUIRE(endpoint.minimumSilenceSamples == 2032);
        REQUIRE(endpoint.speechPaddingSamples == 656);
        REQUIRE(endpoint.maximumUtteranceSamples == 145968);
        REQUIRE(!configuration.grammarEnabled && !configuration.compactPromptEnabled);
        REQUIRE(configuration.grammarPenalty == 12.5F);
        const auto parameters = asr::detail::makeWhisperCommandParameters(configuration);
        REQUIRE(parameters.n_threads == 6 && parameters.max_tokens == 48 && parameters.audio_ctx == 512);
        REQUIRE(parameters.beam_search.beam_size == 3 && parameters.greedy.best_of == 5);
        REQUIRE(parameters.temperature == 0.2F && parameters.temperature_inc == 0.3F);
        REQUIRE(parameters.no_context && parameters.no_timestamps && parameters.single_segment);
        const asr::RecognitionHints hints{{"channel", "canal"}, {}, asr::RecognitionConstraintIntent::commandPhrase};
        const auto disabled = asr::detail::configuredWhisperGuidance(hints, configuration);
        REQUIRE(disabled.grammar.empty() && disabled.initialPrompt.empty());
        REQUIRE(disabled.startRule.empty() && disabled.grammarPenalty == 0.0F);
        REQUIRE(asr::detail::isValidWhisperRecognitionGuidance(disabled));
        const auto backend = core::createAsrBackend(core::AsrEngineId::whisperCpp, {}, values);
        REQUIRE(backend->state() == asr::AsrState::stopped);
        values["whisper.threads"] = 1;
        REQUIRE(configuration.decoding.threadCount == 6);
        values = asr::defaultSettings(core::asrSettingDescriptors(core::AsrEngineId::whisperCpp));
        const auto reset = asr::detail::makeWhisperConfiguration(values, 16);
        REQUIRE(reset.decoding.threadCount == 4 && reset.decoding.maxTokens == 32);
        REQUIRE(reset.endpoint.minimumSpeechSamples == 4000 && reset.endpoint.maximumUtteranceSamples == 128000);
        const auto defaultGuidance = asr::detail::configuredWhisperGuidance(hints, reset);
        REQUIRE(defaultGuidance.grammar.empty() && !defaultGuidance.initialPrompt.empty());
        REQUIRE(defaultGuidance.grammarPenalty == 20.0F);
        // Both switches operate independently.
        values["whisper.grammar_enabled"] = false;
        auto guidance = asr::detail::configuredWhisperGuidance(hints, asr::detail::makeWhisperConfiguration(values, 1));
        REQUIRE(guidance.grammar.empty() && !guidance.initialPrompt.empty());
        values["whisper.grammar_enabled"] = true;
        guidance = asr::detail::configuredWhisperGuidance(hints, asr::detail::makeWhisperConfiguration(values, 1));
        REQUIRE(!guidance.grammar.empty() && !guidance.initialPrompt.empty());
        values["whisper.compact_prompt"] = false;
        guidance = asr::detail::configuredWhisperGuidance(hints, asr::detail::makeWhisperConfiguration(values, 1));
        REQUIRE(!guidance.grammar.empty() && guidance.initialPrompt.empty());
    }

    void independentGuidanceParameters()
    {
        const auto hints = core::commandRecognitionHints();
        for (const bool grammarEnabled : {true, false})
        for (const bool promptEnabled : {true, false})
        {
            const asr::SettingValues values{
                {"whisper.grammar_enabled", grammarEnabled}, {"whisper.compact_prompt", promptEnabled},
                {"whisper.grammar_penalty", 12.5}, {"whisper.threads", 6},
                {"whisper.beam_size", 3}, {"whisper.max_tokens", 48},
                {"whisper.temperature", 0.2}, {"whisper.temperature_increment", 0.3}
            };
            const auto config = asr::detail::makeWhisperConfiguration(values, 2);
            const auto guidance = asr::detail::configuredWhisperGuidance(hints, config);
            REQUIRE(asr::detail::isValidWhisperRecognitionGuidance(guidance));
            REQUIRE(guidance.grammar.empty() == !grammarEnabled);
            REQUIRE(guidance.startRule.empty() == !grammarEnabled);
            REQUIRE(guidance.initialPrompt.empty() == !promptEnabled);
            REQUIRE(guidance.grammarPenalty == (grammarEnabled ? 12.5F : 0.0F));
            const auto grammar = guidance.grammar.empty() ? grammar_parser::parse_state{}
                : grammar_parser::parse(guidance.grammar.c_str());
            auto rules = grammar.c_rules();
            auto parameters = asr::detail::makeWhisperCommandParameters(config);
            // Language and tuning remain independent of guidance.
            parameters.language = "es";
            asr::detail::applyWhisperRecognitionGuidance(parameters, guidance, grammar, rules);
            REQUIRE((parameters.initial_prompt != nullptr) == promptEnabled);
            REQUIRE((parameters.grammar_rules != nullptr) == grammarEnabled);
            REQUIRE(parameters.n_grammar_rules == (grammarEnabled ? rules.size() : 0));
            REQUIRE(parameters.grammar_penalty == (grammarEnabled ? 12.5F : 0.0F));
            if (grammarEnabled)
            {
                REQUIRE(parameters.grammar_rules == rules.data());
                REQUIRE(parameters.i_start_rule == grammar.symbol_ids.at(guidance.startRule));
            }
            else REQUIRE(parameters.i_start_rule == 0);
            if (promptEnabled) REQUIRE(std::string(parameters.initial_prompt) == guidance.initialPrompt);
            REQUIRE(parameters.no_context && parameters.strategy == WHISPER_SAMPLING_BEAM_SEARCH);
            REQUIRE(std::string(parameters.language) == "es");
            REQUIRE(parameters.n_threads == 6 && parameters.beam_search.beam_size == 3);
            REQUIRE(parameters.max_tokens == 48 && parameters.greedy.best_of == 5);
            REQUIRE(parameters.temperature == 0.2F && parameters.temperature_inc == 0.3F);
            const auto diagnostics = asr::detail::whisperGuidanceDiagnostics(config, parameters);
            REQUIRE(std::get<bool>(diagnostics.at("whisper.grammar_enabled")) == grammarEnabled);
            REQUIRE(std::get<bool>(diagnostics.at("whisper.compact_prompt")) == promptEnabled);
            REQUIRE(std::get<bool>(diagnostics.at("whisper.grammar_passed")) == grammarEnabled);
            REQUIRE(std::get<bool>(diagnostics.at("whisper.initial_prompt_passed")) == promptEnabled);
            REQUIRE(diagnostics.contains("whisper.grammar_penalty") == grammarEnabled);
            if (grammarEnabled) REQUIRE(std::get<double>(diagnostics.at("whisper.grammar_penalty")) == 12.5);

            // Reusing parameters and stale parsed rules must not re-enable absent guidance.
            const auto neutral = asr::detail::configuredWhisperGuidance(hints,
                asr::detail::makeWhisperConfiguration({{"whisper.grammar_enabled", false},
                    {"whisper.compact_prompt", false}}, 2));
            asr::detail::applyWhisperRecognitionGuidance(parameters, neutral, grammar, rules);
            REQUIRE(parameters.initial_prompt == nullptr && parameters.grammar_rules == nullptr);
            REQUIRE(parameters.n_grammar_rules == 0 && parameters.i_start_rule == 0);
            REQUIRE(parameters.grammar_penalty == 0.0F && parameters.no_context);
        }

        // Enabled switches are permissions, not a reason to manufacture absent hints.
        const auto config = asr::detail::makeWhisperConfiguration({}, 1);
        for (const auto& emptyHints : {asr::RecognitionHints{}, asr::RecognitionHints{
            {"channel"}, {}, asr::RecognitionConstraintIntent::none}})
        {
            const auto guidance = asr::detail::configuredWhisperGuidance(emptyHints, config);
            REQUIRE(asr::detail::isValidWhisperRecognitionGuidance(guidance));
            REQUIRE(guidance.grammar.empty() && guidance.startRule.empty() && guidance.grammarPenalty == 0.0F);
            auto parameters = asr::detail::makeWhisperCommandParameters(config);
            auto rules = std::vector<const whisper_grammar_element*>{};
            asr::detail::applyWhisperRecognitionGuidance(parameters, guidance, {}, rules);
            REQUIRE(parameters.grammar_rules == nullptr && parameters.n_grammar_rules == 0);
            REQUIRE(parameters.i_start_rule == 0 && parameters.grammar_penalty == 0.0F);
            REQUIRE((parameters.initial_prompt != nullptr) == !emptyHints.vocabulary.empty());
        }
    }
}
int main()
{
    defaults();
    validation();
    configuredEndpointBehavior();
    translationAndSnapshot();
    independentGuidanceParameters();
}
