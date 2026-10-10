# TIMMY — Project Charter and Architecture Intent

**Working expansion:** *The Imaginary Man, Minding You*  
**Document status:** Draft 0.3
**Date:** 2026-10-07
**Intended repository location:** `docs/PROJECT_CHARTER.md`  
**Primary implementation language:** C++20  
**Primary GUI toolkit:** wxWidgets  
**Initial operating target:** Windows, while preserving portability to Linux and macOS  
**Planned project license:** GPL-3.0-or-later, subject to final confirmation before the repository is made public

---

## 1. Why TIMMY exists

TIMMY is intended to become a small, reliable, offline-first voice command assistant for live-event technical workflows, beginning with lighting-control systems.

The initial practical objective is deliberately simple:

1. capture audio from a user-selected input;
2. recognize a short spoken command;
3. apply deterministic lexical cleanup and parse a structured `CommandIntent`;
4. validate the intent and render target command syntax;
5. emit the accepted command to an external application or console, initially through OSC.

Typical spoken input may include native console syntax, approximate natural-language equivalents, or small bilingual variations such as:

```text
Fixture 1 At Full
Channel 5 At 50
Group 2 At 25
Canal cinco al cincuenta
Fixture uno a tope
Apaga el canal cinco
```

TIMMY is **not** intended to invent a replacement language for lighting consoles. The operator is expected to understand the target console or application. TIMMY exists to provide a faster and more convenient voice-driven input path, with enough normalization and validation to reduce avoidable recognition errors.

The architecture should also allow TIMMY to outgrow lighting-specific use cases. Audio capture, recognition, normalization, validation and command emission must therefore remain sufficiently independent that other target applications or transports can be added later.

---

## 2. What this document is

This document is the architectural contract for the project.

It records:

- the problem TIMMY is intended to solve;
- the architectural boundaries we want to preserve;
- the initial technology choices and the reasons behind them;
- the responsibilities of each subsystem;
- the implementation patterns we want to encourage;
- the patterns and couplings we explicitly want to avoid;
- the assumptions that future Codex or human contributors should treat as intentional unless a later architectural decision supersedes them.

This is **not** a development roadmap, release plan or task checklist. Those documents may change frequently. This document should change only when the project changes direction or an architectural decision is intentionally replaced.

---

## 3. Core architectural principles

### 3.1 Headless by design, GUI from the beginning

TIMMY must be usable without a graphical interface.

The wxWidgets GUI is nevertheless a first-class frontend and should be developed from the first development phase. The important distinction is that the GUI must **consume** TIMMY capabilities rather than **own** them.

The same Core should support:

- the wxWidgets desktop application;
- a CLI/headless executable;
- automated tests;
- benchmark tools;
- future services, IPC frontends, remote controllers or alternative GUIs.

A useful architectural test is:

> If the entire `apps/gui/` tree were removed, audio capture, speech recognition, command processing and command emission should still be implementable and testable.

GUI event handlers must not become the place where core business logic accumulates.

### 3.2 Replaceable modules

The following concerns must have explicit boundaries:

- audio device discovery and capture;
- audio processing and metering;
- speech recognition;
- lexical normalization and semantic parsing;
- semantic validation and target rendering;
- command emission/transport;
- application orchestration and state;
- CLI frontend;
- GUI frontend.

The project should assume that any implementation dependency may eventually be replaced.

In particular:

- the ASR engine must not own the microphone;
- the GUI must not call recognizer internals directly;
- the command filter must not be embedded inside one ASR backend;
- OSC must not be hard-coded into command-processing logic;
- wxWidgets types must not leak into reusable Core interfaces.

### 3.3 Offline first

Normal operation must not require an Internet connection.

Cloud speech recognition or general-purpose AI services may be explored later as optional adapters, but TIMMY's primary live-event workflow must remain functional without network access.

### 3.4 Deterministic command path

The initial execution path should remain inspectable and predictable:

```text
Audio
  -> ASR
  -> Transcript
  -> Lexical normalization
  -> Semantic parser
  -> CommandIntent
  -> Semantic validation
  -> Console/profile renderer
  -> Command text
  -> ICommandEmitter
```

A general-purpose LLM is not part of the required command path.

Natural-language interpretation in the normal path is deterministic and compositional. An optional future local semantic fallback may run only after deterministic parsing fails, produce a `CommandIntent` candidate, and use the same validator and renderer. It must never directly emit commands or bypass validation. No model dependency is required.

