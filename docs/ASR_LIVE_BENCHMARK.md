# Guided live recognition benchmark (ASR-BENCH-010)

In the Recognition column, select the engine, local model and VAD model, language,
and recognition tuning as usual. Select a `.jsonl` result file (or enter its path),
then press **Start benchmark**. Files are appended to, so sessions can share one
file; every line carries a distinct session ID and a UTC timestamp.

The fixed `timmy-live-v1` corpus contains 32 standalone cases: numeric confusion
pairs, channels/fixtures/groups, 0/50/100 intensity, ranges, lists, exclusions,
select/test/home/editor/release operations, Spanish natural phrases, four English
cases, and four non-command phrases. Numerals in the displayed phrases are spoken
as numbers. The existing command pipeline expects numeric tokens in ASR text;
the benchmark does not add number-word parsing or change recognition guidance.
Case IDs, order, phrases, and expected intents are versioned together. Changes to
that corpus require a new corpus version.

Speak the displayed phrase once. The first final result is evaluated and flushed
to disk before the GUI displays **PASS** or **FAIL**. The current case stays visible
until **Next** or **Retry**. Additional recognition during review is ignored and
does not silently consume cases. Retry records another attempt for the same case;
Next starts attempt 1 for the next case. At the final case, Next is disabled;
Retry and Stop remain available.

Next and Retry asynchronously stop the previous recognition resources, discard
queued audio/inference, then restart using the same frozen snapshot. The GUI stays
responsive during shutdown. A model may need to reload between attempts. This
avoids attributing a previous utterance or linguistic context to a new attempt.
Model, VAD model, language, tuning, input selection and output configuration stay
locked until **Stop benchmark** completes. Stop and window close join recognition
callbacks before closing the file. Every already completed attempt is flushed,
including results whose GUI event has not yet been processed. Initialization and
logging failures are reported instead of silently continuing.

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

## JSONL schema version 1

Each UTF-8 line contains one self-contained JSON object:

- `schema_version`, `corpus_version`, `session_id`, `timestamp` (UTC ISO 8601);
- `case_id`, `category`, `displayed_phrase`;
- `expected_semantic_result` (the shared command description or null),
  `expected_no_command` (explicit boolean);
- `raw_transcript`, `normalized_transcript`, `assembly_state`, `assembly_text`;
- `actual_semantic_description`, `validation_state`, `validation_diagnostic`,
  `rendered_command`, `rendering_diagnostic`;
- `result` (`PASS`/`FAIL`), `attempt_number` (1-based), `retry_number` (0-based);
- `detected_language` (nullable), `asr_engine_id`, `model_filename`,
  `vad_model_filename`, `selected_language` (null means automatic selection);
- `recognition_settings`: full model/VAD paths, language, engine ID, command
  profile ID, rendering target/pre-clear, all typed tuning values, and recognition
  vocabulary/expected phrases/constraint intent used by the session;
- `metrics.audio_duration_seconds`, `metrics.inference_duration_seconds` (nullable).

Backend-neutral `RecognitionMetrics` propagate from the final ASR result through
Core. Whisper audio duration is the actual inference sample count divided by
16,000, including whatever endpoint/pre-roll audio was passed to inference.
Inference duration measures only `whisper_full()` with a steady clock. These are
not microphone onset latency or model load time. Fake backends leave metrics absent
unless a test supplies them. Disk I/O occurs on the recognition worker, never on
the realtime audio callback; each attempt is flushed before it is published.

Deterministic Core and GUI tests use fake ASR without devices, models, GPUs or
consoles. An additional strict JSON decoder test runs when Python 3 is available;
it uses only Python's standard library. WAV capture/replay and parameter sweeps
are outside this feature.
