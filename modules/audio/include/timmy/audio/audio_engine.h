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

#include "timmy/audio/audio_capture_config.h"
#include "timmy/audio/audio_diagnostic.h"
#include "timmy/audio/audio_consumer.h"
#include "timmy/audio/audio_stream_format.h"

#include <memory>
#include <optional>

namespace timmy::audio {

    struct AudioStartResult
    {
        AudioStreamFormat format;
        std::optional<AudioDiagnostic> diagnostic;

        // Reports whether capture started without a diagnostic.
        [[nodiscard]] bool ok() const noexcept
        {
            return !diagnostic.has_value();
        }
    };

    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        AudioEngine(AudioEngine&&) noexcept;
        AudioEngine& operator=(AudioEngine&&) noexcept;

        void addConsumer(std::shared_ptr<IAudioConsumer> consumer);
        void removeConsumer(const std::shared_ptr<IAudioConsumer>& consumer);
        void clearConsumers();

        [[nodiscard]]
        AudioStartResult start(const AudioCaptureConfig& config);

        void stop() noexcept;

        [[nodiscard]]
        bool isRunning() const noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace timmy::audio
