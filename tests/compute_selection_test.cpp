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
#include "timmy/core/asr_engine_factory.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace timmy::asr;
namespace {
    void require(bool value, const char* message)
    {
        if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(EXIT_FAILURE); }
    }
}

int main(int argc, char** argv)
{
    const ComputeDiscovery available{true, true,
        {{ComputeBackend::vulkan, 0, "Vulkan0", "GPU zero", "gpu"},
         {ComputeBackend::vulkan, 1, "Vulkan1", "GPU one", "integrated_gpu"}}, {}};
    for (auto backend : {ComputeBackend::automatic, ComputeBackend::cpu, ComputeBackend::vulkan}) {
        require(computeBackendFromId(computeBackendId(backend)) == backend, "Stable backend ID round trip");
        for (bool present : {false, true})
        for (bool succeeds : {false, true}) {
            auto discovery = present ? available : ComputeDiscovery{false, false, {}, "vulkan_not_compiled"};
            std::vector<ComputeBackend> attempts;
            bool gpuResourceAlive = false;
            const auto result = initializeCompute({backend, {}}, discovery,
                [&](const ComputeDevice& device) {
                    attempts.push_back(device.backend);
                    if (device.backend == ComputeBackend::vulkan) {
                        gpuResourceAlive = true;
                        if (!succeeds) gpuResourceAlive = false; // Failed candidate cleaned before retry.
                        return succeeds;
                    }
                    require(!gpuResourceAlive, "GPU candidate released before CPU retry");
                    return true;
                });
            const bool gpu = backend != ComputeBackend::cpu && present && succeeds;
            require(result.activeBackend == (gpu ? ComputeBackend::vulkan : ComputeBackend::cpu), "Actual backend");
            require(result.requested.backend == backend, "Requested backend preserved");
            require(result.fallbackUsed == (backend != ComputeBackend::cpu && !gpu), "Fallback flag");
            require(result.fallbackReason.empty() == !result.fallbackUsed, "Fallback reason populated");
            require(attempts.size() == (backend != ComputeBackend::cpu && present && !succeeds ? 2U : 1U), "Attempt count");
            if (backend == ComputeBackend::cpu) require(attempts.front() == ComputeBackend::cpu, "CPU never attempts GPU");
            require(!result.activeDeviceName.empty(), "Human-readable device name");
        }
    }
    const auto explicitDevice = initializeCompute({ComputeBackend::vulkan, 1}, available,
        [](const auto& device) { return device.index == 1; });
    require(explicitDevice.activeDeviceIndex == 1 && explicitDevice.activeDeviceId == "Vulkan1", "Explicit GPU ordinal");
    auto reversed = available;
    std::swap(reversed.devices[0], reversed.devices[1]);
    require(initializeCompute({ComputeBackend::vulkan, 1}, reversed,
        [](const auto& d) { return d.index == 1; }).activeDeviceId == "Vulkan1", "Selection uses ID, not vector position");
    const auto missingDevice = initializeCompute({ComputeBackend::automatic, 7}, available,
        [](const auto& d) { return d.backend == ComputeBackend::cpu; });
    require(missingDevice.fallbackReason == "requested_device_unavailable", "Missing explicit device");
    const auto exception = initializeCompute({}, available, [](const auto& device) {
        if (device.backend == ComputeBackend::vulkan) throw std::runtime_error("driver error");
        return true;
    });
    require(exception.activeBackend == ComputeBackend::cpu && exception.fallbackUsed
        && exception.fallbackReason == "vulkan_initialization_exception", "Exception fallback");
    require(computeStatus(exception).find("using CPU") != std::string::npos, "Fallback visible");
    const auto failedCpu = initializeCompute({}, available, [](const auto&) { return false; });
    require(!failedCpu.activeBackend, "Failed CPU is never reported active");
    require(failedCpu.initializationFailure == "cpu_initialization_failed", "CPU failure diagnostic");
    const auto cpuException = initializeCompute({ComputeBackend::cpu, {}}, available,
        [](const auto&) -> bool { throw std::runtime_error("CPU init failure"); });
    require(!cpuException.activeBackend && !cpuException.fallbackUsed
        && cpuException.initializationFailure == "cpu_initialization_exception", "CPU exception is transactional failure");
    try {
        (void)initializeCompute({ComputeBackend::vulkan, -1}, available, [](const auto&) { return true; });
        require(false, "Negative device index rejected");
    } catch (const std::invalid_argument&) {}
    try { (void)computeBackendFromId("CUDA"); require(false, "Unknown backend rejected"); }
    catch (const std::invalid_argument&) {}
    const auto discovered = timmy::core::availableComputeDevices(timmy::core::AsrEngineId::whisperCpp);
    if (argc == 2 && std::string_view(argv[1]) == "--expect-missing-vulkan-module") {
        require(discovered.vulkanCompiled && !discovered.vulkanAvailable,
            "Vulkan-enabled distribution starts without optional module");
        require(discovered.unavailableReason == "vulkan_module_unavailable", "Missing module reason");
    }
    if (!discovered.vulkanCompiled)
        require(!discovered.vulkanAvailable && discovered.devices.empty(), "CPU-only discovery needs no Vulkan runtime");
    return EXIT_SUCCESS;
}
