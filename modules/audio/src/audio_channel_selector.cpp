/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_channel_selector.h"

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace timmy::audio {

    AudioChannelSelector::AudioChannelSelector(
        std::size_t channelIndex)
        : channelIndex_(channelIndex)
    {}

    void AudioChannelSelector::setChannelIndex(
        std::size_t channelIndex) noexcept
    {
        channelIndex_.store(
            channelIndex,
            std::memory_order_release);
    }

    std::size_t
        AudioChannelSelector::channelIndex() const noexcept
    {
        return channelIndex_.load(
            std::memory_order_acquire);
    }

    void AudioChannelSelector::addConsumer(
        std::shared_ptr<IAudioConsumer> consumer)
    {
        if (!consumer)
        {
            return;
        }

        std::lock_guard lock(consumersMutex_);

        const auto existing =
            std::find(
                consumers_.begin(),
                consumers_.end(),
                consumer);

        if (existing == consumers_.end())
        {
            consumers_.push_back(
                std::move(consumer));
        }
    }

    void AudioChannelSelector::removeConsumer(
        const std::shared_ptr<IAudioConsumer>& consumer)
    {
        std::lock_guard lock(consumersMutex_);

        consumers_.erase(
            std::remove(
                consumers_.begin(),
                consumers_.end(),
                consumer),
            consumers_.end());
    }

    void AudioChannelSelector::clearConsumers()
    {
        std::lock_guard lock(consumersMutex_);
        consumers_.clear();
    }

    void AudioChannelSelector::onAudioBlock(
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

        const std::size_t selectedChannel =
            channelIndex_.load(
                std::memory_order_acquire);

        if (selectedChannel >= channelCount)
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

        std::vector<float> monoBuffer(
            availableFrameCount);

        for (std::size_t frame = 0;
            frame < availableFrameCount;
            ++frame)
        {
            monoBuffer[frame] =
                block.interleavedSamples[
                    frame * channelCount
                        + selectedChannel];
        }

        std::vector<std::shared_ptr<IAudioConsumer>>
            consumersSnapshot;

        {
            std::lock_guard lock(consumersMutex_);
            consumersSnapshot = consumers_;
        }

        AudioStreamFormat monoFormat =
            block.format;

        monoFormat.channelCount = 1;

        const AudioBlockView monoBlock{
            std::span<const float>(
                monoBuffer.data(),
                monoBuffer.size()),
            availableFrameCount,
            monoFormat
        };

        for (const auto& consumer : consumersSnapshot)
        {
            if (consumer)
            {
                consumer->onAudioBlock(
                    monoBlock);
            }
        }
    }

} // namespace timmy::audio