### 3.5 Observable behavior

TIMMY should make each significant processing step visible to logs, tests and frontends.

For example:

```text
HEARD:      "enciende canal 5"
NORMALIZED: "enciende canal 5"
PARSED INTENT: SetIntensity(Channel(5), 100%)
VALIDATION: accepted
RENDERED COMMAND: "Channel 5 At 100"
OUTPUT:     selected emitter
STATUS:     sent
```

This observability is part of the product design, not merely a debugging convenience.

---

## 4. High-level architecture

The intended dependency shape is:

```text
+-------------------------------------------------------------+
|                         Frontends                           |
|                                                             |
|       timmy-cli                    timmy-gui                 |
|                                    wxWidgets                 |
+---------------------------+---------------------------------+
                            |
                            v
+-------------------------------------------------------------+
|                         timmy_core                          |
|                                                             |
| Session / lifecycle / state / configuration / orchestration |
+----------+------------------+------------------+-------------+
           |                  |                  |
           v                  v                  v
+----------------+   +----------------+   +--------------------+
|  timmy_audio   |   |   timmy_asr    |   |   timmy_command    |
|                |   |                |   |                    |
| device enum    |   | IAsrBackend    |   | normalization      |
| capture        |   | Whisper        |   | filtering          |
| channel route  |   | future backends|   | validation         |
| resampling     |   |                |   | profiles           |
| metering       |   +----------------+   +----------+---------+
+----------------+                                 |
                                                   v
                                         +--------------------+
                                         | timmy_transport    |
                                         |                    |
                                         | ICommandEmitter    |
                                         | OSC first          |
                                         | future emitters    |
                                         +--------------------+
```

The exact target names may evolve, but ownership boundaries should remain recognizable.

---

## 5. Core and orchestration responsibilities

`timmy_core` should coordinate the application without containing device-specific, GUI-specific or backend-specific implementation details.

Likely responsibilities include:

- application/session lifecycle;
- selected configuration;
- starting and stopping subsystems in the correct order;
- publishing status and diagnostic events;
- connecting audio producers to consumers;
- connecting recognition results to command processing;
- connecting accepted commands to the selected emitter;
- representing recoverable errors and state transitions.

The Core should depend on contracts, not concrete frontend widgets or recognizer-specific APIs.

Configuration should be represented in ordinary serializable C++ models, not stored implicitly in wxWidgets controls.

---

## 6. Audio subsystem

### 6.1 Selected audio library: miniaudio

The initial audio I/O implementation will use **miniaudio**.

Reasons:

- TIMMY needs focused audio capture rather than a large multimedia framework;
- miniaudio is small and portable;
- it provides device enumeration and cross-platform capture support;
- its permissive licensing is suitable for an open-source project;
- it avoids binding the audio Core to a GUI or media framework.

Peratext provides valuable prior experience with SDL2 audio device discovery, channel selection and VU metering, but TIMMY will be implemented from scratch with a cleaner ownership model.

### 6.2 One owner of the physical device

A central design rule is:

> The audio subsystem owns the physical input device. ASR backends do not open microphones themselves.

Conceptually:

```text
                         +-> Metering
Microphone -> AudioEngine+-> ASR consumer
                         +-> Optional recorder
                         +-> Benchmark/debug consumer
```

This avoids duplicated capture paths, keeps the selected device consistent, and makes it possible to feed exactly the same PCM stream into different recognizers.

### 6.3 Audio callback discipline

Real-time capture callbacks must remain lightweight.

They must not perform:

- ASR inference;
- GUI updates;
- disk I/O;
- network operations;
- blocking waits on worker threads.

The likely shape is:

```text
Audio callback
    -> bounded buffer / queue
    -> processing worker
    -> consumers
```

The exact synchronization mechanism should be chosen only after measuring actual needs. The project should avoid both blocking the audio callback and introducing unnecessarily complex lock-free infrastructure before it is justified.

### 6.4 Internal representation

The audio subsystem should expose a stable PCM representation to consumers and own format conversion/resampling where practical.

For the first ASR backend, this will likely include a 16 kHz mono stream, while still retaining native device information for diagnostics.

Metering should retain at least:

```text
peak linear
RMS linear
peak dBFS
RMS dBFS
```

The GUI may display only a subset of these values.

---

## 7. Speech recognition strategy

### 7.1 Initial backend: whisper.cpp

