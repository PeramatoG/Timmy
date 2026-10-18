/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/audio/float_wav.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace timmy::audio {
    namespace {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        // Diagnostic utterances, not arbitrary recordings (ten minutes at 16 kHz).
        constexpr std::uint32_t maximumSamples = 16000U * 600U;
        using Bytes = std::array<unsigned char, 4>;
        struct CloseFile { void operator()(std::FILE* file) const { std::fclose(file); } };
        Bytes little(std::uint32_t value)
        {
            return {static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8),
                static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 24)};
        }
        std::uint32_t integer(const unsigned char* p)
        {
            return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8)
                | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
        }
        std::uint16_t shortInteger(const unsigned char* p)
        { return static_cast<std::uint16_t>(p[0] | (unsigned{p[1]} << 8)); }
        void check(bool condition)
        { if (!condition) throw std::runtime_error("Invalid or unsupported mono float32 WAV."); }
    }

    void writeFloatWav(const std::filesystem::path& path,
        std::span<const float> samples, std::uint32_t sampleRate)
    {
        if (samples.empty() || samples.size() > maximumSamples || sampleRate == 0
            || sampleRate > std::numeric_limits<std::uint32_t>::max() / 4)
            throw std::invalid_argument("Invalid diagnostic WAV size or sample rate.");
        for (float sample : samples)
            if (!std::isfinite(sample)) throw std::invalid_argument("Nonfinite diagnostic PCM.");
#ifdef _WIN32
        std::FILE* raw = nullptr;
        if (_wfopen_s(&raw, path.c_str(), L"wbx") != 0) raw = nullptr;
#else
        auto* raw = std::fopen(path.c_str(), "wbx");
#endif
        if (!raw) throw std::runtime_error("Cannot exclusively create diagnostic WAV (file exists or path is not writable).");
        std::unique_ptr<std::FILE, CloseFile> output(raw);
        const auto write = [&](const void* data, std::size_t size) {
            if (std::fwrite(data, 1, size, output.get()) != size)
                throw std::runtime_error("Cannot write diagnostic WAV.");
        };
        const auto word = [&](std::uint32_t value) { const auto bytes = little(value); write(bytes.data(), bytes.size()); };
        try {
            const auto size = static_cast<std::uint32_t>(samples.size());
            write("RIFF", 4); word(48 + size * 4); write("WAVEfmt ", 8); word(16);
            const unsigned char format[]{3, 0, 1, 0};
            write(format, 4); word(sampleRate); word(sampleRate * 4);
            const unsigned char alignment[]{4, 0, 32, 0};
            write(alignment, 4);
            write("fact", 4); word(4); word(size);
            write("data", 4); word(size * 4);
            for (float sample : samples) word(std::bit_cast<std::uint32_t>(sample));
            if (std::fflush(output.get()) != 0) throw std::runtime_error("Cannot flush diagnostic WAV.");
            if (std::fclose(output.release()) != 0) throw std::runtime_error("Cannot close diagnostic WAV.");
        } catch (...) {
            // Close before removing partial files (also required on Windows).
            if (output) std::fclose(output.release());
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            throw;
        }
    }

    FloatWav readFloatWav(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open input WAV.");
        const auto fileSize = input.tellg();
        check(fileSize >= 12 && fileSize <= static_cast<std::streamoff>(maximumSamples) * 4 + 65536);
        input.seekg(0);
        std::array<unsigned char, 12> header{};
        input.read(reinterpret_cast<char*>(header.data()), header.size());
        const auto tag = [](const unsigned char* data, std::string_view value) {
            return std::string_view(reinterpret_cast<const char*>(data), 4) == value;
        };
        check(tag(header.data(), "RIFF") && tag(header.data() + 8, "WAVE"));
        const std::uint64_t riffEnd = std::uint64_t{integer(header.data() + 4)} + 8;
        check(riffEnd == static_cast<std::uint64_t>(fileSize));
        FloatWav result;
        bool haveFormat = false, haveData = false;
        std::uint64_t offset = 12;
        while (offset + 8 <= riffEnd) {
            std::array<unsigned char, 8> chunk{};
            input.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
            const auto size = integer(chunk.data() + 4);
            offset += 8;
            const auto padded = std::uint64_t{size} + (size & 1U);
            check(input.good() && padded <= riffEnd - offset);
            if (tag(chunk.data(), "fmt ")) {
                check(!haveFormat && size >= 16);
                std::array<unsigned char, 16> format{};
                input.read(reinterpret_cast<char*>(format.data()), format.size());
                check(shortInteger(format.data()) == 3 && shortInteger(format.data() + 2) == 1
                    && shortInteger(format.data() + 12) == 4 && shortInteger(format.data() + 14) == 32);
                result.sampleRate = integer(format.data() + 4);
                check(result.sampleRate > 0 && result.sampleRate <= std::numeric_limits<std::uint32_t>::max() / 4
                    && integer(format.data() + 8) == result.sampleRate * 4);
                haveFormat = true;
            } else if (tag(chunk.data(), "data")) {
                check(haveFormat && !haveData && size > 0 && size % 4 == 0 && size / 4 <= maximumSamples);
                result.samples.resize(size / 4);
                for (auto& sample : result.samples) {
                    Bytes bytes{};
                    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
                    sample = std::bit_cast<float>(integer(bytes.data()));
                    check(input.good() && std::isfinite(sample));
                }
                haveData = true;
            }
            offset += padded;
            input.seekg(static_cast<std::streamoff>(offset));
        }
        check(offset == riffEnd && haveFormat && haveData && input.good());
        return result;
    }
}
