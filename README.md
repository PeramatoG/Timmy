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
additive list terms use OK, and excluded terms use EXCP. `--pre-clear` optionally
prefixes two C pulses; it is disabled by default and never prefixes relative
selection edits. Hold and gap default to 30 ms each, including the final gap.
Adapt these to the console configuration.

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
| `Añade canal 4` / `Add channel 4` | `OK CH 4 OK` |
| `Quita canal 4` / `Remove channel 4` | `EXCP CH 4 OK` |
| `Limpia selección` / `Clear selection` | `C C` |
| `Libera todo` / `Release editor` | `REL REL` |

Selection navigation also accepts fixture and group target types. HOME applies
only to whole fixtures. Bare `siguiente` advances Hydra's TEST workflow; TIMMY
does not calculate a new target. TEST, NEXT and PREV addresses default to
`/keyTEST`, `/keyNEXT` and `/keyPREV`, and remain configurable in the key catalog.
AdvanceTest and ClearEditor always emit exactly one key pulse, including when
pre-clear is enabled. Selection clearing and editor release retain their own
plans without pre-clear. The [Hydra II v7.04 manual](https://lt-light.com/en/download/system-ii-manuals/?filename=ManualSysII704_EN.pdf&ind=17260914681211&wpdmdl=5453)
defines C C, OK and EXCP in section 5.1.1 (page 5-2), and REL REL fading release
versus RST abrupt editor clear in section 5.2.12 (page 5-16). RST is emitted only
for the explicit editor-clear operation; bare `reset`, `borra`, `limpia` and
`clear` are rejected. `Cler all` is a declared ASR alias for `Clear all`;
other spelling errors are not corrected by fuzzy matching.

Selection verbs also accept `dame`, `coge`, `toma`, `marca` and `select`, with
controlled articles: `Dame los canales del 1 al 10 y me quitas el 6` selects
the range excluding 6. `Canales 1 al 10 menos el 6` and `Channels 1 through 10
except 6` express the same selection. `Dame canales 1 al 10 y añade el 15`
and `Channels 1 through 10 plus 15` include the additional term. Known add/remove
aliases and supported accented/unaccented Spanish forms are canonicalized by a
shared command lexicon; transcript normalization remains formatting only.

After a successful command explicitly naming a channel, fixture or group,
follow-ups such as `Súmale el 4`, `Añade el 4`, `Add 4`, `Quita el 10` and
`Remove 10` use that last target type. `Suma del 20 al 25` and `Quita del 5 al 8`
retain compact ranges. TIMMY remembers only the target type, never the console's
selected IDs. Without that context, elided edits are rejected. Stop/restart,
discarding pending assembly and GUI disarm/re-arm reset context; changing output
profiles requires stopping recognition and therefore also starts fresh.

`Canal 5` and `Canales 1 a 10` remain pending until a level or action completes
them. A bare selection with an explicit add/exclusion clause, such as
`Canales 1 al 10 excepto 6`, completes `SelectSelection`; start with `pon` or
`set` to keep a multi-fragment intensity command pending until its level arrives.
`Canal 10` followed by `test` completes a selection test rather than advancing
an earlier test. Pending assembly holds at most 256 bytes and selections contain
at most 32 compact terms; invalid fragments never execute. Recognition hints
use the command lexicon's vocabulary and are covered by deterministic tests
against this same normalize -> assemble -> semantic parse pipeline, with an
explicit preceding selection for relative examples.
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

The GUI shows Audio, Recognition and Output simultaneously in three native,
resizable columns. Each column scrolls when its contents exceed the available space.
Native dark appearance is requested at startup where wxWidgets supports it.
Command output is disabled by default, so listening still shows raw transcript,
normalized transcript, assembly state/text and semantic command without sending.

For Hydra II, select the audio device/channel and local recognition resources,
then use the Output column, select **LT Hydra II**, enter the console
IP or hostname and use port **8000**. Hold and gap inherit the transport profile
defaults (currently 30 ms each); pre-clear is off. **Generic OSC** uses the existing
generic mapping and disables the Hydra controls. Hydra hold and gap each accept
**0–250 ms**; the finite limit prevents accidental multi-minute key sequences.
No output settings are persisted. Configuration can be edited while stopped,
including while **Enable OSC command output** is unchecked.

Recognition tuning is separated into **Engine settings** (currently Whisper)
and **VAD / Endpoint**.
Thread count **0** keeps automatic selection clamped to 1–4; all tuning starts with
the existing command-mode defaults. Endpoint durations are edited in milliseconds
and converted to 16 kHz samples inside the backend. **Reset to defaults** restores
only recognition tuning, leaving model paths, language and output settings alone.
Tuning and reset are available only while stopped. Core exposes typed descriptors
and validates values; the selected backend translates a copied snapshot to its
private configuration. CLI and GUI construct recognition through the same Core
factory, including the same defaults. No tuning is persisted.

Press **Start listening** in Recognition and speak an already supported command,
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

### Semantic command diagnostics

Inspect command processing without microphone, model, socket or console:

```sh
./build/timmy-cli osc send --dry-run --command "enciende los canales del 2 al 40"
./build/timmy-cli osc send --dry-run --command "canales del 2 al 40 al 50"
./build/timmy-cli osc send --dry-run --command "fuera todo"
./build/timmy-cli osc send --dry-run --profile hydra2 --command "enciende canal 1"
```

The shared command services report heard text, lexical normalization, parsed
intent, semantic validation and rendered native text or selected Hydra console
keys. Live recognition
exposes these stages in CLI diagnostics and the GUI's semantic/status display.
`apaga 10` uses the language profile's explicit Channel default. Generic parser
instances have no omitted-target default.

Rendering is separate from OSC wire mapping. Hydra key syntax is generated by
the command renderer and consumed by the OSC key-pulse adapter. Generic OSC
keeps its typed compatibility mapping and supports only single-target intensity.
All-target intensity can be rendered as native text; Hydra rendering and generic
OSC mapping reject it with diagnostics. See
[ADR 0003](docs/decisions/0003-semantic-command-processing-stages.md) for this
compatibility boundary and the future validated semantic fallback extension point.

### Guided live recognition benchmark (ASR-BENCH-011)

Choose **Open benchmark...** under Recognition benchmark to open the dedicated
operator window for the 36-case `timmy-live-v1` corpus. Select a JSONL result file,
press **Start benchmark**, wait for **Ready**, press **Start attempt**, wait for
**SPEAK NOW**, speak the prominently displayed phrase, then choose
**Next** or **Retry** after reviewing PASS/FAIL. State instructions, attempt counts
and a frozen configuration summary guide the session; technical transcripts and
timings are available under **Details**. Use **No result** when no final ASR result
arrives. A five-second speech-start timeout records no speech; empty successful
inference is recorded separately. Next/Retry keep the model loaded while resetting
the attempt. Use **Stop benchmark** or **Close** to release the frozen configuration.
OSC stays disarmed throughout the session. Results compare semantic commands and
include the explicit recognition outcome, frozen audio source, tuning, language
and optional inference/audio timings. See [the benchmark guide](docs/ASR_LIVE_BENCHMARK.md)
for lifecycle details and JSONL schema version 2.
