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
`IOscCommandProfile` to replace only this semantic-to-message mapping; OSC
serialization and UDP ownership remain in `OscCommandEmitter`.
