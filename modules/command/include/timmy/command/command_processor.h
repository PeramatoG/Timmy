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

#include "timmy/command/command_profile.h"
#include "timmy/command/semantic_command_parser.h"
#include "timmy/command/transcript_normalizer.h"
#include <optional>
#include <string>
#include <string_view>

namespace timmy::command {
    struct IntentValidationResult
    {
        bool accepted{false};
        std::string diagnostic;
    };
    [[nodiscard]] IntentValidationResult validateIntent(const CommandIntent& intent,
        const ValidationConfiguration& configuration = {});
    struct IntentRenderingResult
    {
        std::optional<std::string> text;
        std::string diagnostic;
        RenderingTarget target{RenderingTarget::nativeText};
        std::vector<std::string> consoleKeys{};
    };
    [[nodiscard]] IntentRenderingResult renderHydra2Intent(const CommandIntent& intent, bool preClear = false);
    // Console-language rendering is separate from semantic description and OSC.
    [[nodiscard]] IntentRenderingResult renderIntent(const CommandIntent& intent,
        const RenderingConfiguration& configuration = {});
    struct CommandProcessingResult
    {
        NormalizedTranscript transcript;
        SemanticParseResult parsed;
        IntentValidationResult validation;
        IntentRenderingResult rendering;
    };
    class CommandProcessor
    {
    public:
        explicit CommandProcessor(CommandProfile profile = {});
        [[nodiscard]] CommandProcessingResult process(std::string_view transcript,
            const CommandContext& context = {}) const;
        [[nodiscard]] const CommandProfile& profile() const { return profile_; }
    private:
        CommandProfile profile_;
    };
}
