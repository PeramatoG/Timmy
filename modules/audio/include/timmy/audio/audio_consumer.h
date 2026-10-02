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

#include "timmy/audio/audio_stream_format.h"

#include <cstddef>
#include <span>

namespace timmy::audio {

    struct AudioBlockView
    {
        std::span<const float> interleavedSamples;
        std::size_t frameCount = 0;
        AudioStreamFormat format;
    };

    class IAudioConsumer
    {
    public:
        virtual ~IAudioConsumer() = default;

        // Called from Timmy's audio worker thread, never directly from the
        // realtime device callback.
        //
        // The sample view is valid only for the duration of this call.
        // Consumers that need to retain audio must copy it to their own storage.
        virtual void onAudioBlock(const AudioBlockView& block) = 0;
    };

} // namespace timmy::audio
