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

#include "timmy/asr/asr_result_listener.h"
#include "timmy/audio/audio_consumer.h"

#include <memory>
#include <mutex>

namespace timmy::asr {

    class IAsrBackend
        : public audio::IAudioConsumer
    {
    public:
        static constexpr std::uint32_t requiredSampleRate = 16000;
        static constexpr std::uint32_t requiredChannelCount = 1;

        // Allows backends to be destroyed through the common contract.
        ~IAsrBackend() override = default;

        virtual AsrCapabilities capabilities() const = 0;
        virtual AsrState state() const = 0;
        virtual AsrOperationResult start(
            const AsrConfiguration& configuration) = 0;
        virtual AsrOperationResult stop() = 0;

        void setResultListener(
            std::shared_ptr<IAsrResultListener> listener);

        // Validates Timmy-supplied float PCM before handing it to the backend.
        // This method is called by the audio pipeline worker, never by the
        // realtime device callback; implementations must preserve that rule.
        void onAudioBlock(const audio::AudioBlockView& block) final;

    protected:
        virtual void processAudioBlock(
            const audio::AudioBlockView& block) = 0;

        void publishResult(const AsrResult& result) const;
        void publishDiagnostic(const AsrDiagnostic& diagnostic) const;

    private:
        std::shared_ptr<IAsrResultListener> resultListener() const;

        mutable std::mutex listenerMutex_;
        std::shared_ptr<IAsrResultListener> listener_;
    };

} // namespace timmy::asr