The first ASR implementation will use **whisper.cpp**.

The initial baseline model is:

```text
Whisper Small multilingual Q5_1
```

This is a baseline, not a permanent architectural dependency.

Reasons for selecting whisper.cpp first:

- mature C/C++ integration;
- strong existing project experience from Peratext;
- multilingual recognition suitable for Spanish, English and mixed console terminology;
- broad execution options rather than a mandatory CUDA dependency;
- quantized models suitable for local execution;
- permissive licensing;
- command-oriented examples and grammar-constrained decoding, which are particularly relevant to short technical commands.

### 7.2 Why Small multilingual is the initial model

TIMMY does not need general long-form transcription quality above all else. It needs reliable recognition of short phrases, numbers and domain vocabulary with reasonable latency and resource usage.

Starting with Small multilingual gives more robustness than the very smallest Whisper models without paying the cost of Medium or Large before measurements show that it is necessary.

The actual default may later change as a result of TIMMY-specific benchmarks.

### 7.3 ASR must remain replaceable

A recognizer boundary conceptually similar to the following is required:

```cpp
class IAsrBackend {
public:
    virtual ~IAsrBackend() = default;

    virtual AsrCapabilities capabilities() const = 0;
    virtual StartResult start(const AsrConfiguration& config) = 0;
    virtual void stop() = 0;
    virtual void pushAudio(const AudioFrame& frame) = 0;
};
```

The exact signatures are not frozen by this document.

What **is** frozen is the responsibility boundary.

Backend-specific concepts such as:

- Whisper GBNF grammar;
- Whisper decoding parameters;
- CUDA/Vulkan selection;
- Nemotron word boosting;
- model-specific endpointing;

must remain behind the ASR boundary and must not become assumptions of the command Core or GUI.

### 7.4 Recognition hints

The common system may provide generic hints such as:

```text
preferred language(s)
domain vocabulary
expected command terms
optional grammar intent
suppression patterns
```

Each backend may support only a subset.

Unsupported capabilities should be reported clearly rather than silently changing behavior.

### 7.5 Planned alternative backend

NVIDIA NeMo-Speech.cpp with Nemotron streaming ASR is the first planned alternative to benchmark.

It is attractive because it is a true streaming ASR system with multilingual support and contextual word boosting. It is not selected as the initial backend because whisper.cpp is currently more mature in our environment and its grammar-constrained command workflow is particularly aligned with TIMMY's first use case.

The decision to replace the default backend should be based on TIMMY-specific measurements, not general ASR leaderboard results.

---

## 8. Command language and semantic processing

`timmy_command` owns lexical normalization, compositional parsing, the bounded
`CommandIntent` model, semantic validation and command-text rendering. ASR only
answers “what text was spoken?” The parser answers “what does this supported
command mean?” The renderer selects target syntax; neither parser nor renderer
owns OSC, audio devices, backend grammar APIs or GUI controls.

`CommandIntent` names the existing `SemanticCommand` variant. Operations cover
TIMMY's currently supported intensity and selection/editor capabilities, not a
universal lighting-console model. `TargetSelection` stores compact inclusive
ranges (single items and lists are compositions of ranges), exclusions, or an
explicit `all` flag. All never means an invented numeric identifier interval.

### 8.1 Lexical normalization and compositional grammar

`TranscriptNormalizer` performs ordered whitespace, punctuation, exact Spanish
and English spoken-number normalization and percent cleanup without generating
console commands. Strict cardinal composition currently supports 0 through 999;
malformed or unsupported numeric phrases remain unconverted for downstream
rejection. Already-numeric tokens and selection separators are preserved.
Speech-specific number vocabulary stays in normalization, after ASR and before
command assembly and semantic parsing. The grammar lexer uses ordered
exact token/phrase aliases, including Spanish and English vocabulary. Unknown
words remain unknown so the parser rejects unsupported expressions. No global
substring replacement, fuzzy matching, or per-sentence replacement inventory is
required. Prefix on/off terms combine with the same selection grammar used for
explicit intensity commands.

Representative supported phrases include `enciende canal 1`, `apaga canal 10`,
`fuera todo`, `enciende los canales del 2 al 40`, and
`canales del 2 al 40 al 50`. The default TIMMY language profile explicitly sets
Channel as the omitted target for prefix intensity commands (`apaga 10`). This
profile default is separate from the existing last-explicit-target linguistic
context for relative selection edits. Numeric fragments cannot silently replace
pending selections. Generic parser/assembler instances have no default target.
Prefix intensity supplies the level; extra value clauses are rejected. Ambiguous
singular forms such as `apaga canal 10 al 50` are rejected; plural selection,
`del ... al ...`, or explicit `through`/`hasta` makes a range clear.

