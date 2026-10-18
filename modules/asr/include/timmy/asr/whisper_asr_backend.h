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

#include "timmy/asr/asr_backend.h"
#include "timmy/asr/configurable_settings.h"

#include <filesystem>
#include <memory>

namespace timmy::asr {

    [[nodiscard]] const std::vector<SettingDescriptor>& whisperSettingDescriptors();
    [[nodiscard]] ComputeDiscovery whisperComputeDevices();

    struct WhisperAsrBackendOptions
    {
        std::filesystem::path vadModelPath;
        SettingValues settings;
    };

    class WhisperAsrBackend final
        : public IAsrBackend
    {
    public:
        explicit WhisperAsrBackend(WhisperAsrBackendOptions options = {});
        ~WhisperAsrBackend() override;

        WhisperAsrBackend(const WhisperAsrBackend&) = delete;
        WhisperAsrBackend& operator=(const WhisperAsrBackend&) = delete;

        [[nodiscard]] AsrCapabilities capabilities() const override;
        [[nodiscard]] AsrState state() const override;
        AsrOperationResult start(const AsrConfiguration& configuration) override;
        AsrOperationResult stop() override;
        AsrOperationResult resetAttempt(AttemptGeneration generation) override;
        AttemptDiagnostics attemptDiagnostics() const override;
        AsrOperationResult submitInferenceInput(std::span<const float> samples) override;

    private:
        void processAudioBlock(const audio::AudioBlockView& block) override;

        class Impl;
        std::unique_ptr<Impl> impl_;
    };

} // namespace timmy::asr
