/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "whisper_compute_runtime.h"
#include <ggml-backend.h>

#include <filesystem>
#include <mutex>

#if TIMMY_VULKAN_COMPILED
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace timmy::asr::detail {
    namespace {
#if TIMMY_VULKAN_COMPILED
        std::filesystem::path executableDirectory()
        {
#if defined(_WIN32)
            std::wstring path(32768, L'\0');
            const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!count || count >= path.size()) return {};
            path.resize(count);
            return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string path(size, '\0');
            if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
            return std::filesystem::path(path.c_str()).parent_path();
#else
            std::error_code error;
            const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::filesystem::path{} : path.parent_path();
#endif
        }
#endif
    }

    ComputeDiscovery discoverWhisperCompute(bool loadVulkan)
    {
        // GGML's process registry is shared, and modules must stay loaded until exit.
        static std::mutex registryMutex;
        std::lock_guard lock(registryMutex);
        ComputeDiscovery result;
        result.vulkanCompiled = TIMMY_VULKAN_COMPILED != 0;
#if TIMMY_VULKAN_COMPILED
        static bool attempted = false;
        static std::string loadFailure;
        if (loadVulkan && !attempted) {
            attempted = true;
            const auto directory = executableDirectory();
            if (directory.empty()) loadFailure = "backend_directory_unavailable";
            else {
#if defined(_WIN32)
                const auto module = directory / "ggml-vulkan.dll";
#else
                const auto module = directory / "libggml-vulkan.so";
#endif
                // Load only the packaged module, never arbitrary backends from the CWD.
                try {
                    if (!ggml_backend_load(module.string().c_str()))
                        loadFailure = "vulkan_module_unavailable";
                } catch (...) { loadFailure = "vulkan_discovery_failed"; }
            }
        }
        result.unavailableReason = loadFailure;
#else
        (void)loadVulkan;
        result.unavailableReason = "vulkan_not_compiled";
#endif
        int gpuOrdinal = 0;
        try {
            for (std::size_t i = 0; i < ggml_backend_dev_count(); ++i) {
                const auto device = ggml_backend_dev_get(i);
                const auto type = ggml_backend_dev_type(device);
                if (type != GGML_BACKEND_DEVICE_TYPE_GPU && type != GGML_BACKEND_DEVICE_TYPE_IGPU) continue;
                const int index = gpuOrdinal++;
                const auto backend = ggml_backend_reg_name(ggml_backend_dev_backend_reg(device));
                if (result.vulkanCompiled && std::string_view(backend) == "Vulkan") {
                    result.devices.push_back({ComputeBackend::vulkan, index,
                        ggml_backend_dev_name(device), ggml_backend_dev_description(device),
                        type == GGML_BACKEND_DEVICE_TYPE_IGPU ? "integrated_gpu" : "gpu"});
                }
            }
        } catch (...) {
            result.devices.clear();
            result.unavailableReason = "vulkan_discovery_failed";
        }
        result.vulkanAvailable = !result.devices.empty();
        if (!result.vulkanAvailable && result.unavailableReason.empty())
            result.unavailableReason = "no_usable_vulkan_device";
        return result;
    }
}
