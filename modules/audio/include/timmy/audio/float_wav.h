/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace timmy::audio {
    struct FloatWav
    {
        std::uint32_t sampleRate = 16000;
        std::vector<float> samples;
    };
    // Diagnostic mono IEEE float32 RIFF/WAVE. Never call from a realtime callback.
    // Throws on malformed/unsupported input or I/O failure. Creation is exclusive:
    // an existing file is never truncated. Samples are preserved bit for bit.
    void writeFloatWav(const std::filesystem::path& path,
        std::span<const float> samples, std::uint32_t sampleRate = 16000);
    // Strict lossless input only: mono format 3, 32 bits, finite PCM, bounded size.
    // Unknown RIFF chunks are skipped; no conversion/resampling/quantization occurs.
    [[nodiscard]] FloatWav readFloatWav(const std::filesystem::path& path);
}
