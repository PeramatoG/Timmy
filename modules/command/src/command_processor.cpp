/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/command/command_processor.h"
#include <type_traits>
#include <utility>

namespace timmy::command {
    namespace {
        bool validType(TargetType type)
        {
            return type == TargetType::channel || type == TargetType::fixture || type == TargetType::group;
        }
        bool validSelection(const TargetSelection& selection, const ValidationConfiguration& config)
        {
            if (!selection.isValid() || (selection.all && !config.allowAll)) return false;
            for (const auto& range : selection.included)
                if (range.last > config.maxIdentifier) return false;
            for (const auto& range : selection.excluded)
                if (range.last > config.maxIdentifier) return false;
            return true;
        }
        std::string targetName(TargetType type, const RenderingConfiguration& config)
        {
            switch (type) {
            case TargetType::channel: return config.channel;
            case TargetType::fixture: return config.fixture;
            case TargetType::group: return config.group;
            }
            return {};
        }
        std::string ranges(const std::vector<TargetRange>& terms, const RenderingConfiguration& config)
        {
            std::string text;
            for (const auto& term : terms) {
                if (!text.empty()) text += " + ";
                text += std::to_string(term.first);
                if (term.first != term.last)
                    text += " " + config.range + " " + std::to_string(term.last);
            }
            return text;
        }
        std::string selectionText(const TargetSelection& selection, const RenderingConfiguration& config)
        {
            auto text = targetName(selection.type, config) + " "
                + (selection.all ? config.all : ranges(selection.included, config));
            for (const auto& term : selection.excluded)
                text += " - " + ranges({term}, config);
            return text;
        }
    }
    IntentValidationResult validateIntent(const CommandIntent& intent, const ValidationConfiguration& config)
    {
        const bool valid = std::visit([&](const auto& operation) {
            using T = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<T, NavigateSelection>)
                return validType(operation.type) && (operation.direction == SelectionDirection::next
                    || operation.direction == SelectionDirection::previous);
            else if constexpr (std::is_same_v<T, AdvanceTest> || std::is_same_v<T, ClearEditor>
                || std::is_same_v<T, ClearSelection> || std::is_same_v<T, ReleaseEditor>) return true;
            else {
                if (!validSelection(operation.target, config)) return false;
                if constexpr (std::is_same_v<T, HomeSelection>)
                    return operation.target.type == TargetType::fixture;
                if constexpr (std::is_same_v<T, ModifySelection>)
                    return !operation.target.all && operation.target.excluded.empty()
                        && (operation.modification == SelectionModification::add
                            || operation.modification == SelectionModification::remove);
                return true;
            }
        }, intent);
        return {valid, valid ? "" : "Intent rejected by semantic validation."};
    }
    IntentRenderingResult renderIntent(const CommandIntent& intent, const RenderingConfiguration& config)
    {
        if (config.target == RenderingTarget::hydra2Keys)
            return renderHydra2Intent(intent, config.preClear);
        if (config.target != RenderingTarget::nativeText)
            return {std::nullopt, "Unknown command rendering target."};
        const auto validation = validateIntent(intent);
        if (!validation.accepted) return {std::nullopt, validation.diagnostic};
        if (config.channel.empty() || config.fixture.empty() || config.group.empty()
            || config.range.empty() || config.all.empty() || config.intensity.empty())
            return {std::nullopt, "Rendering vocabulary must not be empty."};
        return {std::visit([&](const auto& operation) -> std::string {
            using T = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<T, AdvanceTest>) return "Test Next";
            else if constexpr (std::is_same_v<T, ClearEditor>) return "Clear Editor";
            else if constexpr (std::is_same_v<T, ClearSelection>) return "Clear Selection";
            else if constexpr (std::is_same_v<T, ReleaseEditor>) return "Release Editor";
            else if constexpr (std::is_same_v<T, NavigateSelection>)
                return targetName(operation.type, config) + (operation.direction == SelectionDirection::next
                    ? " Next" : " Previous");
            else if constexpr (std::is_same_v<T, SetIntensity>)
                return selectionText(operation.target, config) + " " + config.intensity
                    + " " + std::to_string(operation.level.percent());
            else if constexpr (std::is_same_v<T, ModifySelection>)
                return std::string(operation.modification == SelectionModification::add ? "Add " : "Remove ")
                    + selectionText(operation.target, config);
            else {
                const auto text = selectionText(operation.target, config);
                if constexpr (std::is_same_v<T, SelectSelection>) return "Select " + text;
                else if constexpr (std::is_same_v<T, ReleaseSelection>) return "Release " + text;
                else if constexpr (std::is_same_v<T, TestSelection>) return "Test " + text;
                else return "Home " + text;
            }
        }, intent), {}};
    }
    CommandProcessor::CommandProcessor(CommandProfile profile) : profile_(std::move(profile)) {}
    CommandProcessingResult CommandProcessor::process(std::string_view transcript, const CommandContext& context) const
    {
        CommandProcessingResult result;
        result.transcript = TranscriptNormalizer{profile_.normalization}.normalize(transcript);
        result.parsed = SemanticCommandParser{profile_.parsing}.parse(result.transcript.normalizedText, context);
        if (!result.parsed.command) {
            result.validation.diagnostic = "No deterministic intent produced.";
            return result;
        }
        result.validation = validateIntent(*result.parsed.command, profile_.validation);
        if (result.validation.accepted)
            result.rendering = renderIntent(*result.parsed.command, profile_.rendering);
        return result;
    }
}
