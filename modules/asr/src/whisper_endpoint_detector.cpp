/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_endpoint_detector.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace timmy::asr::detail {

    // Creates an endpoint detector with bounded command-oriented timing.
    WhisperEndpointDetector::WhisperEndpointDetector(
        WhisperEndpointOptions options)
        : options_(options)
    {
    }

    // Accumulates classified PCM and returns each completed utterance once.
    std::optional<std::vector<float>> WhisperEndpointDetector::process(
        std::span<const float> samples,
        float speechProbability)
    {
        if (samples.empty())
        {
            return std::nullopt;
        }

        const float negativeThreshold = std::max(
            options_.speechThreshold - 0.15F, 0.01F);
        const bool positiveSpeech =
            speechProbability >= options_.speechThreshold;
        const bool negativeSpeech = speechProbability < negativeThreshold;
        if (suppressUntilSilence_)
        {
            if (positiveSpeech)
            {
                consecutiveSilenceSamples_ = 0;
            }
            else if (negativeSpeech)
            {
                consecutiveSilenceSamples_ += samples.size();
            }
            if (consecutiveSilenceSamples_ >= options_.minimumSilenceSamples)
            {
                suppressUntilSilence_ = false;
                consecutiveSilenceSamples_ = 0;
                appendPreRoll(samples.last(std::min(
                    samples.size(), options_.speechPaddingSamples)));
            }
            return std::nullopt;
        }

        if (!speaking_)
        {
            if (!positiveSpeech)
            {
                appendPreRoll(samples);
                return std::nullopt;
            }

            speaking_ = true;
            utterance_ = std::move(preRoll_);
            utterance_.insert(
                utterance_.end(), samples.begin(), samples.end());
            speechSpanSamples_ = samples.size();
            if (utterance_.size() >= options_.maximumUtteranceSamples)
            {
                return finish(true);
            }
            return std::nullopt;
        }

        utterance_.insert(utterance_.end(), samples.begin(), samples.end());
        speechSpanSamples_ += samples.size();
        if (positiveSpeech)
        {
            consecutiveSilenceSamples_ = 0;
            speechEndSamples_ = 0;
        }
        else if (negativeSpeech)
        {
            if (speechEndSamples_ == 0)
            {
                speechEndSamples_ = speechSpanSamples_ - samples.size();
            }
            consecutiveSilenceSamples_ += samples.size();
        }
        if (utterance_.size() >= options_.maximumUtteranceSamples)
        {
            return finish(true);
        }
        if (consecutiveSilenceSamples_ >= options_.minimumSilenceSamples)
        {
            return finish(false);
        }
        return std::nullopt;
    }

    // Clears all speech, silence, and pre-roll state between sessions.
    void WhisperEndpointDetector::reset()
    {
        preRoll_.clear();
        utterance_.clear();
        speechSpanSamples_ = 0;
        speechEndSamples_ = 0;
        consecutiveSilenceSamples_ = 0;
        speaking_ = false;
        suppressUntilSilence_ = false;
    }

    // Completes one utterance and resets state without duplicating the job.
    std::optional<std::vector<float>> WhisperEndpointDetector::finish(
        bool maximumDurationReached)
    {
        const std::size_t candidateSpeechSamples = speechEndSamples_ == 0
            ? speechSpanSamples_
            : speechEndSamples_;
        const bool validSpeech =
            candidateSpeechSamples >= options_.minimumSpeechSamples;
        if (!maximumDurationReached
            && consecutiveSilenceSamples_ > options_.speechPaddingSamples)
        {
            const std::size_t trimCount = consecutiveSilenceSamples_
                - options_.speechPaddingSamples;
            utterance_.resize(utterance_.size() - trimCount);
        }
        if (utterance_.size() > options_.maximumUtteranceSamples)
        {
            utterance_.resize(options_.maximumUtteranceSamples);
        }

        auto completed = std::move(utterance_);
        reset();
        suppressUntilSilence_ = maximumDurationReached;
        if (!validSpeech)
        {
            return std::nullopt;
        }
        return completed;
    }

    // Retains only enough leading silence to provide the configured padding.
    void WhisperEndpointDetector::appendPreRoll(
        std::span<const float> samples)
    {
        preRoll_.insert(preRoll_.end(), samples.begin(), samples.end());
        if (preRoll_.size() > options_.speechPaddingSamples)
        {
            preRoll_.erase(
                preRoll_.begin(),
                preRoll_.begin() + static_cast<std::ptrdiff_t>(
                    preRoll_.size() - options_.speechPaddingSamples));
        }
    }

} // namespace timmy::asr::detail
