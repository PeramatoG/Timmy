/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/asr/compute_selection.h"

#include <algorithm>
#include <stdexcept>

namespace timmy::asr {
    std::string_view computeBackendId(ComputeBackend backend)
    {
        switch (backend) {
        case ComputeBackend::automatic: return "automatic";
        case ComputeBackend::cpu: return "cpu";
        case ComputeBackend::vulkan: return "vulkan";
        }
        throw std::invalid_argument("Unknown compute backend.");
    }

    ComputeBackend computeBackendFromId(std::string_view id)
    {
        for (auto backend : {ComputeBackend::automatic, ComputeBackend::cpu, ComputeBackend::vulkan})
            if (computeBackendId(backend) == id) return backend;
        throw std::invalid_argument("Unknown compute backend ID.");
    }

    ComputeDiagnostics initializeCompute(const ComputeSelection& requested,
        const ComputeDiscovery& discovery, const ComputeInitializer& initialize)
    {
        if (requested.deviceIndex && *requested.deviceIndex < 0)
            throw std::invalid_argument("Compute device index must be nonnegative.");
        (void)computeBackendId(requested.backend);
        ComputeDiagnostics result;
        result.requested = requested;
        result.discovery = discovery;
        const auto activate = [&](const ComputeDevice& device) {
            result.activeBackend = device.backend;
            if (device.index >= 0) result.activeDeviceIndex = device.index;
            result.activeDeviceId = device.id;
            result.activeDeviceName = device.name;
        };
        if (requested.backend != ComputeBackend::cpu) {
            const auto device = std::find_if(discovery.devices.begin(), discovery.devices.end(),
                [&](const auto& d) { return d.backend == ComputeBackend::vulkan
                    && (!requested.deviceIndex || d.index == *requested.deviceIndex); });
            if (discovery.vulkanAvailable && device != discovery.devices.end()) {
                try {
                    if (initialize(*device)) { activate(*device); return result; }
                    result.fallbackReason = "vulkan_initialization_failed";
                } catch (...) {
                    result.fallbackReason = "vulkan_initialization_exception";
                }
            } else {
                result.fallbackReason = requested.deviceIndex && discovery.vulkanAvailable
                    ? "requested_device_unavailable"
                    : discovery.unavailableReason.empty() ? "vulkan_unavailable" : discovery.unavailableReason;
            }
            result.fallbackUsed = true;
        }
        const ComputeDevice cpu{ComputeBackend::cpu, -1, "cpu", "CPU", "cpu"};
        try {
            if (initialize(cpu)) activate(cpu);
            else result.initializationFailure = "cpu_initialization_failed";
        } catch (...) {
            result.initializationFailure = "cpu_initialization_exception";
        }
        return result;
    }

    std::string computeStatus(const ComputeDiagnostics& diagnostics)
    {
        if (!diagnostics.activeBackend) return "Recognition device: unavailable";
        if (*diagnostics.activeBackend == ComputeBackend::vulkan)
            return "Recognition device: " + diagnostics.activeDeviceName + " · Vulkan";
        if (diagnostics.fallbackUsed)
            return "Vulkan unavailable — using CPU (" + diagnostics.fallbackReason + ")";
        return "Recognition device: CPU";
    }
}
