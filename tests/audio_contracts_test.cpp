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
#include "miniaudio_device_identity.h"

#include <array>
#include <cstring>
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
    const AudioDeviceId otherBackendId{ "other-backend", "opaque-device-a" };

    if (firstId.empty()
        || firstId != matchingId
        || firstId == otherId
        || firstId == otherBackendId
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

    ma_device_id alsaId{};
    std::strcpy(alsaId.alsa, "hw:Device,7");

    const auto canonicalAlsaId =
        detail::makeAudioDeviceId(ma_backend_alsa, alsaId);

    ma_device_id pulseId{};
    std::strcpy(pulseId.pulse, "source.with opaque chars");

    const auto canonicalPulseId =
        detail::makeAudioDeviceId(ma_backend_pulseaudio, pulseId);

    ma_device_id coreAudioId{};
    std::strcpy(coreAudioId.coreaudio, "AppleUSBAudioEngine:opaque");

    const auto canonicalCoreAudioId =
        detail::makeAudioDeviceId(ma_backend_coreaudio, coreAudioId);

    ma_device_id wasapiId{};
    wasapiId.wasapi[0] = static_cast<ma_wchar_win32>('A');
    wasapiId.wasapi[1] = static_cast<ma_wchar_win32>('b');

    const auto canonicalWasapiId =
        detail::makeAudioDeviceId(ma_backend_wasapi, wasapiId);

    ma_device_id directSoundId{};
    directSoundId.dsound[0] = 0x01;
    directSoundId.dsound[15] = 0xFF;

    const auto canonicalDirectSoundId =
        detail::makeAudioDeviceId(ma_backend_dsound, directSoundId);

    ma_device_id winmmId{};
    winmmId.winmm = 0x12ABU;

    const auto canonicalWinmmId =
        detail::makeAudioDeviceId(ma_backend_winmm, winmmId);

    if (canonicalAlsaId != AudioDeviceId{ "alsa", "hw:Device,7" }
        || canonicalPulseId
            != AudioDeviceId{ "pulseaudio", "source.with opaque chars" }
        || canonicalCoreAudioId
            != AudioDeviceId{ "coreaudio", "AppleUSBAudioEngine:opaque" }
        || canonicalWasapiId != AudioDeviceId{ "wasapi", "00410062" }
        || canonicalDirectSoundId
            != AudioDeviceId{
                "directsound",
                "010000000000000000000000000000ff" }
        || canonicalWinmmId != AudioDeviceId{ "winmm", "000012ab" })
    {
        return fail("native device IDs were not canonically encoded");
    }

    std::array<ma_device_info, 2> nativeDevices{};
    std::strcpy(nativeDevices[0].id.alsa, "first");
    std::strcpy(nativeDevices[1].id.alsa, "second");

    const auto* matchedNativeId =
        detail::findCaptureDeviceId(
            ma_backend_alsa,
            nativeDevices.data(),
            static_cast<ma_uint32>(nativeDevices.size()),
            AudioDeviceId{ "alsa", "second" });

    if (matchedNativeId != &nativeDevices[1].id
        || detail::findCaptureDeviceId(
            ma_backend_alsa,
            nativeDevices.data(),
            static_cast<ma_uint32>(nativeDevices.size()),
            AudioDeviceId{ "pulseaudio", "second" }) != nullptr
        || detail::findCaptureDeviceId(
            ma_backend_alsa,
            nativeDevices.data(),
            static_cast<ma_uint32>(nativeDevices.size()),
            AudioDeviceId{ "alsa", "missing" }) != nullptr)
    {
        return fail("device selection did not require an exact identity match");
    }

    AudioCaptureConfig ambiguousConfig;
    ambiguousConfig.deviceId = AudioDeviceId{ "alsa", "first" };
    ambiguousConfig.runtimeDeviceIndex = 0;

    AudioEngine engine;
    const auto ambiguousStart = engine.start(ambiguousConfig);

    if (ambiguousStart.ok()
        || ambiguousStart.diagnostic->code
            != AudioDiagnosticCode::invalidConfiguration)
    {
        return fail("ambiguous device selectors were not rejected");
    }

    std::cout << "PASS: deterministic audio contracts\n";
    return 0;
}
