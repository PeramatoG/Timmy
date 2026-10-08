# Guided live recognition benchmark (ASR-BENCH-010)

In the Recognition column, select the engine, local model and VAD model, language,
and recognition tuning as usual. Press **Open benchmark...** in the compact
Recognition benchmark section. In the dedicated window, select a `.jsonl` result
file (or enter its path), then press **Start benchmark**. The configuration freezes
at Start, rather than when the window opens. Files are appended to, so sessions
can share one file; every line carries a distinct session ID and a UTC timestamp.

The primary view shows the corpus version, result path, frozen engine/model/
language/profile summary, case/category/progress, phrase, operator instructions
and last result. PASS is green, FAIL is red, and active states have a neutral blue
accent. The states guide starting, listening/waiting, operator review, stopping
and finishing. Listening and waiting for speech end/recognition share one state:
the current backend-neutral API does not publish speech-boundary progress events.
There are no automatic timeouts. **Details** expands to show raw/normalized
transcripts, assembly, semantic command, validation/rendering and available timings.

PASS and FAIL counters count recorded attempts, including retries. **No result**
is a subset of those counts, so an expected non-command with no ASR result adds
to both PASS and No result. Starting a new session resets the displayed counters.
At the final case the state is **Finished**; Retry remains available and the main
configuration stays frozen until Stop or Close completes. Closing the benchmark
window waits asynchronously for its recognition resources before hiding it.
Reopening reuses the window and retains the selected path and last session summary.

The main GUI separates engine selection, session controls and engine/VAD settings.
Short tuning properties share rows in wider windows and stack in narrower ones.
Output profile selection is separated from profile settings; transcript details
can be collapsed to keep the main recognition view compact.

The definitive initial `timmy-live-v1` corpus contains 36 standalone cases:
channels/fixtures/groups, 0/50/100 intensity, ranges, lists, exclusions,
select/test/home/editor/release operations, Spanish natural phrases, eight English
cases, and four non-command phrases. Its ten Spanish `numeric-robustness` cases
use 1, 5, 15, 25, 35, 105, 115, 150, 515 and 550, covering short numbers, tens,
compounds, hundreds and high channel/fixture identifiers. The structural pairs
105/150 and 515/550 exercise preservation of multi-part spoken numbers. The six
`english-numeric-confusion` cases use English commands for 13/30, 15/50 and 17/70:
thirteen/thirty, fifteen/fifty and seventeen/seventy.

Speak displayed numeric digits naturally as number words: for example,
**Canal 105 al 50** is spoken as **Canal ciento cinco al cincuenta**; English
**Channel 13 at 50** is spoken as **Channel thirteen at fifty**. Whisper must
still produce numeric tokens for the existing deterministic command parser;
the benchmark does not add number-word parsing or change recognition guidance.
These corrections define the still-unreleased initial corpus. Case IDs, order,
phrases and expected intents are versioned together; changes after release
require a new corpus version.

Speak the displayed phrase once. The first final result is evaluated and flushed
to disk before the GUI displays **PASS** or **FAIL**. The current case stays visible
until **Next** or **Retry**. Additional recognition during review is ignored and
does not silently consume cases. Retry records another attempt for the same case;
Next starts attempt 1 for the next case. At the final case, Next is disabled;
Retry and Stop remain available.

If you have spoken the phrase but no final ASR result arrives, press **No result**.
This explicit operator action records `recognition_outcome: "no_result"` without
fabricating a Whisper transcript. An expected positive command fails; an expected
non-command passes. The attempt then enables Next/Retry as usual. The action is
available only while the current benchmark attempt awaits a result. It shares
the final-result gate, so a late ASR callback cannot record a second attempt.
There is no automatic timeout, and normal Whisper empty-result behavior is
unchanged.