### 8.2 Profile configuration

`CommandProfile` is an ordinary C++ value model with distinct domains:

```text
recognition: backend-neutral terms and examples
normalization: lexical formatting configuration
parsing: ordered semantic vocabulary and explicit default target
validation: supported intent constraints and identifier limits
rendering: target command-language vocabulary
```

These values are serializable independently of GUI controls; a persistent file
format and GUI profile editor are deferred. Core adapts recognition vocabulary
to generic `RecognitionHints`. Whisper GBNF remains inside the ASR adapter.
Recognition guidance may be derived from the profile without being authoritative
for parsing or validation.

### 8.3 Rendering and observability

`CommandProcessor` runs transcript cleanup, deterministic parsing, validation and
rendering. Core recognition assembly uses the same command services and profile
configuration. CLI and GUI display the shared intent, validation and rendered
text. `osc send --dry-run --command <text>` exercises processing without audio,
models, sockets or a console. `describe(intent)` remains a semantic diagnostic,
not target command syntax.

Two renderers demonstrate the boundary: configurable TIMMY native text and the
current Hydra II console key syntax. Hydra rendering owns target keys, compact
range/list/exclusion syntax, operation keys and optional pre-clear; it has no OSC
addresses, packet serialization or transport timing. The Hydra OSC adapter only
maps rendered keys to configured addresses and executes key pulses. Semantic
interpretation is not repeated by the production Hydra transport path.

`ICommandEmitter` accepts a rendered command carrying text, optional console key
tokens and the validated intent for typed compatibility adapters. Existing generic
OSC messages retain their typed mapping; native text is not falsely presented as
their wire payload. The legacy semantic emitter/profile entry points remain as
compatibility APIs. Hydra's legacy profile entry point delegates syntax generation
to the same renderer rather than keeping a second grammar in transport.

CLI recognition and GUI recognition/execution use the same selected language
profile. GUI output configuration is frozen for the full listening lifecycle, and
its wx-free profile adapter supplies both recognition and output dispatch. Both
frontends expose the selected renderer's text, including rendering rejection
reasons. Execution results also carry the actual rendered command sent to the
emitter. Broader console text profiles are intentionally deferred.

## 9. Semantic validation

Semantic validation follows parsing and precedes rendering and dispatch. It
checks selection structure, identifiers, operation-specific constraints and
profile policy such as whether `all` is allowed. Dispatcher repeats validation
for directly constructed intents, so future fallback candidates cannot bypass
this boundary. A failed validation produces a diagnostic and no emission.

Transport profiles independently reject unsupported mappings. In particular,
`all` is representable and renderable as TIMMY native text, but the Hydra renderer
and generic OSC adapter reject it because no verified all-target mapping exists. Parser success never
promises that every selected output adapter supports the operation.

Potential future destructive-operation policy belongs here, outside ASR. Optional
local semantic fallback must return candidates to this same path and must not
send OSC or console text directly. Broader grammar coverage, ASR grammar
generation and AI interpretation are not required for normal execution.

---

## 10. Command emission and transport

The initial target transport will be OSC, normally over UDP.

OSC should nevertheless be only one implementation behind a transport-neutral boundary.

Conceptually:

```cpp
class ICommandEmitter {
public:
    virtual ~ICommandEmitter() = default;
    virtual EmissionResult emitRendered(const RenderedCommand& command);
};
```

Initial implementation:

```text
OscCommandEmitter
```

Possible future implementations:

```text
TcpCommandEmitter
MidiCommandEmitter
PerastageIpcEmitter
LocalProcessEmitter
TestCommandEmitter
```

A fake/test emitter should make it possible to verify exact command output in automated tests without opening a real network socket.

The specific OSC serialization library is intentionally **not** selected in this charter. That decision belongs to the transport implementation phase and should consider small permissively licensed C/C++ libraries as well as the actual subset of OSC TIMMY needs.

---

## 11. Frontend responsibilities

### 11.1 CLI / headless frontend

The CLI is not merely a developer debugging tool.

It proves that capabilities really exist outside the GUI and provides a practical interface for:

