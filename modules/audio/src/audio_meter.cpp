/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_meter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace timmy::audio {

    void AudioMeter::onAudioBlock(
        const AudioBlockView& block)
    {
        const std::size_t channelCount =
            static_cast<std::size_t>(
                block.format.channelCount);

        if (channelCount == 0
            || block.frameCount == 0
            || block.interleavedSamples.empty())
        {
            return;
        }

        const std::size_t availableFrameCount =
            std::min(
                block.frameCount,
                block.interleavedSamples.size()
                / channelCount);

        if (availableFrameCount == 0)
        {
            return;
        }

        std::vector<AudioChannelLevel> levels(
            channelCount);

        std::vector<double> squareSums(
            channelCount,
            0.0);

        for (std::size_t frame = 0;
            frame < availableFrameCount;
            ++frame)
        {
            const std::size_t frameOffset =
                frame * channelCount;

            for (std::size_t channel = 0;
                channel < channelCount;
                ++channel)
            {
                const float sample =
                    block.interleavedSamples[
                        frameOffset + channel];

                const float magnitude =
                    std::min(
                        std::abs(sample),
                        1.0F);

                levels[channel].peak =
                    std::max(
                        levels[channel].peak,
                        magnitude);

                squareSums[channel] +=
                    static_cast<double>(magnitude)
                    * static_cast<double>(magnitude);
            }
        }

        for (std::size_t channel = 0;
            channel < channelCount;
            ++channel)
        {
            levels[channel].rms =
                static_cast<float>(
                    std::sqrt(
                        squareSums[channel]
                        / static_cast<double>(
                            availableFrameCount)));
        }

        {
            std::lock_guard lock(mutex_);

            snapshot_.format =
                block.format;

            snapshot_.channels =
                std::move(levels);

            snapshot_.processedFrameCount +=
                availableFrameCount;
        }
    }

    AudioMeterSnapshot AudioMeter::snapshot() const
    {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void AudioMeter::reset()
    {
        std::lock_guard lock(mutex_);
        snapshot_ = {};
    }

} // namespace timmy::audio