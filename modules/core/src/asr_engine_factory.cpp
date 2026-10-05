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

#include "timmy/asr/whisper_asr_backend.h"

#include <stdexcept>

namespace timmy::core {

    // Lists the backend-neutral recognition engines available to frontends.
    const std::vector<AsrEngineDescriptor>& availableAsrEngines()
    {
        static const std::vector<AsrEngineDescriptor> engines{
            { AsrEngineId::whisperCpp, "Whisper.cpp" }
        };
        return engines;
    }

    // Constructs the selected backend while containing engine-specific setup.
    std::shared_ptr<asr::IAsrBackend> createAsrBackend(
        AsrEngineId engine,
        const AsrEngineResources& resources)
    {
        switch (engine)
        {
        case AsrEngineId::whisperCpp:
            return std::make_shared<asr::WhisperAsrBackend>(
                asr::WhisperAsrBackendOptions{
                    resources.voiceActivityModelPath
                });
        }
        throw std::invalid_argument("Unknown ASR engine.");
    }

} // namespace timmy::core
