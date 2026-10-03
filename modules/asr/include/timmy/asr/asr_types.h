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

#include <filesystem>
#include <optional>
#include <string>

namespace timmy::asr {

    enum class AsrState
    {
        stopped,
        starting,
        running,
        stopping,
        failed
    };

    struct AsrCapabilities
    {
        bool supportsPartialResults = false;
        bool supportsExplicitLanguage = false;
        bool supportsGrammarConstraints = false;
        bool supportsVocabularyHints = false;
    };

    struct AsrConfiguration
    {
        std::optional<std::string> preferredLanguage;
        std::filesystem::path modelResourcePath;
    };

    enum class AsrResultKind
    {
        partial,
        final
    };

    struct AsrResult
    {
        std::string transcript;
        AsrResultKind kind = AsrResultKind::final;
        std::optional<std::string> detectedLanguage;
    };

    enum class AsrDiagnosticCode
    {
        invalidConfiguration,
        unavailableResource,
        initializationFailure,
        invalidAudioFormat,
        recognitionFailure
    };

    struct AsrDiagnostic
    {
        AsrDiagnosticCode code = AsrDiagnosticCode::recognitionFailure;
        std::string message;
    };

    struct AsrOperationResult
    {
        bool succeeded = false;
        std::optional<AsrDiagnostic> diagnostic;
    };

} // namespace timmy::asr
