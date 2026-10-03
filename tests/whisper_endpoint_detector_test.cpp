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

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

    using timmy::asr::detail::WhisperEndpointDetector;
    using timmy::asr::detail::WhisperEndpointOptions;

    // Fails the test process with a focused endpointing message.
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    // Creates compact endpoint timings for deterministic sample-level tests.
    WhisperEndpointOptions testOptions()
    {
        return { 0.5F, 4, 3, 2, 12 };
    }

    // Verifies pre-roll, speech accumulation, padding, and one completed job.
    void testSpeechEndpoint()
    {
        WhisperEndpointDetector detector(testOptions());
        const std::vector<float> silence{ 0.0F, 0.0F };
        const std::vector<float> speech{ 1.0F, 1.0F };
        require(!detector.process(silence, 0.1F),
            "silence should not create a job");
        require(!detector.process(speech, 0.9F),
            "unconfirmed speech should not create a job");
        require(!detector.process(speech, 0.9F),
            "speech start should only begin accumulation");
        require(!detector.process(speech, 0.9F),
            "active speech should remain open");
        require(!detector.process(silence, 0.1F),
            "short silence should not close speech");

        const std::vector<float> lastSilence{ 0.0F };
        auto completed = detector.process(lastSilence, 0.1F);
        require(completed && completed->size() == 10,
            "end-of-speech should preserve pre-roll and configured padding");
        require(!detector.process(silence, 0.1F),
            "completed speech should not emit a duplicate job");
    }

    // Verifies the command duration bound forces a single completed segment.
    void testMaximumDuration()
    {
        WhisperEndpointDetector detector(testOptions());
        const std::vector<float> speech{ 1.0F, 1.0F, 1.0F, 1.0F };
        require(!detector.process(speech, 0.9F),
            "minimum speech should activate without immediately completing");
        require(!detector.process(speech, 0.9F),
            "speech below the maximum should remain open");
        auto completed = detector.process(speech, 0.9F);
        require(completed && completed->size() == 12,
            "maximum duration should force an exactly bounded job");
        require(!detector.process(speech, 0.9F),
            "continued speech after the bound should not create a duplicate");
        require(!detector.process(std::vector<float>{ 0.0F }, 0.1F),
            "forced completion should wait for sufficient silence");
        require(!detector.process(std::vector<float>{ 0.0F, 0.0F }, 0.1F),
            "sufficient silence should rearm without creating a job");
    }

    // Verifies reset discards an in-progress utterance on backend stop.
    void testReset()
    {
        WhisperEndpointDetector detector(testOptions());
        const std::vector<float> speech{ 1.0F, 1.0F, 1.0F, 1.0F };
        require(!detector.process(speech, 0.9F),
            "speech should be active before reset");
        detector.reset();
        const std::vector<float> silence{ 0.0F, 0.0F, 0.0F };
        require(!detector.process(silence, 0.1F),
            "reset should discard an active utterance without a job");
    }

} // namespace

// Runs deterministic model-free endpointing state tests.
int main()
{
    testSpeechEndpoint();
    testMaximumDuration();
    testReset();
    return EXIT_SUCCESS;
}
