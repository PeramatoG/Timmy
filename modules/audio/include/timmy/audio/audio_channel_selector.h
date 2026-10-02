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

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace timmy::audio {

    class AudioChannelSelector final
        : public IAudioConsumer
    {
    public:
        explicit AudioChannelSelector(
            std::size_t channelIndex = 0);

        void setChannelIndex(std::size_t channelIndex) noexcept;

        [[nodiscard]]
        std::size_t channelIndex() const noexcept;

        void addConsumer(std::shared_ptr<IAudioConsumer> consumer);
        void removeConsumer(const std::shared_ptr<IAudioConsumer>& consumer);
        void clearConsumers();

        void onAudioBlock(const AudioBlockView& block) override;

    private:
        std::atomic<std::size_t> channelIndex_{ 0 };

        mutable std::mutex consumersMutex_;

        std::vector<std::shared_ptr<IAudioConsumer>>
            consumers_;
    };

} // namespace timmy::audio