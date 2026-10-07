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

#include "timmy/command/semantic_command.h"
#include "timmy/command/command_lexicon.h"
#include "timmy/command/transcript_normalizer.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timmy::command {
    // Plain configuration values: no widgets, ASR backend types or transport.
    struct SemanticParsingConfiguration
    {
        std::optional<TargetType> defaultTarget;
        std::vector<CommandAlias> vocabulary{defaultCommandVocabulary()};
    };
    struct ValidationConfiguration
    {
        std::uint32_t maxIdentifier{0xffffffffU};
        bool allowAll{true};
    };
    enum class RenderingTarget { nativeText, hydra2Keys };
    struct RenderingConfiguration
    {
        RenderingTarget target{RenderingTarget::nativeText};
        bool preClear{false};
        std::string channel{"Channel"};
        std::string fixture{"Fixture"};
        std::string group{"Group"};
        std::string range{"Through"};
        std::string all{"All"};
        std::string intensity{"At"};
    };
    struct RecognitionVocabulary
    {
        std::vector<std::string> terms;
        std::vector<std::string> examples;
    };
    [[nodiscard]] RecognitionVocabulary defaultRecognitionVocabulary();
    struct CommandProfile
    {
        std::string id{"timmy-native"};
        RecognitionVocabulary recognition{defaultRecognitionVocabulary()};
        // Distinct configuration domains are serializable independently of widgets.
        LexicalNormalizationConfiguration normalization;
        SemanticParsingConfiguration parsing{TargetType::channel};
        ValidationConfiguration validation;
        RenderingConfiguration rendering;
    };
    [[nodiscard]] CommandProfile hydra2CommandProfile(bool preClear = false);
}
