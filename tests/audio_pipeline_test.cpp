/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_channel_selector.h"
#include "timmy/audio/audio_meter.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

    // Reports a failed expectation and returns a value suitable for main().
    int fail(std::string_view message)
    {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }

    // Compares floating-point audio levels with a small deterministic tolerance.
    bool nearlyEqual(float left, float right)
    {
        constexpr float tolerance = 0.00001F;
        return std::abs(left - right) <= tolerance;
    }

} // namespace

// Verifies channel selection and metering using only in-memory sample data.
int main()
{
    auto meter = std::make_shared<timmy::audio::AudioMeter>();
    timmy::audio::AudioChannelSelector selector(1);
    selector.addConsumer(meter);

    const std::vector<float> stereoSamples{
        0.10F, -0.50F,
        0.20F, 0.50F,
        0.30F, 1.50F,
        0.40F, -1.00F
    };

    const timmy::audio::AudioBlockView input{
        std::span<const float>(stereoSamples),
        4,
        { 48000, 2 }
    };

    selector.onAudioBlock(input);

    const timmy::audio::AudioMeterSnapshot snapshot =
        meter->snapshot();

    if (snapshot.format.sampleRate != 48000
        || snapshot.format.channelCount != 1)
    {
        return fail("the selected stream format was not preserved as mono");
    }

    if (snapshot.processedFrameCount != 4
        || snapshot.channels.size() != 1)
    {
        return fail("the meter did not process the expected mono frames");
    }

    if (!nearlyEqual(snapshot.channels[0].peak, 1.0F))
    {
        return fail("the peak level was not clamped to one");
    }

    const float expectedRms =
        std::sqrt((0.25F + 0.25F + 1.0F + 1.0F) / 4.0F);

    if (!nearlyEqual(snapshot.channels[0].rms, expectedRms))
    {
        return fail("the RMS level did not match the selected samples");
    }

    std::cout << "PASS: deterministic audio pipeline\n";
    return 0;
}
