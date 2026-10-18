/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "timmy/core/inference_audio.h"
#include "timmy/audio/float_wav.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace timmy::core {
    namespace {
        std::string safe(std::string_view text)
        {
            constexpr char hex[] = "0123456789abcdef";
            std::string result;
            for (unsigned char c : text) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || c == '-') result += static_cast<char>(c);
                else { result += '_'; result += hex[c >> 4]; result += hex[c & 15]; }
            }
            return result;
        }
    }
    std::filesystem::path inferenceAudioPath(const InferenceAudioCapture& capture,
        std::uint64_t inferenceIndex)
    {
        auto directory = capture.resultPath.stem();
        directory += "_audio";
        return directory / ("s-" + safe(capture.sessionId) + "_c-" + safe(capture.caseId)
            + "_a-" + std::to_string(capture.attempt) + "_g-" + std::to_string(capture.generation)
            + "_i-" + std::to_string(inferenceIndex) + ".wav");
    }
    asr::InferenceInputDiagnostics inspectInferenceAudio(const asr::InferenceInputView& input,
        const std::optional<InferenceAudioCapture>& capture)
    {
        asr::InferenceInputDiagnostics result;
        result.inferenceIndex = input.inferenceIndex;
        result.sampleRate = input.sampleRate;
        result.sampleCount = input.samples.size();
        double squares = 0;
        for (float sample : input.samples) {
            const auto magnitude = std::abs(static_cast<double>(sample));
            result.peakLinear = std::max(result.peakLinear, magnitude);
            squares += static_cast<double>(sample) * sample;
            if (magnitude >= 0.999) ++result.nearFullScaleSamples;
        }
        if (!input.samples.empty()) {
            result.rmsLinear = std::sqrt(squares / static_cast<double>(input.samples.size()));
            result.nearFullScaleRatio = static_cast<double>(result.nearFullScaleSamples)
                / static_cast<double>(input.samples.size());
        }
        if (result.peakLinear > 0) result.peakDbfs = 20 * std::log10(result.peakLinear);
        if (result.rmsLinear > 0) result.rmsDbfs = 20 * std::log10(result.rmsLinear);
        if (capture) {
            try {
                if (capture->generation != input.generation)
                    throw std::runtime_error("Diagnostic capture generation mismatch.");
                const auto relative = inferenceAudioPath(*capture, input.inferenceIndex);
                const auto destination = capture->resultPath.parent_path() / relative;
                std::error_code error;
                std::filesystem::create_directories(destination.parent_path(), error);
                if (error) throw std::runtime_error("Cannot create diagnostic WAV directory: " + error.message());
                audio::writeFloatWav(destination, input.samples, input.sampleRate);
                const auto utf8 = relative.generic_u8string();
                result.wavPath = std::string(utf8.begin(), utf8.end());
            } catch (const std::exception& error) { result.wavError = error.what(); }
        }
        return result;
    }
}
