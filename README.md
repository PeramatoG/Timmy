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
address). `SetIntensity` selects CH, FXT or GRP, enters the compact selection, then
`@` and a two-digit percentage; 100/Full uses a second `@`. Ranges use THRU,
additive list terms use OK, and excluded terms use EXCP. `--pre-clear` optionally prefixes two C pulses;
it is disabled by default. Hold and gap default to 30 ms each, including the
final gap. Adapt these to the console configuration.

`Hydra2OscOptions::keyAddresses` exposes the full configurable key catalog;
applications can override individual OSC addresses without changing command
semantics. `IOscCommandProfile` now returns an ordered `OscExecutionPlan` of
messages or complete key pulses. Generic remains the default and still sends
exactly one unchanged message, without waiting.

### Practical selection, test and editor commands (CMD-040)

CLI and GUI share the same deterministic command language through Core. Channel,
fixture and group selections contain positive IDs, inclusive ranges, lists and
exclusions. Ranges stay compact; they are never expanded into individual commands.
For example, `Canales 1, 3 y 5 al 70` and `Channels 1 through 10 except 5 at 50`
set the level of the whole selection. `full` and `tope` mean 100 percent.

Hydra II additionally supports these operations:

| Spoken command | Hydra key sequence |
| --- | --- |
| `Selecciona canales 1 a 10` | `CH 1 THRU 1 0 OK` |
| `Libera canal 5` / `Release channel 5` | `CH 5 REL` |
| `Test canal 21` / `Prueba canal 21` | `CH 2 1 TEST` |
| `Test` / `Siguiente` / `Next test` | `TEST` |
| `Siguiente canal` / `Next channel` | `CH NEXT` |
| `Canal anterior` / `Previous channel` | `CH PREV` |
| `Fixtures 1 a 6 a home` | `FXT 1 THRU 6 HOME` |
| `Limpia editor` / `Borra todo el editor` / `Clear editor` | `RST` |

Selection navigation also accepts fixture and group target types. HOME applies
only to whole fixtures. Bare `siguiente` advances Hydra's TEST workflow; TIMMY
does not calculate a new target. TEST, NEXT and PREV addresses default to
`/keyTEST`, `/keyNEXT` and `/keyPREV`, and remain configurable in the key catalog.
AdvanceTest and ClearEditor always emit exactly one key pulse, including when
pre-clear is enabled. RST is emitted only for the explicit editor operation;
bare `reset`, `borra` and `clear` are rejected.

`Canal 5` and `Canales 1 a 10` remain pending until a level or action completes
them. `Canal 10` followed by `test` completes a selection test rather than advancing
an earlier test. Pending assembly holds at most 256 bytes and selections contain
at most 32 compact terms; invalid fragments never execute. Recognition hints
are covered by deterministic tests against this same Core pipeline.
The existing singular `Canal 5 a 50` intensity alias remains supported; use
plural `Canales 1 a 10` or explicit `Canal 1 thru 10` for a pending range.

Generic OSC preserves its existing single-target intensity messages. Complex
selections and the new operations return an unsupported diagnostic because the
generic profile has no defined representation for them.

Reuse one `OscCommandEmitter` for a destination: concurrent calls on that
instance execute whole plans serially, including failure cleanup. Independent
emitters or external console operators are not coordinated. Waiting is injected
in deterministic tests. The emitter validates every message before the first
send, stops on failure, attempts to release the active key, and propagates the
original error. Presses are never retried. UDP success means local submission,
not console acknowledgement; best-effort release cannot guarantee delivery.

### Live voice execution (E2E-010)

`asr listen` remains transcript-only unless `--profile` is explicitly supplied.
To send supported semantic commands to Hydra II:

```sh
timmy-cli asr listen --model <whisper-model> --vad-model <silero-model> \
  --language es --profile hydra2 --host <HYDRA_IP> --port 8000
```

`--key-hold-ms`, `--key-gap-ms` and `--pre-clear` use the same Hydra profile
settings as `osc send`. Output options require a profile, host and port; Hydra
options require `hydra2`. `generic` is also supported.

RAW, NORMALIZED, ASSEMBLY and SEMANTIC output remain visible. A single execution
worker reports DISPATCH queued/succeeded/failed independently of transcript
printing. Its FIFO holds up to four waiting commands behind the one in flight in
both CLI and GUI; overflow rejects the new command with a capacity diagnostic
rather than blocking recognition or replaying it later. Repeated spoken commands are separate events. On
Ctrl+C, capture and recognition stop first, waiting commands are discarded, and
the current emitter plan finishes before the worker is joined.

### GUI recognition lifecycle and command output (GUI-030)

The GUI retains its Audio and Recognition pages and adds an Output page.
Command output is disabled by default, so listening still shows raw transcript,
normalized transcript, assembly state/text and semantic command without sending.

For Hydra II, select the audio device/channel and local recognition resources,
then open Output, select **LT Hydra II**, enter the console
IP or hostname and use port **8000**. Hold and gap inherit the transport profile
defaults (currently 30 ms each); pre-clear is off. **Generic OSC** uses the existing
generic mapping and disables the Hydra controls. Hydra hold and gap each accept
**0–250 ms**; the finite limit prevents accidental multi-minute key sequences.
No output settings are persisted. Configuration can be edited while stopped,
including while **Enable OSC command output** is unchecked.

Press **Start listening** on Recognition and speak an already supported command,
for example `Channel 5 At 50`. While listening, audio device/channel, recognition
resources and output configuration are frozen. **Enable OSC command output**
remains available: arm it to validate the frozen settings and emit subsequent
semantic commands without restarting Whisper; disarm it to immediately reject
new output and discard queued commands. An already executing key sequence finishes
safely. Invalid arming leaves recognition running and output disarmed with its
diagnostic. Commands produced while disarmed remain visible and are never replayed
by a later arm. Output shows **OSC output: Armed/Disarmed** separately from
**Execution: Queued/Succeeded/Failed**, including failure diagnostics. Succeeded
means local UDP delivery, not console acknowledgement.

**Stop listening** immediately disarms output and enters **Stopping...**. Whisper
cooperatively aborts inference using its pinned abort callback. Recognition and
output workers are joined on an owned shutdown thread; Start and configuration
controls remain disabled until it completes. Closing the window defers destruction
until recognition, output and audio cleanup finish, preserving the callback target.
An in-flight command may extend cleanup until its final key release, while the wx
event loop continues running. No workers are detached.

Deterministic tests use fake emitters/backends and a simulated inference callback;
they require no microphone, model, network service or console. Real Whisper abort
latency and Hydra II execution still require this manual acceptance check:

1. Start listening with OSC disarmed; say `Canal cinco al cincuenta` and verify the
   semantic command is visible while Hydra does nothing.
2. Arm output during listening and repeat; verify Hydra executes the command.
3. Disarm during listening and repeat; verify recognition remains visible without
   output. Disarming during a pulse must let the active sequence finish.
4. Stop during inference, verify the window remains responsive through Stopping,
   then Start again and repeat. Close during recognition and during output to
   verify safe deferred cleanup.