- device discovery;
- diagnostics;
- headless deployment;
- automation;
- tests and reproducible troubleshooting.

### 11.2 wxWidgets GUI

The GUI should expose the same Core state and actions in a convenient visual form.

It should not duplicate logic that already exists in Core services.

Examples:

- the audio Core discovers devices; the GUI displays them;
- the audio Core computes dBFS; the GUI draws the meter;
- the ASR Core reports a transcript; the GUI renders it;
- the command Core reports normalized text, parsed intent, validation and rendering; the GUI displays those results;
- the emitter reports delivery status; the GUI displays it.

The GUI should be replaceable without rewriting the application engine.

---

## 12. Configuration philosophy

Configuration must be serializable independently of GUI controls.

Likely configuration domains include:

```text
AudioConfiguration
AsrConfiguration
CommandProfile
NormalizationRules
ValidationConfiguration
TransportConfiguration
```

The GUI edits these models. It does not become the storage schema.

Backend-specific settings should remain namespaced so adding another recognizer does not pollute unrelated configuration.

Persistent device selection should prefer stable identifiers when the underlying platform makes them available rather than relying only on enumeration order.

---

## 13. Testing philosophy

TIMMY should be designed so most behavior can be tested without a microphone, GPU or lighting console.

### Unit tests

Expected areas include:

- audio level calculations;
- channel extraction/routing;
- format conversion helpers;
- normalization rules;
- configuration parsing;
- validation;
- profile behavior;
- emitter mocks.

### Integration tests

Deterministic PCM or WAV fixtures should allow pipelines such as:

```text
PCM -> ASR -> normalization -> validation -> fake emitter
```

Real hardware must not be a CI requirement.

### Recognition regression corpus

ASR upgrades should eventually be tested against a fixed corpus containing expected command outcomes.

TIMMY should measure semantic correctness, especially number correctness, rather than relying on generic WER alone.

Critical distinctions include cases such as:

```text
13 / 30
15 / 50
17 / 70
105 / 150
115 / 150
515 / 550
```

along with Spanish, English, mixed-language terminology, silence, ordinary non-command speech, background music and PA spill.

---

## 14. Observability and diagnostics

Subsystems should expose useful structured status rather than forcing frontends to infer it.

Examples:

```text
Audio: device opened / stopped / lost
Audio: native format / internal format
Meter: peak / RMS / dBFS
ASR: backend / model / listening state
ASR: partial / final result
Command: original / normalized / accepted / rejected
Transport: destination / sent / failed
```

Errors should contain enough information to diagnose common live-event failures without attaching a debugger.

---

## 15. Licensing and redistribution intent

The intended project license is **GPL-3.0-or-later**, following the current Perastage philosophy, subject to final confirmation before public release.

Expected dependency profile:

| Dependency | Purpose | Expected license/status |
|---|---|---|
| wxWidgets | GUI | wxWindows Library Licence 3.1, GPL-compatible |
| miniaudio | audio | Public Domain or MIT-0 |
| whisper.cpp | ASR runtime | MIT |
| OpenAI Whisper model weights | initial models | MIT |
| NeMo-Speech.cpp | optional future ASR | Apache-2.0 |
| Nemotron model | optional future model | model-specific terms; review before redistribution |

A `THIRD_PARTY_NOTICES.md` file should exist from the beginning.

Large model files should not normally be committed to Git. Exact redistribution policy for models should be handled separately from source code.

Every binary release should record the versions and licenses of the dependencies and models it actually redistributes.

---

## 16. Prior projects as reference, not architecture inheritance

Two existing projects are especially useful references:

### Peratext

Peratext demonstrates working solutions for:

- audio input selection;
- channel selection;
- VU metering;
- real-time Whisper transcription;
- GPU configuration;
- configurable text replacement.

TIMMY may reuse ideas and well-understood implementation patterns, but it should not reproduce Peratext's tighter coupling between transcription code and GUI objects.

### Perastage

Perastage provides the architectural model we want to follow more closely:

- reusable headless Core capabilities;
- GUI as a frontend;
- CLI and other frontends consuming shared contracts;
- modular adapters around stable application services.

TIMMY should adopt that architectural discipline from the start rather than refactoring toward it later.

---

## 17. Explicit non-goals

The following are **not** initial architectural requirements:

