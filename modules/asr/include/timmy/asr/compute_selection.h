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

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timmy::asr {

    enum class ComputeBackend { automatic, cpu, vulkan };
    [[nodiscard]] std::string_view computeBackendId(ComputeBackend backend);
    [[nodiscard]] ComputeBackend computeBackendFromId(std::string_view id);

    struct ComputeSelection
    {
        ComputeBackend backend = ComputeBackend::automatic;
        // GGML GPU ordinal, stable for a given runtime enumeration. Absent = automatic.
        std::optional<int> deviceIndex{};
    };

    struct ComputeDevice
    {
        ComputeBackend backend = ComputeBackend::cpu;
        int index = -1;
        std::string id;
        std::string name;
        std::string deviceClass;
    };

    struct ComputeDiscovery
    {
        bool vulkanCompiled = false;
        bool vulkanAvailable = false;
        std::vector<ComputeDevice> devices;
        std::string unavailableReason;
    };

    struct ComputeDiagnostics
    {
        ComputeSelection requested;
        std::optional<ComputeBackend> activeBackend{};
        std::optional<int> activeDeviceIndex{};
        std::string activeDeviceId;
        std::string activeDeviceName;
        bool fallbackUsed = false;
        std::string fallbackReason;
        ComputeDiscovery discovery;
        std::string initializationFailure;
    };

    // A failed initializer must release its candidate before returning/throwing.
    // True means the requested device actually initialized; never a silent CPU substitute.
    using ComputeInitializer = std::function<bool(const ComputeDevice&)>;
    [[nodiscard]] ComputeDiagnostics initializeCompute(const ComputeSelection& requested,
        const ComputeDiscovery& discovery, const ComputeInitializer& initialize);
    [[nodiscard]] std::string computeStatus(const ComputeDiagnostics& diagnostics);
}
