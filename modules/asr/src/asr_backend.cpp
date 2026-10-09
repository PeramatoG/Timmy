/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/asr_backend.h"

#include <utility>

namespace timmy::asr {

    // Replaces the observer used for recognition results and diagnostics.
    void IAsrBackend::setResultListener(
        std::shared_ptr<IAsrResultListener> listener)
    {
        std::lock_guard lock(listenerMutex_);
        listener_ = std::move(listener);
    }

    // Gates audio by lifecycle and validates the 16 kHz mono float boundary.
    void IAsrBackend::onAudioBlock(const audio::AudioBlockView& block)
    {
        if (state() != AsrState::running)
        {
            return;
        }

        if (block.format.sampleRate != requiredSampleRate
            || block.format.channelCount != requiredChannelCount
            || block.frameCount == 0
            || block.interleavedSamples.size() < block.frameCount)
        {
            publishDiagnostic({
                AsrDiagnosticCode::invalidAudioFormat,
                "ASR input must be non-empty 16 kHz mono float PCM."
            });
            return;
        }

        const audio::AudioBlockView validatedBlock{
            block.interleavedSamples.first(block.frameCount),
            block.frameCount,
            block.format
        };
        processAudioBlock(validatedBlock);
    }

    void IAsrBackend::publishProgress(const RecognitionProgress& progress) const
    {
        if (const auto listener = resultListener()) listener->onAsrProgress(progress);
    }

    // Publishes a recognition result to the current listener snapshot.
    void IAsrBackend::publishResult(const AsrResult& result) const
    {
        if (const auto listener = resultListener())
        {
            listener->onAsrResult(result);
        }
    }

    // Publishes a structured diagnostic to the current listener snapshot.
    void IAsrBackend::publishDiagnostic(
        const AsrDiagnostic& diagnostic) const
    {
        if (const auto listener = resultListener())
        {
            listener->onAsrDiagnostic(diagnostic);
        }
    }

    // Returns a stable listener snapshot without holding the callback lock.
    std::shared_ptr<IAsrResultListener> IAsrBackend::resultListener() const
    {
        std::lock_guard lock(listenerMutex_);
        return listener_;
    }

} // namespace timmy::asr
