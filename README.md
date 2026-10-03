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
