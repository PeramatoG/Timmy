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

#include <cstdint>
#include <optional>
#include <string>

namespace timmy::audio {

    enum class AudioDiagnosticDomain
    {
        backend,
        device,
        capture
    };

    enum class AudioDiagnosticCode
    {
        invalidConfiguration,
        backendInitializationFailed,
        deviceEnumerationFailed,
        deviceUnavailable,
        deviceInitializationFailed,
        invalidStreamFormat,
        captureBufferInitializationFailed,
        captureWorkerInitializationFailed,
        captureStartFailed
    };

    // Describes a recoverable audio failure without exposing backend-specific types.
    struct AudioDiagnostic
    {
        AudioDiagnosticDomain domain = AudioDiagnosticDomain::capture;
        AudioDiagnosticCode code = AudioDiagnosticCode::captureStartFailed;
        std::string message;
        std::optional<std::int64_t> backendCode;
        bool recoverable = true;
    };

} // namespace timmy::audio
