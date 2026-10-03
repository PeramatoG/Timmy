/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_format_adapter.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace timmy::audio {

    // Adds a downstream consumer if it is valid and not already registered.
    void AudioFormatAdapter::addConsumer(
        std::shared_ptr<IAudioConsumer> consumer)
    {
        if (!consumer)
        {
            return;
        }

        std::lock_guard lock(consumersMutex_);

        if (std::find(consumers_.begin(), consumers_.end(), consumer)
            == consumers_.end())
        {
            consumers_.push_back(std::move(consumer));
        }
    }

    // Removes a downstream consumer from the adapter.
    void AudioFormatAdapter::removeConsumer(
        const std::shared_ptr<IAudioConsumer>& consumer)
    {
        std::lock_guard lock(consumersMutex_);
        consumers_.erase(
            std::remove(consumers_.begin(), consumers_.end(), consumer),
            consumers_.end());
    }

    // Removes all downstream consumers from the adapter.
    void AudioFormatAdapter::clearConsumers()
    {
        std::lock_guard lock(consumersMutex_);
        consumers_.clear();
    }

    // Adapts a mono float block to a continuous 16 kHz mono stream.
    void AudioFormatAdapter::onAudioBlock(
        const AudioBlockView& block)
    {
        if (block.format.channelCount != 1
            || block.format.sampleRate == 0
            || block.frameCount == 0
            || block.interleavedSamples.empty())
        {
            return;
        }

        const std::size_t availableFrameCount =
            std::min(block.frameCount, block.interleavedSamples.size());

        if (availableFrameCount == 0)
        {
            return;
        }

        const std::span<const float> input(
            block.interleavedSamples.data(),
            availableFrameCount);

        if (block.format.sampleRate == targetSampleRate)
        {
            {
                std::lock_guard lock(stateMutex_);

                if (sourceSampleRate_ != targetSampleRate)
                {
                    resetResampler(targetSampleRate);
                }
            }

            dispatch(input);
            return;
        }

        std::vector<float> output;

        {
            std::lock_guard lock(stateMutex_);

            if (sourceSampleRate_ != block.format.sampleRate)
            {
                resetResampler(block.format.sampleRate);
            }

            inputBuffer_.insert(
                inputBuffer_.end(),
                input.begin(),
                input.end());
            receivedFrameCount_ += availableFrameCount;

            while (true)
            {
                const std::uint64_t positionNumerator =
                    nextOutputFrame_
                    * static_cast<std::uint64_t>(sourceSampleRate_);
                const std::uint64_t lowerFrame =
                    positionNumerator / targetSampleRate;
                const std::uint32_t remainder =
                    static_cast<std::uint32_t>(
                        positionNumerator % targetSampleRate);

                if (lowerFrame >= receivedFrameCount_
                    || (remainder != 0
                        && lowerFrame + 1 >= receivedFrameCount_))
                {
                    break;
                }

                const std::size_t lowerIndex =
                    static_cast<std::size_t>(
                        lowerFrame - bufferedFrameOffset_);
                const float lowerSample = inputBuffer_[lowerIndex];
                float sample = lowerSample;

                if (remainder != 0)
                {
                    const float fraction =
                        static_cast<float>(remainder)
                        / static_cast<float>(targetSampleRate);
                    sample +=
                        (inputBuffer_[lowerIndex + 1] - lowerSample)
                        * fraction;
                }

                output.push_back(sample);
                ++nextOutputFrame_;
            }

            const std::uint64_t nextPositionNumerator =
                nextOutputFrame_
                * static_cast<std::uint64_t>(sourceSampleRate_);
            const std::uint64_t nextLowerFrame =
                nextPositionNumerator / targetSampleRate;
            const std::uint64_t discardCount =
                std::min(
                    nextLowerFrame - bufferedFrameOffset_,
                    static_cast<std::uint64_t>(inputBuffer_.size()));

            inputBuffer_.erase(
                inputBuffer_.begin(),
                inputBuffer_.begin()
                    + static_cast<std::ptrdiff_t>(discardCount));
            bufferedFrameOffset_ += discardCount;
        }

        dispatch(output);
    }

    // Clears streaming history so the next block starts a new timeline.
    void AudioFormatAdapter::reset()
    {
        std::lock_guard lock(stateMutex_);
        resetResampler(0);
    }

    // Initializes empty streaming state for a source sample rate.
    void AudioFormatAdapter::resetResampler(
        std::uint32_t sourceSampleRate)
    {
        sourceSampleRate_ = sourceSampleRate;
        bufferedFrameOffset_ = 0;
        receivedFrameCount_ = 0;
        nextOutputFrame_ = 0;
        inputBuffer_.clear();
    }

    // Delivers a normalized mono block to a stable consumer snapshot.
    void AudioFormatAdapter::dispatch(
        std::span<const float> samples)
    {
        if (samples.empty())
        {
            return;
        }

        std::vector<std::shared_ptr<IAudioConsumer>> consumersSnapshot;

        {
            std::lock_guard lock(consumersMutex_);
            consumersSnapshot = consumers_;
        }

        const AudioBlockView outputBlock{
            samples,
            samples.size(),
            { targetSampleRate, 1 }
        };

        for (const auto& consumer : consumersSnapshot)
        {
            if (consumer)
            {
                consumer->onAudioBlock(outputBlock);
            }
        }
    }

} // namespace timmy::audio
