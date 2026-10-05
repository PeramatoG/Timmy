/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/asr_backend_factory.h"

#include "timmy/asr/whisper_asr_backend.h"

#include <stdexcept>

namespace timmy::asr {

    // Lists the recognition engines available in this Timmy build.
    std::vector<AsrEngineInfo> availableAsrEngines()
    {
        return {
            {
                AsrEngine::whisperCpp,
                "Whisper.cpp",
                true
            }
        };
    }

    // Creates the selected ASR backend while keeping its implementation private.
    std::shared_ptr<IAsrBackend> createAsrBackend(
        const AsrBackendCreateOptions& options)
    {
        switch (options.engine)
        {
        case AsrEngine::whisperCpp:
        {
            WhisperAsrBackendOptions whisperOptions;
            whisperOptions.vadModelPath = options.vadModelResourcePath;
            return std::make_shared<WhisperAsrBackend>(whisperOptions);
        }
        }

        throw std::invalid_argument("Unsupported ASR engine.");
    }

} // namespace timmy::asr
