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

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace timmy::asr::detail {

    struct WhisperEndpointOptions
    {
        float speechThreshold = 0.5F;
        std::size_t minimumSpeechSamples = 16000U / 4U;
        std::size_t minimumSilenceSamples = 16000U / 10U;
        std::size_t speechPaddingSamples = 16000U * 30U / 1000U;
        std::size_t maximumUtteranceSamples = 16000U * 8U;
    };

    class WhisperEndpointDetector
    {
    public:
        explicit WhisperEndpointDetector(
            WhisperEndpointOptions options = {});

        [[nodiscard]] std::optional<std::vector<float>> process(
            std::span<const float> samples,
            float speechProbability);
        void reset();

    private:
        [[nodiscard]] std::optional<std::vector<float>> finish(
            bool maximumDurationReached);
        void appendPreRoll(std::span<const float> samples);

        WhisperEndpointOptions options_;
        std::vector<float> preRoll_;
        std::vector<float> utterance_;
        std::size_t speechSpanSamples_ = 0;
        std::size_t speechEndSamples_ = 0;
        std::size_t consecutiveSilenceSamples_ = 0;
        bool speaking_ = false;
        bool suppressUntilSilence_ = false;
    };

} // namespace timmy::asr::detail
