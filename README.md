# Timmy
Offline-first voice command assistant for lighting consoles and OSC-controlled systems.

## Headless build and tests

The reusable audio module, CLI, and deterministic tests can be built without
wxWidgets, a display server, or an audio input device:

```sh
cmake -S . -B build -DTIMMY_BUILD_GUI=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite feeds in-memory sample data through TIMMY's public audio APIs;
it does not open a microphone.

## Windows development setup

Windows GUI builds require Visual Studio with the MSVC toolchain, CMake,
Ninja, and an external vcpkg checkout. Set `VCPKG_ROOT` to the checkout before
configuring Timmy; for example, in PowerShell:

```powershell
$env:VCPKG_ROOT = "<path-to-vcpkg>"
cmake --preset windows-debug
cmake --build out/build/windows-debug
```

The Windows presets use vcpkg manifest mode and install their dependencies in
Timmy's project-local `vcpkg_installed` directory. wxWidgets is declared by
Timmy's `vcpkg.json`, so developers should not download or build wxWidgets
manually. vcpkg's application-local deployment copies the matching Debug or
Release runtime DLLs beside `timmy-gui.exe` as part of the build.

whisper.cpp remains managed separately by the project through CMake
`FetchContent`. Local Whisper and Silero model files are external runtime
resources; they are not vcpkg dependencies.

## Command-oriented speech recognition

With local Whisper and Silero VAD weights, the CLI closes each short speech
segment after the upstream default silence interval and transcribes it without
waiting for the fallback five-second window:

```sh
timmy-cli asr listen \
  --model /path/to/whisper-model.bin \
  --vad-model /path/to/ggml-silero-v6.2.0.bin \
  --language en
```

The `--vad-model` option is optional. Without it, the Whisper backend retains
the bounded five-second accumulation behavior.

## Explicit OSC test sends

The CLI can parse and send one explicitly supplied semantic command to a UDP
destination. This path does not send commands recognized from live audio:

```sh
timmy-cli osc send --host 127.0.0.1 --port 9000 \
  --command "Channel 5 At 50"
```

The initial generic profile is intended for tests and integrations rather than
a specific lighting console. It maps channel intensity to
`/timmy/channel/intensity` with integer channel and percentage arguments,
fixture intensity to `/timmy/fixture/<id>/intensity`, and group intensity to
`/timmy/group/intensity/<id>`. A console integration can implement
`IOscCommandProfile` to replace only this semantic-to-plan mapping; OSC
serialization and UDP ownership remain in `OscCommandEmitter`.

### LT Hydra II / WOLE remote keys

```sh
timmy-cli osc send --profile hydra2 --host 127.0.0.1 --port 8000 \
  --command "Channel 5 At 50" --key-hold-ms 30 --key-gap-ms 30
```

Hydra II uses ordered remote-key pulses (integer `1` then `0` at the same
address). `SetIntensity` selects CH, FXT or GRP, enters the target ID, then `@`
and a two-digit percentage; 100/Full uses a second `@`. No reset or other
catalog-only keys are emitted. `--pre-clear` optionally prefixes two C pulses;
it is disabled by default. Hold and gap default to 30 ms each, including the
final gap. Adapt these to the console configuration.

`Hydra2OscOptions::keyAddresses` exposes the full configurable key catalog;
applications can override individual OSC addresses without changing command
semantics. `IOscCommandProfile` now returns an ordered `OscExecutionPlan` of
messages or complete key pulses. Generic remains the default and still sends
exactly one unchanged message, without waiting.

Reuse one `OscCommandEmitter` for a destination: concurrent calls on that
instance execute whole plans serially, including failure cleanup. Independent
emitters or external console operators are not coordinated. Waiting is injected
in deterministic tests. The emitter validates every message before the first
send, stops on failure, attempts to release the active key, and propagates the
original error. Presses are never retried. UDP success means local submission,
not console acknowledgement; best-effort release cannot guarantee delivery.
Live recognition remains disconnected from emission.

### Live voice execution (E2E-010)

`asr listen` remains transcript-only unless `--profile` is explicitly supplied.
To send the existing SetIntensity commands to Hydra II:

```sh
timmy-cli asr listen --model <whisper-model> --vad-model <silero-model> \
  --language es --profile hydra2 --host <HYDRA_IP> --port 8000
```

`--key-hold-ms`, `--key-gap-ms` and `--pre-clear` use the same Hydra profile
settings as `osc send`. Output options require a profile, host and port; Hydra
options require `hydra2`. `generic` is also supported.

RAW, NORMALIZED, ASSEMBLY and SEMANTIC output remain visible. A single execution
worker reports DISPATCH queued/succeeded/failed independently of transcript
printing. Its FIFO holds up to 16 waiting commands; overflow is reported rather
than blocking recognition. Repeated spoken commands are separate events. On
Ctrl+C, capture and recognition stop first, waiting commands are discarded, and
the current emitter plan finishes before the worker is joined.
