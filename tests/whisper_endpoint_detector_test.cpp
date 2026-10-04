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

    // Creates one 512-sample Silero v6.2.0 frame of identifiable PCM.
    std::vector<float> frame(float value)
    {
        return std::vector<float>(512, value);
    }

    // Verifies one positive frame starts speech through probability hysteresis.
    void testImmediateStartWithHysteresis()
    {
        WhisperEndpointDetector detector;
        require(!detector.process(frame(0.0F), 0.1F),
            "silence should not create a job");
        require(!detector.process(frame(1.0F), 0.5F),
            "the threshold crossing should start an open utterance");
        require(!detector.process(frame(1.0F), 0.42F),
            "the hysteresis band should preserve active speech");
        require(!detector.process(frame(1.0F), 0.49F),
            "fluctuating speech should remain active below the start threshold");
        require(!detector.process(frame(1.0F), 0.36F),
            "a valid speech span should not require consecutive positive frames");
        require(!detector.process(frame(1.0F), 0.45F),
            "speech in the hysteresis band should remain open");
        require(!detector.process(frame(1.0F), 0.38F),
            "more than 250 ms of fluctuating speech should remain open");
        require(!detector.process(frame(1.0F), 0.48F),
            "a valid speech span should remain active until real silence");
        require(!detector.process(frame(1.0F), 0.41F),
            "more than 250 ms should not require another positive frame");
        require(!detector.process(frame(0.0F), 0.1F),
            "short silence should not close valid speech");
        require(!detector.process(frame(0.0F), 0.1F),
            "less than 100 ms of silence should keep speech open");
        require(!detector.process(frame(0.0F), 0.1F),
            "three Silero frames are still below the silence endpoint");
        auto completed = detector.process(frame(0.0F), 0.1F);
        require(completed && completed->size() == 5056,
            "valid fluctuating speech should complete with pre-roll and padding");
    }

    // Verifies a candidate shorter than the minimum speech duration is dropped.
    void testShortSpeechIsDiscarded()
    {
        WhisperEndpointDetector detector;
        require(!detector.process(frame(1.0F), 0.8F),
            "a single threshold crossing should open a candidate");
        require(!detector.process(frame(0.0F), 0.1F),
            "initial silence should keep the short candidate pending");
        require(!detector.process(frame(0.0F), 0.1F),
            "continued silence should keep the short candidate pending");
        require(!detector.process(frame(0.0F), 0.1F),
            "three silence frames should remain below 100 ms");
        require(!detector.process(frame(0.0F), 0.1F),
            "a completed short candidate should be discarded");
    }

    // Verifies hysteresis-band frames alone never end an active utterance.
    void testHysteresisBandDoesNotEndSpeech()
    {
        WhisperEndpointDetector detector;
        require(!detector.process(frame(1.0F), 0.8F),
            "positive speech should start immediately");
        for (int index = 0; index < 8; ++index)
        {
            require(!detector.process(frame(1.0F), 0.4F),
                "hysteresis-band frames must not close active speech");
        }
    }

    // Verifies minimum low-probability silence closes one valid utterance once.
    void testSilenceClosesExactlyOnce()
    {
        WhisperEndpointDetector detector;
        for (int index = 0; index < 8; ++index)
        {
            require(!detector.process(frame(1.0F), index == 0 ? 0.8F : 0.4F),
                "valid speech should remain open before endpoint silence");
        }
        require(!detector.process(frame(0.0F), 0.1F),
            "less than the configured silence must not close speech");
        require(!detector.process(frame(0.0F), 0.1F),
            "two low-probability frames should remain below 100 ms");
        require(!detector.process(frame(0.0F), 0.1F),
            "three low-probability frames should remain below 100 ms");
        auto completed = detector.process(frame(0.0F), 0.1F);
        require(completed.has_value(),
            "configured low-probability silence should close valid speech");
        require(!detector.process(frame(0.0F), 0.1F),
            "a closed utterance must not be emitted twice");
    }

    // Verifies the command duration bound forces one exactly bounded segment.
    void testMaximumDuration()
    {
        WhisperEndpointDetector detector;
        std::optional<std::vector<float>> completed;
        for (int index = 0; index < 250; ++index)
        {
            completed = detector.process(frame(1.0F), 0.8F);
        }
        require(completed && completed->size() == 128000,
            "maximum duration should force an exactly bounded job");
        require(!detector.process(frame(1.0F), 0.8F),
            "continued speech after the bound should not create a duplicate");
    }

    // Verifies reset removes all endpoint state before detector reuse.
    void testResetAndReuse()
    {
        WhisperEndpointDetector detector;
        require(!detector.process(frame(1.0F), 0.8F),
            "speech should be active before reset");
        require(!detector.process(frame(0.0F), 0.1F),
            "partial endpoint silence should be active before reset");
        detector.reset();
        require(!detector.process(frame(0.0F), 0.1F),
            "reset should not retain endpoint silence or emit stale speech");
        require(!detector.process(frame(0.0F), 0.1F),
            "session reuse should remain idle after reset");
        for (int index = 0; index < 8; ++index)
        {
            require(!detector.process(frame(1.0F), index == 0 ? 0.8F : 0.4F),
                "reused endpoint state should accept a fresh utterance");
        }
        for (int index = 0; index < 3; ++index)
        {
            require(!detector.process(frame(0.0F), 0.1F),
                "fresh speech should remain open before minimum silence");
        }
        auto completed = detector.process(frame(0.0F), 0.1F);
        require(completed && completed->size() == 5056,
            "reused state should emit only the fresh padded utterance");
    }

} // namespace

// Runs deterministic model-free endpointing state tests.
int main()
{
    testImmediateStartWithHysteresis();
    testShortSpeechIsDiscarded();
    testHysteresisBandDoesNotEndSpeech();
    testSilenceClosesExactlyOnce();
    testMaximumDuration();
    testResetAndReuse();
    return EXIT_SUCCESS;
}