- implementing the full syntax of grandMA3, Eos or any other console;
- creating a universal lighting-console abstraction;
- unrestricted natural-language understanding;
- depending on an LLM for normal commands;
- automatic programming of shows;
- cloud-dependent speech recognition;
- a plugin marketplace;
- user accounts or cloud synchronization;
- a mobile frontend;
- direct Perastage control in the first implementation;
- supporting every possible transport before OSC works.

These ideas may be revisited later, but they must not complicate the initial foundations.

---

## 18. Patterns we explicitly want to avoid

Future contributors should treat the following as architectural warning signs:

### GUI-owned logic

Avoid:

```text
wx button event -> open microphone -> run Whisper -> send UDP
```

Prefer:

```text
wx button event -> Core action
Core -> audio / ASR / command / transport
GUI observes resulting state
```

### ASR-owned audio devices

The recognizer should consume audio supplied by TIMMY, not enumerate and open the selected microphone independently.

### Backend-specific settings leaking everywhere

Whisper-specific or Nemotron-specific configuration must not become fields that unrelated modules are forced to understand.

### Blind text replacement

Normalization must not rely on unordered substring substitutions that can modify unrelated text accidentally.

### Transport logic inside command normalization

Command processing should not care whether the result is sent via OSC, MIDI, TCP or a test emitter.

### Hidden behavior

Silent fallback, silent command rewriting and silent backend capability loss should be avoided. If behavior changes, diagnostics should make it visible.

### Premature generalization

Do not build a universal protocol framework, full semantic lighting engine or elaborate plugin system before a real use case requires it.

---

## 19. Future capabilities this architecture intentionally leaves open

Without committing to implement them now, the design should make the following possible without a fundamental rewrite:

- Nemotron or other local ASR backends;
- cloud ASR backends;
- additional command transports such as MIDI, TCP or IPC;
- Perastage integration;
- multiple lighting-console profiles;
- user-defined normalization profiles;
- push-to-talk, keyboard, pedal or Stream Deck activation;
- VAD-driven activation;
- destructive-command confirmation policies;
- recording and replay of recognition sessions;
- corpus-based ASR regression testing;
- another desktop GUI or service frontend;
- live-event domains beyond lighting.

---

## 20. Architectural change policy

These decisions are not immutable, but changes should be intentional.

When a major decision changes — for example replacing miniaudio, changing the default ASR strategy, or introducing a semantic command model — the reason should be recorded in a short Architecture Decision Record under something similar to:

```text
docs/decisions/
```

A change should normally be justified by at least one of:

- measured performance;
- maintainability;
- portability;
- reliability;
- licensing;
- a demonstrated product requirement.

"A newer library exists" is not, by itself, sufficient reason to destabilize a working boundary.

---

## 21. Architectural compliance checklist

A new feature is architecturally healthy when the answers to these questions remain positive:

1. Can its Core logic be tested without launching wxWidgets?
2. Can the CLI exercise the same underlying capability where that is meaningful?
3. Does audio capture remain owned by the audio subsystem?
4. Is ASR replaceable without rewriting command processing?
5. Is command processing independent of OSC?
6. Are backend-specific details contained within adapters or configuration namespaces?
7. Are observable state and errors available to more than one frontend?
8. Can CI test the logic without requiring the developer's actual microphone or lighting console?
9. Has the feature avoided introducing functionality unrelated to its stated scope?

If several of these answers become "no", the design should be reconsidered before the coupling spreads.

---

## 22. Current decision summary

Unless a later architectural decision intentionally supersedes them, the working decisions are:

```text
Project:            TIMMY
Working meaning:    The Imaginary Man, Minding You
Language:           C++20
Build system:       CMake
Architecture:       modular, headless-first
GUI:                wxWidgets
Audio I/O:          miniaudio
Audio ownership:    one central capture engine
Initial ASR:        whisper.cpp
Initial model:      Whisper Small multilingual Q5_1
ASR strategy:       replaceable backends
First ASR rival:    NeMo-Speech.cpp / Nemotron
Normalization:      lexical cleanup in command layer
Parsing:            deterministic compositional CommandIntent
Validation:         semantic policy before rendering/emission
Rendering:          target syntax separate from semantic meaning
Initial transport:  OSC behind ICommandEmitter
Internet:           optional; not required for normal use
License:            GPL-3.0-or-later, pending final confirmation
```

The most important decision is not that any particular library must remain forever.

The most important decision is that replacing the GUI, recognizer, audio backend, normalization rules or command transport should not require rewriting TIMMY as a whole.
