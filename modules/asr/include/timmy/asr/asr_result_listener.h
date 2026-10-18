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

#include "timmy/asr/asr_types.h"

namespace timmy::asr {

    class IAsrResultListener
    {
    public:
        // Allows listeners to be destroyed through the common contract.
        virtual ~IAsrResultListener() = default;

        // Receives recognition events on a backend-selected thread, which is
        // not guaranteed to be a frontend or GUI thread.
        virtual void onAsrResult(const AsrResult& result) = 0;

        // Optional exact inference-input observation. Implementations must consume
        // the borrowed view synchronously; never retain its span. No realtime thread.
        // false cancels work from an attempt Core has already disarmed. Diagnostic
        // save failures must still return true so they cannot veto recognition.
        virtual bool onAsrInferenceInput(const InferenceInputView&) { return true; }

        // Optional progress; implementations may emit only supported stages.
        virtual void onAsrProgress(const RecognitionProgress&) {}

        // Receives runtime diagnostics on a backend-selected thread.
        virtual void onAsrDiagnostic(const AsrDiagnostic& diagnostic) = 0;
    };

} // namespace timmy::asr
