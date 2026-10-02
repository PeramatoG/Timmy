/*
 * This file is part of Timmy.
 * Copyright (C) 2026 Luisma Peramato
 *
 * Timmy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "timmy/audio/audio_capture_state.h"
#include "timmy/audio/audio_device_enumerator.h"
#include "timmy/audio/audio_engine.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

    // Reports a failed contract expectation and returns a value suitable for main().
    int fail(std::string_view message)
    {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }

} // namespace

// Verifies audio identity, lifecycle, diagnostic, and result contracts in memory.
int main()
{
    using namespace timmy::audio;

    const AudioDeviceId firstId{ "test-backend", "opaque-device-a" };
    const AudioDeviceId matchingId{ "test-backend", "opaque-device-a" };
    const AudioDeviceId otherId{ "test-backend", "opaque-device-b" };

    if (firstId.empty()
        || firstId != matchingId
        || firstId == otherId
        || !AudioDeviceId{}.empty())
    {
        return fail("device identities did not retain their opaque values");
    }

    AudioDeviceInfo device;
    device.runtimeIndex = 7;
    device.name = "Test input";

    if (device.id.has_value())
    {
        return fail("an unmapped device unexpectedly acquired an identity");
    }

    device.id = firstId;

    if (device.id != matchingId || device.runtimeIndex != 7)
    {
        return fail("device identity changed its ephemeral runtime index");
    }

    constexpr std::array states{
        AudioCaptureState::stopped,
        AudioCaptureState::starting,
        AudioCaptureState::running,
        AudioCaptureState::stopping,
        AudioCaptureState::failed
    };

    if (states.size() != 5)
    {
        return fail("the capture lifecycle contract is incomplete");
    }

    const AudioDiagnostic diagnostic{
        AudioDiagnosticDomain::device,
        AudioDiagnosticCode::deviceUnavailable,
        "Test device is unavailable.",
        std::int64_t{ -42 },
        true
    };

    const AudioStartResult failedStart{ {}, diagnostic };
    const AudioDeviceEnumerationResult failedEnumeration{ {}, diagnostic };

    if (failedStart.ok()
        || failedEnumeration.ok()
        || failedStart.diagnostic->domain != AudioDiagnosticDomain::device
        || failedStart.diagnostic->code != AudioDiagnosticCode::deviceUnavailable
        || failedStart.diagnostic->backendCode != -42
        || !failedStart.diagnostic->recoverable)
    {
        return fail("structured diagnostics were not preserved by result contracts");
    }

    if (!AudioStartResult{}.ok()
        || !AudioDeviceEnumerationResult{}.ok())
    {
        return fail("empty result contracts did not report success");
    }

    std::cout << "PASS: deterministic audio contracts\n";
    return 0;
}