Next and Retry asynchronously stop the previous recognition resources, discard
queued audio/inference, then restart using the same frozen snapshot. The GUI stays
responsive during shutdown. A model may need to reload between attempts. This
avoids attributing a previous utterance or linguistic context to a new attempt.
Model, VAD model, language, tuning, input selection and output configuration stay
locked until **Stop benchmark** completes. Stop and window close join recognition
callbacks before closing the file. Every already completed attempt is flushed,
including results whose GUI event has not yet been processed. Initialization and
logging failures are reported instead of silently continuing.
The session also freezes and records the selected audio device display name,
zero-based channel index and channel display name when available. Every attempt,
including retries and No result attempts, contains this same audio-source snapshot.

Command output is disarmed before recognition starts and cannot be armed during
a benchmark, including through synthetic GUI events. Benchmark observer events
carry no output generation and bypass command submission entirely. Ordinary
Start listening and output arming remain available after stopping the benchmark.
The selected command profile's validation/rendering are reused, without creating
an OSC emitter or requiring console state.

## Evaluation and reusable API

`timmy_core/recognition_benchmark.h` owns the corpus, semantic evaluator, frozen
snapshot, session attempt numbering and JSONL writer. It has no wxWidgets types
or emitter. Recognition still uses `RecognitionPipeline` for normalization,
assembly, parsing, validation and rendering. Only benchmark pipelines isolate
utterances; normal pipelines retain their existing assembly and context behavior.

Positive cases pass only for accepted, renderable commands with the expected
semantic operation, target type/selection and intensity. Equivalent lists, ranges
and exclusions are compared as compact selections, without expanding identifiers.
Raw transcript spelling and renderer text are not the equality metric. Negative
cases pass only when no accepted/renderable semantic command is produced.
An explicit No result outcome uses the same positive/negative expectation rules
without evaluating a transcript or running the command pipeline.

## JSONL schema version 1

Each UTF-8 line contains one self-contained JSON object:

- `schema_version`, `corpus_version`, `session_id`, `timestamp` (UTC ISO 8601);
- `case_id`, `category`, `displayed_phrase`;
- `expected_semantic_result` (the shared command description or null),
  `expected_no_command` (explicit boolean);
- `recognition_outcome` (`final_result` or `no_result`);
- `raw_transcript`, `normalized_transcript`, `assembly_state`, `assembly_text`;
- `actual_semantic_description`, `validation_state`, `validation_diagnostic`,
  `rendered_command`, `rendering_diagnostic`;
- `result` (`PASS`/`FAIL`), `attempt_number` (1-based), `retry_number` (0-based);
- `detected_language` (nullable), `asr_engine_id`, `model_filename`,
  `vad_model_filename`, `selected_language` (null means automatic selection);
- `audio_source.device_display_name`, `audio_source.channel_index` (zero-based),
  `audio_source.channel_display_name` (nullable), all backend-neutral metadata;
- `recognition_settings`: full model/VAD paths, language, engine ID, command
  profile ID, rendering target/pre-clear, all typed tuning values, and recognition
  vocabulary/expected phrases/constraint intent used by the session;
- `metrics.audio_duration_seconds`, `metrics.inference_duration_seconds` (nullable).

For `no_result`, transcript/assembly text, semantic description, rendered command,
detected language and both timings are null. Assembly and validation states are
`not_attempted`, and diagnostics are empty. A real final result retains
`final_result` even if its transcript is empty. Schema version 1 includes these
fields as part of the definitive unreleased initial definition.

Backend-neutral `RecognitionMetrics` propagate from the final ASR result through
Core. Whisper audio duration is the actual inference sample count divided by
16,000, including whatever endpoint/pre-roll audio was passed to inference.
Inference duration measures only `whisper_full()` with a steady clock. These are
not microphone onset latency or model load time. Fake backends leave metrics absent
unless a test supplies them. Final-result disk I/O occurs on the recognition
worker; the explicit No result action writes on the GUI thread. Neither writes
on the realtime audio callback. Each attempt is flushed before it is published.

Deterministic Core and GUI tests use fake ASR without devices, models, GPUs or
consoles. An additional strict JSON decoder test runs when Python 3 is available;
it uses only Python's standard library. WAV capture/replay and parameter sweeps
are outside this feature.
