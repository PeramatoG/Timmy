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

#include "timmy/audio/audio_consumer.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace timmy::audio {

    struct AudioChannelLevel
    {
        float peak = 0.0F;
        float rms = 0.0F;
    };

    struct AudioMeterSnapshot
    {
        AudioStreamFormat format;
        std::vector<AudioChannelLevel> channels;
        std::uint64_t processedFrameCount = 0;
    };

    class AudioMeter final
        : public IAudioConsumer
    {
    public:
        void onAudioBlock(const AudioBlockView& block) override;

        [[nodiscard]]
        AudioMeterSnapshot snapshot() const;

        void reset();

    private:
        mutable std::mutex mutex_;
        AudioMeterSnapshot snapshot_;
    };

} // namespace timmy::audio