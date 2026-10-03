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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

    class CollectingConsumer final
        : public timmy::audio::IAudioConsumer
    {
    public:
        // Copies adapted samples and metadata while their block view is valid.
        void onAudioBlock(
            const timmy::audio::AudioBlockView& block) override
        {
            formats.push_back(block.format);
            blockFrameCounts.push_back(block.frameCount);
            samples.insert(
                samples.end(),
                block.interleavedSamples.begin(),
                block.interleavedSamples.end());
        }

        std::vector<float> samples;
        std::vector<std::size_t> blockFrameCounts;
        std::vector<timmy::audio::AudioStreamFormat> formats;
    };

    // Reports a failed expectation and returns a value suitable for main().
    int fail(std::string_view message)
    {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }

    // Compares audio samples with a deterministic floating-point tolerance.
    bool nearlyEqual(float left, float right)
    {
        return std::abs(left - right) <= 0.00001F;
    }

    // Generates a non-repeating ramp that makes interpolation errors visible.
    std::vector<float> makeInput(std::size_t frameCount)
    {
        std::vector<float> input(frameCount);

        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            input[frame] =
                static_cast<float>(frame) * 0.003F - 0.4F;
        }

        return input;
    }

    // Computes the deterministic linear interpolation expected from one timeline.
    std::vector<float> makeExpected(
        std::span<const float> input,
        std::uint32_t sourceRate)
    {
        std::vector<float> expected;
        std::uint64_t outputFrame = 0;

        while (true)
        {
            const std::uint64_t numerator = outputFrame * sourceRate;
            const std::size_t lower = static_cast<std::size_t>(
                numerator / timmy::audio::AudioFormatAdapter::targetSampleRate);
            const std::uint32_t remainder = static_cast<std::uint32_t>(
                numerator % timmy::audio::AudioFormatAdapter::targetSampleRate);

            if (lower >= input.size()
                || (remainder != 0 && lower + 1 >= input.size()))
            {
                break;
            }

            float sample = input[lower];

            if (remainder != 0)
            {
                const float fraction =
                    static_cast<float>(remainder)
                    / static_cast<float>(
                        timmy::audio::AudioFormatAdapter::targetSampleRate);
                sample += (input[lower + 1] - input[lower]) * fraction;
            }

            expected.push_back(sample);
            ++outputFrame;
        }

        return expected;
    }

    // Sends one logical stream using consecutive blocks of requested sizes.
    void sendBlocks(
        timmy::audio::AudioFormatAdapter& adapter,
        std::span<const float> input,
        std::uint32_t sourceRate,
        std::span<const std::size_t> blockSizes)
    {
        std::size_t offset = 0;

        for (const std::size_t requestedSize : blockSizes)
        {
            const std::size_t blockSize =
                std::min(requestedSize, input.size() - offset);
            const timmy::audio::AudioBlockView block{
                input.subspan(offset, blockSize),
                blockSize,
                { sourceRate, 1 }
            };
            adapter.onAudioBlock(block);
            offset += blockSize;
        }
    }

    // Verifies a source rate across boundaries, output counts, and metadata.
    int verifyResampling(
        std::uint32_t sourceRate,
        std::span<const std::size_t> blockSizes,
        std::size_t expectedFrameCount)
    {
        std::size_t inputFrameCount = 0;

        for (const std::size_t size : blockSizes)
        {
            inputFrameCount += size;
        }

        const std::vector<float> input = makeInput(inputFrameCount);
        const std::vector<float> expected = makeExpected(input, sourceRate);
        auto consumer = std::make_shared<CollectingConsumer>();
        timmy::audio::AudioFormatAdapter adapter;
        adapter.addConsumer(consumer);
        sendBlocks(adapter, input, sourceRate, blockSizes);

        if (expected.size() != expectedFrameCount
            || consumer->samples.size() != expectedFrameCount)
        {
            return fail("resampling produced an unexpected frame count");
        }

        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (!nearlyEqual(consumer->samples[index], expected[index]))
            {
                return fail("resampling lost continuity across block boundaries");
            }
        }

        for (const auto& format : consumer->formats)
        {
            if (format.sampleRate != 16000 || format.channelCount != 1)
            {
                return fail("adapted stream metadata was not mono 16 kHz");
            }
        }

        return 0;
    }

} // namespace

// Verifies deterministic, hardware-free audio format adaptation contracts.
int main()
{
    constexpr std::size_t blocks48k[]{ 101, 137, 242 };
    constexpr std::size_t blocks44k[]{ 100, 73, 268 };

    if (verifyResampling(48000, blocks48k, 160) != 0
        || verifyResampling(44100, blocks44k, 160) != 0)
    {
        return 1;
    }

    auto passThroughConsumer = std::make_shared<CollectingConsumer>();
    timmy::audio::AudioFormatAdapter passThroughAdapter;
    passThroughAdapter.addConsumer(passThroughConsumer);
    const std::vector<float> passThroughInput = makeInput(37);
    constexpr std::size_t passThroughBlocks[]{ 11, 26 };
    sendBlocks(
        passThroughAdapter,
        passThroughInput,
        16000,
        passThroughBlocks);

    if (passThroughConsumer->samples != passThroughInput
        || passThroughConsumer->blockFrameCounts
            != std::vector<std::size_t>{ 11, 26 }
        || passThroughConsumer->formats.size() != 2
        || passThroughConsumer->formats[0].sampleRate != 16000
        || passThroughConsumer->formats[0].channelCount != 1)
    {
        return fail("16 kHz input was not passed through unchanged");
    }

    auto resetConsumer = std::make_shared<CollectingConsumer>();
    timmy::audio::AudioFormatAdapter resetAdapter;
    resetAdapter.addConsumer(resetConsumer);
    const std::vector<float> oldInput = makeInput(2);
    constexpr std::size_t oldBlock[]{ 2 };
    sendBlocks(resetAdapter, oldInput, 44100, oldBlock);
    resetAdapter.reset();
    resetConsumer->samples.clear();
    const std::vector<float> restartedInput{ 0.75F, -0.25F, 0.5F, 0.0F };
    constexpr std::size_t restartedBlock[]{ 4 };
    sendBlocks(resetAdapter, restartedInput, 48000, restartedBlock);

    if (resetConsumer->samples.size() != 2
        || !nearlyEqual(resetConsumer->samples[0], 0.75F)
        || !nearlyEqual(resetConsumer->samples[1], 0.0F))
    {
        return fail("reset did not start a fresh resampling timeline");
    }

    const std::size_t validSampleCount = resetConsumer->samples.size();
    resetAdapter.onAudioBlock({ {}, 0, { 48000, 1 } });
    resetAdapter.onAudioBlock({ restartedInput, 4, { 0, 1 } });
    resetAdapter.onAudioBlock({ restartedInput, 4, { 48000, 2 } });
    resetAdapter.onAudioBlock({ restartedInput, 0, { 48000, 1 } });

    if (resetConsumer->samples.size() != validSampleCount)
    {
        return fail("empty or invalid input unexpectedly produced output");
    }

    std::cout << "PASS: deterministic audio format adaptation\n";
    return 0;
}
