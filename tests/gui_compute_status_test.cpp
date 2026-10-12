/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "compute_status.h"

#include <cstdlib>
#include <iostream>

using namespace timmy;
namespace {
    void require(bool value, const char* message)
    {
        if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(EXIT_FAILURE); }
    }
}

int main()
{
    using Backend = asr::ComputeBackend;
    const asr::ComputeDiscovery available{true, true,
        {{Backend::vulkan, 0, "Vulkan0", "NVIDIA GeForce RTX 3070", "gpu"},
         {Backend::vulkan, 1, "Vulkan1", "GPU one", "integrated_gpu"}}, {}};
    const asr::ComputeDiscovery notCompiled{false, false, {}, "vulkan_not_compiled"};
    const auto format = gui::recognitionComputeStatus;
    require(format({}, available, {}).find("Active: Not initialized\nAvailable: Vulkan · NVIDIA GeForce RTX 3070")
        != std::string::npos, "GPU readiness before model initialization");
    require(format({Backend::automatic, 1}, available, {}).find("Available: Vulkan · GPU one")
        != std::string::npos, "Explicit device readiness");
    require(format({Backend::vulkan, 7}, available, {}).find("selected Vulkan device is unavailable")
        != std::string::npos, "Missing explicit device readiness");
    require(format({}, notCompiled, {}) == "Compute: Automatic\nActive: Not initialized\nAvailable: CPU\nReason: Vulkan support is not included in this build",
        "Missing build capability is visible before start");
    require(format({Backend::cpu, {}}, notCompiled, {}) == "Compute: CPU\nActive: Not initialized\nAvailable: CPU",
        "Forced CPU readiness does not imply fallback");
    for (auto requested : {Backend::automatic, Backend::cpu, Backend::vulkan}) {
        const auto active = asr::initializeCompute({requested, 1}, available, [](const auto&) { return true; });
        const auto text = format({}, notCompiled, active);
        require(text.find(requested == Backend::cpu ? "Active: CPU" : "Active: Vulkan · GPU one")
            != std::string::npos, "Runtime diagnostics override discovery preview");
        require(text.find("Reason:") == std::string::npos, "Successful initialization has no fallback reason");
        if (requested == Backend::cpu) continue;
        const auto missing = asr::initializeCompute({requested, {}}, notCompiled, [](const auto&) { return true; });
        require(format({}, available, missing).find("Active: CPU\nReason: Vulkan support is not included in this build")
            != std::string::npos, "Not-compiled runtime fallback");
        const auto failed = asr::initializeCompute({requested, {}}, available,
            [](const auto& device) { return device.backend == Backend::cpu; });
        require(format({}, available, failed).find("Active: CPU\nReason: Vulkan initialization failed; using CPU")
            != std::string::npos, "Initialization failure differs from unavailability");
        const auto exception = asr::initializeCompute({requested, {}}, available,
            [](const auto& device) { if (device.backend == Backend::vulkan) throw 1; return true; });
        require(format({}, available, exception) == format({}, available, failed), "Exception fallback stays concise");
        const auto failedCpu = asr::initializeCompute({requested, {}}, available, [](const auto&) { return false; });
        require(format({}, available, failedCpu).find("Active: Unavailable\nReason: CPU initialization failed")
            != std::string::npos, "Failed CPU is never presented as active");
    }
    for (const auto* reason : {"vulkan_module_unavailable", "no_usable_vulkan_device",
            "vulkan_discovery_failed", "backend_directory_unavailable", "vulkan_unavailable"}) {
        const asr::ComputeDiscovery unavailable{true, false, {}, reason};
        require(format({}, unavailable, {}).find("Available: CPU\nReason: Vulkan is unavailable on this system")
            != std::string::npos, "Compiled but unavailable readiness");
        const auto fallback = asr::initializeCompute({}, unavailable, [](const auto&) { return true; });
        require(format({}, unavailable, fallback).find("Active: CPU\nReason: Vulkan is unavailable on this system")
            != std::string::npos, "Compiled but unavailable runtime fallback");
        require(fallback.fallbackReason == reason, "Technical diagnostics are preserved");
    }
    asr::ComputeDiagnostics unreported;
    require(format({}, available, unreported) == "Compute: Automatic\nActive: Unreported",
        "Unreported backend never claims GPU acceleration");
}
