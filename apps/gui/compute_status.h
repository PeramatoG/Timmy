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

#include "timmy/asr/compute_selection.h"

namespace timmy::gui {

    inline std::string computeReasonText(std::string_view reason)
    {
        if (reason == "vulkan_not_compiled")
            return "Vulkan support is not included in this build";
        if (reason == "vulkan_initialization_failed" || reason == "vulkan_initialization_exception")
            return "Vulkan initialization failed; using CPU";
        if (reason == "requested_device_unavailable")
            return "The selected Vulkan device is unavailable";
        if (reason == "cpu_initialization_failed" || reason == "cpu_initialization_exception")
            return "CPU initialization failed";
        return "Vulkan is unavailable on this system";
    }

    // Presentation only: discovery supplies readiness, runtime diagnostics supply
    // the confirmed active backend. Never infer successful initialization from discovery.
    inline std::string recognitionComputeStatus(const asr::ComputeSelection& selection,
        const asr::ComputeDiscovery& discovery,
        const std::optional<asr::ComputeDiagnostics>& runtime = {})
    {
        const auto requested = runtime ? runtime->requested : selection;
        const auto backendName = [](asr::ComputeBackend backend) {
            switch (backend) {
            case asr::ComputeBackend::automatic: return "Automatic";
            case asr::ComputeBackend::cpu: return "CPU";
            case asr::ComputeBackend::vulkan: return "Vulkan";
            }
            return "Unknown";
        };
        std::string status = std::string("Compute: ") + backendName(requested.backend);
        if (runtime) {
            status += "\nActive: ";
            if (!runtime->activeBackend)
                status += runtime->initializationFailure.empty() ? "Unreported" : "Unavailable";
            else {
                status += backendName(*runtime->activeBackend);
                if (*runtime->activeBackend == asr::ComputeBackend::vulkan)
                    status += " · " + runtime->activeDeviceName;
            }
            if (!runtime->initializationFailure.empty())
                status += "\nReason: " + computeReasonText(runtime->initializationFailure);
            else if (runtime->fallbackUsed)
                status += "\nReason: " + computeReasonText(runtime->fallbackReason);
            return status;
        }

        status += "\nActive: Not initialized";
        if (requested.backend == asr::ComputeBackend::cpu) return status + "\nAvailable: CPU";
        if (!discovery.vulkanCompiled)
            return status + "\nAvailable: CPU\nReason: " + computeReasonText("vulkan_not_compiled");
        if (!discovery.vulkanAvailable)
            return status + "\nAvailable: CPU\nReason: " + computeReasonText(discovery.unavailableReason);
        // Device names and ordinals are already supplied by structured discovery.
        for (const auto& device : discovery.devices) {
            if (device.backend == asr::ComputeBackend::vulkan
                && (!requested.deviceIndex || device.index == *requested.deviceIndex))
                return status + "\nAvailable: Vulkan · " + device.name;
        }
        return status + "\nAvailable: CPU\nReason: " + computeReasonText("requested_device_unavailable");
    }
}
