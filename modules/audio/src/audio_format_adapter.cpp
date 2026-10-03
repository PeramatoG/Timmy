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

#include <miniaudio.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace timmy::audio {

    class AudioFormatAdapter::Impl
    {
    public:
        ~Impl();

        bool initialize(std::uint32_t sourceSampleRate);
        void reset();
        std::vector<float> process(std::span<const float> input);

        std::uint32_t sourceSampleRate = 0;

    private:
        ma_resampler resampler_{};
        bool initialized_ = false;
        std::vector<float> pendingInput_;
    };

    // Creates an adapter with isolated resampler implementation state.
    AudioFormatAdapter::AudioFormatAdapter()
        : impl_(std::make_unique<Impl>())
    {}

    // Releases the private resampler implementation.
    AudioFormatAdapter::~AudioFormatAdapter() = default;

    // Releases miniaudio resources held by the stateful resampler.
    AudioFormatAdapter::Impl::~Impl()
    {
        reset();
    }

    // Initializes a filtered mono float resampler for a source rate.
    bool AudioFormatAdapter::Impl::initialize(
        std::uint32_t newSourceSampleRate)
    {
        reset();

        ma_resampler_config config = ma_resampler_config_init(
            ma_format_f32,
            1,
            newSourceSampleRate,
            AudioFormatAdapter::targetSampleRate,
            ma_resample_algorithm_linear);
        config.linear.lpfOrder = 4;

        if (ma_resampler_init(&config, nullptr, &resampler_) != MA_SUCCESS)
        {
            return false;
        }

        sourceSampleRate = newSourceSampleRate;
        initialized_ = true;
        return true;
    }

    // Clears all filter, timing, and pending-input state.
    void AudioFormatAdapter::Impl::reset()
    {
        if (initialized_)
        {
            ma_resampler_uninit(&resampler_, nullptr);
        }

        resampler_ = {};
        initialized_ = false;
        sourceSampleRate = 0;
        pendingInput_.clear();
    }

    // Processes all possible pending input while respecting partial consumption.
    std::vector<float> AudioFormatAdapter::Impl::process(
        std::span<const float> input)
    {
        pendingInput_.insert(
            pendingInput_.end(),
            input.begin(),
            input.end());

        std::vector<float> output;
        std::size_t consumedTotal = 0;

        while (consumedTotal < pendingInput_.size())
        {
            const std::size_t remaining =
                pendingInput_.size() - consumedTotal;
            const std::uint64_t scaledCapacity =
                (static_cast<std::uint64_t>(remaining)
                    * AudioFormatAdapter::targetSampleRate
                    + sourceSampleRate - 1)
                / sourceSampleRate;
            const std::uint64_t requestedOutput =
                std::max<std::uint64_t>(scaledCapacity + 64, 64);
            const std::size_t outputOffset = output.size();
            output.resize(
                outputOffset
                + static_cast<std::size_t>(requestedOutput));

            ma_uint64 inputFrameCount =
                static_cast<ma_uint64>(remaining);
            ma_uint64 outputFrameCount =
                static_cast<ma_uint64>(requestedOutput);
            const ma_result result = ma_resampler_process_pcm_frames(
                &resampler_,
                pendingInput_.data() + consumedTotal,
                &inputFrameCount,
                output.data() + outputOffset,
                &outputFrameCount);

            if (result != MA_SUCCESS)
            {
                output.resize(outputOffset);
                break;
            }

            output.resize(
                outputOffset
                + static_cast<std::size_t>(outputFrameCount));
            consumedTotal += static_cast<std::size_t>(inputFrameCount);

            if (inputFrameCount == 0)
            {
                break;
            }
        }

        pendingInput_.erase(
            pendingInput_.begin(),
            pendingInput_.begin()
                + static_cast<std::ptrdiff_t>(consumedTotal));
        return output;
    }

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

                if (impl_->sourceSampleRate != targetSampleRate)
                {
                    impl_->reset();
                    impl_->sourceSampleRate = targetSampleRate;
                }
            }

            dispatch(input);
            return;
        }

        std::vector<float> output;

        {
            std::lock_guard lock(stateMutex_);

            if (impl_->sourceSampleRate != block.format.sampleRate
                && !impl_->initialize(block.format.sampleRate))
            {
                return;
            }

            output = impl_->process(input);
        }

        dispatch(output);
    }

    // Clears streaming history so the next block starts a new timeline.
    void AudioFormatAdapter::reset()
    {
        std::lock_guard lock(stateMutex_);
        impl_->reset();
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
