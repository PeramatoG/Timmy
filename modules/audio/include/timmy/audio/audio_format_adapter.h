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
#include <memory>
#include <mutex>
#include <vector>

namespace timmy::audio {

    class AudioFormatAdapter final
        : public IAudioConsumer
    {
    public:
        static constexpr std::uint32_t targetSampleRate = 16000;

        void addConsumer(std::shared_ptr<IAudioConsumer> consumer);
        void removeConsumer(const std::shared_ptr<IAudioConsumer>& consumer);
        void clearConsumers();

        void onAudioBlock(const AudioBlockView& block) override;

        void reset();

    private:
        void resetResampler(std::uint32_t sourceSampleRate);
        void dispatch(std::span<const float> samples);

        std::mutex stateMutex_;
        std::uint32_t sourceSampleRate_ = 0;
        std::uint64_t bufferedFrameOffset_ = 0;
        std::uint64_t receivedFrameCount_ = 0;
        std::uint64_t nextOutputFrame_ = 0;
        std::vector<float> inputBuffer_;

        std::mutex consumersMutex_;
        std::vector<std::shared_ptr<IAudioConsumer>> consumers_;
    };

} // namespace timmy::audio
