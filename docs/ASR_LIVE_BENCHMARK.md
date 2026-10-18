# Guided live recognition benchmark (ASR-BENCH-011)

In the Recognition column, select the engine, local model and VAD model, language,
and recognition tuning as usual. Press **Open benchmark...** in the compact
Recognition benchmark section. In the dedicated window, select a `.jsonl` result
file (or enter its path). Optionally enable **Save ASR input WAVs** (off by default),
then press **Start benchmark**. The configuration freezes
at Start, rather than when the window opens. Files are appended to, so sessions
can share one file; every line carries a distinct session ID and a UTC timestamp.

The primary view shows the frozen configuration, case/category/progress, phrase,
operator instructions and last result. PASS stays green and FAIL red. Each attempt
starts in **Preparing attempt**, then **Ready** only when the backend reset has
completed. Press **Start attempt**, wait through the one-second **Get ready**
countdown, and speak only at **SPEAK NOW**. Recognition accepts audio/results only
while the attempt is armed. The UI follows optional backend-neutral progress:
**Speech detected**, **Waiting for end of speech**, **Processing**, then operator
review. Unsupported stages may be skipped by future backends.

If no speech-start event arrives within five seconds of SPEAK NOW, the attempt
finishes as **no_speech**. This is a speech-start deadline, not an inference timeout:
after speech starts, existing endpoint/maximum-utterance rules apply. **No result**
remains available during an armed attempt for explicit operator intervention.
**Details** shows transcripts, semantic processing, armed input duration/levels,
speech/endpoint/inference timings and available queue-drop counters.

PASS and FAIL count all recorded attempts, including retries; No result counts
all three absence outcomes as a subset. At the final case Next is disabled and
Retry remains available. The operator reviews and advances each attempt manually.
Stop and Close release the session asynchronously. Reopening retains the path and
last summary; starting a new session resets its counters.

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
**Channel 13 at 50** is spoken as **Channel thirteen at fifty**. Whisper may
produce number words or digits. Shared deterministic transcript normalization
converts supported Spanish/English cardinal phrases to digits before command
assembly and semantic parsing; raw and normalized transcripts remain observable.
These corrections define the still-unreleased initial corpus. Case IDs, order,
phrases and expected intents are versioned together; changes after release
require a new corpus version.

Speak the displayed phrase once. The first final result is evaluated and flushed
to disk before the GUI displays **PASS** or **FAIL**. The current case stays visible
until **Next** or **Retry**. Additional recognition during review is ignored and
does not silently consume cases. Retry records another attempt for the same case;
Next starts attempt 1 for the next case. At the final case, Next is disabled;
Retry and Stop remain available.

The four recorded outcomes are `final_result`, `no_speech`, `empty_asr_result`
and `operator_no_result`. Empty successful Whisper inference is now observable;
ordinary recognition still ignores empty text semantically. A positive command
fails for every absence outcome. Expected non-command cases pass for absence,
including no speech: they measure false positives, not whether speech was heard.
The JSONL diagnostics retain this distinction for subsequent analysis.

The ASR backend, Whisper model and Silero context load once per session. Next and
Retry prepare a fresh attempt while keeping those resources loaded. Reset gates
upstream audio, clears resampler/command assembly/context, cancels and joins the
old ASR workers, clears pending PCM/VAD/inference queues and endpoint state,
resets Silero streaming state, then restarts workers against the same contexts.
This happens away from the wx thread. Nonzero generation tags isolate every
attempt: old progress/results cannot consume the following case, even if a
callback was already in flight. The model is unloaded only at session shutdown.

Model, VAD model, language, tuning, input selection and output configuration stay
locked until Stop/Close completes. Stop joins callbacks before closing the file;
completed attempts are flushed even if their wx event has not been handled.
Initialization, preparation and logging failures are reported explicitly.
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

### Contextual lexical repair (CMD-LEX-010)

The command layer applies `CommandLexicalRepair` after spoken-number normalization
and before assembly in Core and direct command processing. Static root families
recover unknown channel/fixture/group tokens only in a target slot followed by a
numeric selection, range start, or `all`. Exact profile aliases remain authoritative;
matching must identify one role. Number tokens and global-operation words are never
repaired. No spelling library, fuzzy distance, or ASR dependency is involved.

Internal commas admitted by vocabulary GBNF become whitespace unless they connect
numeric selection-list terms. Terminal punctuation retains existing behavior.
Repair neither owns nor resets pending assembly or linguistic context, and the
existing parser, semantic validator and renderer remain authoritative.

Raw, normalized and repaired fragments are available independently in recognition
results and JSONL. Small repair records explain each change; assembly text can still
include earlier utterances. Normalization timing includes lexical repair. Grammar
and Compact Prompt both default to enabled for the next live comparison; other
Whisper, model, decoding and VAD defaults are unchanged.

### Independent Whisper guidance (ASR-GUIDE-010)

**Vocabulary grammar** (`whisper.grammar_enabled`) and **Compact vocabulary
prompt** (`whisper.compact_prompt`) are independent. All four combinations are
supported: grammar/prompt on/on, on/off, off/on and off/off. Off/off supplies a
neutral recognition baseline with no grammar rules, start-rule lookup, grammar
penalty or initial prompt, while retaining the selected model, language, decoding
and endpoint settings. `no_context=true` is preserved. Enabled switches allow
guidance from the profile's hints; absent hints do not manufacture guidance.

Previously grammar-off cleared only the GBNF text, leaving `startRule="root"`.
Validation correctly rejected that inconsistent state before loading a model.
Grammar-off now clears the text, start rule and effective penalty. Validation of
stale start rules and malformed grammars remains strict.

The existing vocabulary GBNF is unchanged for A/B evaluation. Its comma separator
supports lexical lists and can also admit sequences such as `canal, 35 al 50`
and `fixture, 155, 50`. Lexical acceptance does not establish command validity;
the shared semantic parser and validator remain authoritative. This change does
not encode command semantics into Whisper or adjust punctuation alternatives.

Each JSONL row adds `recognition_settings.active_guidance`, using backend-reported
typed settings rather than inferring application from frozen hints. For Whisper:

- `whisper.grammar_enabled` and `whisper.compact_prompt` report configured switches;
- `whisper.grammar_passed` and `whisper.initial_prompt_passed` report the parameters
  supplied to an actual `whisper_full()` call in this attempt;
- `whisper.grammar_penalty` is present only when grammar was actually passed.

Passed flags start false and reset at every attempt. No inference (including a
no-speech attempt) leaves both false even when switches are enabled. Enabled
guidance with no corresponding hints also stays unpassed. Unsupported backends
report `active_guidance: null`. This is an additive field in JSONL schema 2;
existing typed `tuning` remains the frozen configuration snapshot. Backend-local
diagnostics are also available through `attemptDiagnostics()` for debugging.

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

## JSONL schema version 2

Each UTF-8 line contains one self-contained JSON object:

- `schema_version`, `corpus_version`, `session_id`, `timestamp` (UTC ISO 8601);
- `case_id`, `category`, `displayed_phrase`;
- `expected_semantic_result` (the shared command description or null),
  `expected_no_command` (explicit boolean);
- `recognition_outcome` (`final_result`, `no_speech`, `empty_asr_result`, `operator_no_result`);
- `raw_transcript`, `normalized_transcript`, `assembly_state`, `assembly_text`;
- additive CMD-LEX-010 fields: `repaired_transcript` (the fragment submitted to
  assembly), `repair_applied`, and `repairs` (original, replacement, category,
  reason). These are null for no-result outcomes, and empty text / false / an
  empty array for empty ASR results. Version-2 readers may ignore new fields;
  older version-2 rows can omit them. `normalized_transcript` retains its meaning;
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
- `metrics.audio_duration_seconds`, `metrics.inference_duration_seconds` (nullable);
- `attempt_generation` (nonzero session-local generation);
- `diagnostics.audio_samples`, `audio_duration_seconds`, `input_peak`, `input_rms`,
  `speech_detected`, `speech_start_seconds`, `utterance_duration_seconds`,
  `inference_duration_seconds`, `dropped_audio_blocks`, `dropped_vad_frames`,
  `dropped_inference_jobs`. Unsupported counters/timings are null.

For `no_speech` and `operator_no_result`, transcript/assembly text, semantic
result, rendering, detected language and inference metrics are null; assembly and
validation are `not_attempted`. For `empty_asr_result`, transcript strings are
empty and inference metrics/language remain available. Absence outcomes retain
armed diagnostics. Version 1 used `no_result` for operator intervention; readers
must select interpretation by `schema_version`. The corpus remains timmy-live-v1.

GPU-010 adds compute metadata without changing schema-2 outcome semantics:
`requested_compute_backend`, `requested_device`, `active_compute_backend`,
`active_device`, `compute_device_index`, `compute_device_name`, `fallback_used`,
`fallback_reason`, and `compute_discovery` (compiled/available Vulkan and detected
device IDs, names, indices and classes). Active values are null when the backend
does not report them. Older rows omit these fields; readers must tolerate missing
or unknown additive fields. Compute diagnostics survive no-result outcomes.

Armed input measurements count selected-channel samples delivered to Core while
armed, before adaptation to 16 kHz. Duration uses the source sample rate; peak is
the maximum absolute sample and RMS includes all armed samples. Speech and endpoint
timings use backend progress timestamps relative to SPEAK NOW on a steady clock.
No speech event means `speech_detected: false`; missing stage timings stay null.
Queue counters describe drops inside the backend, not microphone-driver losses.

The existing `metrics` describe inference input/work: Whisper audio duration is
actual inference sample count divided by 16,000, including endpoint padding, and
inference duration measures `whisper_full()`. These differ from armed input duration.
Model load time is excluded. Final-result writes run on the recognition worker;
operator and timeout writes run on the frontend thread. Neither path writes from
the realtime capture callback. Every attempt is flushed before publication.

Deterministic Core and GUI tests use fake ASR without devices, models, GPUs or
consoles. An additional strict JSON decoder test runs when Python 3 is available;
it uses only Python's standard library. Exact inference-input WAV capture and
headless replay are described below. Automated parameter sweeps remain outside
this feature.


### ASR-PERF-010 diagnostic additions

Schema version 2 is retained: these are additive, nullable fields under
`diagnostics`. Readers must tolerate missing optional fields in older version 2
rows and ignore extra diagnostic fields. The existing `metrics` object and
version 1/2 outcome interpretations are unchanged.

| Fields | Meaning and scope |
| --- | --- |
| `armed_duration_seconds` | Wall time since the pipeline armed, frozen on disarm. |
| `captured_frames`, `dropped_capture_frames`, `captured_duration_seconds` | AudioEngine counter deltas since arming, in **device frames** before channel selection/resampling. Captured duration includes dropped frames; delivered `audio_samples` / `audio_duration_seconds` retain their existing selected-input meaning. |
| `capture_ring_occupancy_frames`, `capture_ring_max_occupancy_frames`, `capture_ring_capacity_frames` | Sampled current occupancy, maximum sampled **during this attempt**, and capacity, in device frames. Sampling occurs during input delivery and diagnostic reads; transient peaks can occur between samples. |
| `capture_session_ring_high_water_frames` | AudioEngine's producer-sampled high-water since capture started. Explicitly a **capture-session** metric; it can predate the attempt. Available separately from the attempt maximum so earlier session peaks are not mistaken for attempt backlog. |
| `vad_queue_depth`, `vad_queue_max_depth`, `inference_queue_depth`, `inference_queue_max_depth` | Current queued items and maximum depth since attempt preparation; excludes dequeued/in-flight work. VAD items are 512 mono samples at 16 kHz; inference items are utterances (or five-second windows without VAD). |
| `dropped_vad_frames`, `dropped_inference_jobs` | Number of evicted queue items. The bounds remain 256 VAD frames and two inference jobs, dropping oldest. VAD frames here are queue items, not device PCM frames. |
| `oldest_vad_queue_age_seconds`, `vad_queued_audio_duration_seconds` | Oldest surviving item's wall-clock queue residence (null when empty) and total audio duration currently queued (zero when empty). Residence does not include upstream ring delay or partial-frame accumulation. |
| `vad_queue_wait_total_seconds`, `vad_queue_wait_max_seconds`, `inference_queue_wait_total_seconds`, `inference_queue_wait_max_seconds` | Sum and maximum residence of **dequeued** items since preparation; excludes evicted items and currently waiting items. |
| `speech_start_seconds`, `utterance_finalized_seconds`, `utterance_duration_seconds` | Observed worker-stage offsets from arming and speech-start-to-finalization wall duration. These are detection/endpoint processing times, not acoustic timestamps; VAD backlog can delay them. Finalization uses the returned job's timestamp when available. |
| `inference_duration_seconds`, `inference_audio_duration_seconds`, `real_time_factor` | Whisper computation duration, actual submitted PCM duration, and their ratio. RTF excludes queue wait and command work. Zero/unknown audio duration yields null RTF. |
| `end_of_utterance_to_result_seconds` | That returned utterance's endpoint-finalization-to-final-ASR-availability wall duration, including inference queue wait and transcript extraction. Null for fixed windows without an endpoint. |
| `command_processing_duration_seconds` | Normalization through completed command processing, excluding observer/JSONL/OSC work. |
| `normalization_duration_seconds`, `assembly_duration_seconds`, `semantic_parse_duration_seconds`, `validation_duration_seconds`, `rendering_duration_seconds` | Benchmark-only detailed stage durations. Unexecuted stages are null; assembly includes command-lock acquisition and context reset. |

`dropped_audio_blocks` remains readable for older/alternative backends. Whisper
cannot observe upstream block loss and now leaves it null. Do not infer device
capture health from this legacy field; use `dropped_capture_frames`. A missing
stats source, capture-session change during an attempt, or unavailable baseline
produces null capture accounting, never an assumed zero.

AudioEngine's backend-neutral `captureStats()` / `captureStatsSource()` API also
provides total captured/dropped device frames, sample rate, ring capacity/current
occupancy/high-water and a capture-session identifier. The shared stats handle
outlives the engine, and stopped capture retains its final totals. Restarting
capture resets these totals. Snapshot gauges are sampled, not an atomic snapshot
of the entire pipeline.

### Capture-to-inference audit

| Boundary | Audit finding |
| --- | --- |
| AudioEngine callback → PCM ring | Existing loss counter was private, and Whisper falsely published zero block losses. Corrected by exposing device-frame accounting. Added only bounded lock-free atomic operations and ring queries; no callback logging, allocation, locks, GUI, file I/O or recognition work. |
| Ring → audio dispatch worker | The worker copies and commits ring reads before invoking consumers. Slow consumers can fill the ring; capture loss/current occupancy/high-water now expose that pressure. |
| AudioChannelSelector → AudioFormatAdapter | Channel selection and resampling allocate/work on the dispatch worker, never the realtime callback. Capture stats are read directly through the audio-owned handle, preserving original device-frame units across selection/resampling. No processing or buffering policy changed. |
| RecognitionPipeline → backend | Attempt gating excludes disarmed input and resets resampler/command state before backend preparation. Capture baselines are taken at arming; counters freeze at disarming. Detailed input scanning and command clocks remain benchmark-only. |
| Backend → Silero worker | PCM is accumulated into bounded frames under the existing queue lock; VAD runs after dequeue outside that lock. Depth, drops, age, queued audio and residence now reveal backlog. |
| Silero → Whisper inference worker | Completed utterances use the same bounded drop-oldest queue. Each job keeps its endpoint timestamp, avoiding incorrect latency attribution when later utterances finalize before an earlier result returns. Inference still runs outside the queue lock. |
| Stop / Next / Retry | Existing cooperative inference cancellation and worker joins remain intact. Queue histories and inference timings reset only after old workers join; model/VAD contexts retain the existing session lifecycle. |

No additional blocking defect requiring a behavior correction was found in these
boundaries. Model selection, CPU/GPU policy, decoder settings, prompt/grammar,
Silero endpoint options, command semantics, rendering and OSC behavior are
unchanged. Upstream ring occupancy and worker-stage timestamps are observations;
this change does not flush extra captured audio or tune recognition to hide backlog.

## Contextual numeric-role diagnostics (CMD-NUM-020)

The live corpus and expected semantic results remain unchanged. Deterministic
regressions feed connector-loss transcripts into the same recognition pipeline,
including `canales 1 a 10 excepto 5 a 50`, omitted targets, and ON/OFF bare lists.
JSONL adds `numeric_inference_reasons` (an array, empty for explicit parsing and
null for no-result attempts) and `recognition_settings.default_target`. Existing
`repair_applied`/`repairs` distinguish lexical repair from numeric inference;
both may be applied to one transcript. Benchmark Details shows the numeric reasons.

Keep the next live run on Whisper Base Q5_1, Spanish, Silero VAD with 300 ms
minimum silence, Vocabulary Grammar ON and Compact Prompt ON, using the current
beam search and grammar penalty. Select Channel as the default target for the
existing live corpus. This change does not tune ASR or run a Small-model comparison;
the next microphone benchmark measures semantic/contextual recovery alone.


## Exact inference-input capture and replay

The diagnostic audio follows the existing ownership boundaries:

```text
physical/device audio (AudioEngine callback -> bounded PCM ring)
  -> selected channel (audio dispatch worker)
  -> resampled 16 kHz mono float PCM (AudioFormatAdapter)
  -> VAD/endpoint utterance, or the existing five-second non-VAD window
  -> exact inference PCM (Whisper inference worker)
       -> optional float32 WAV + input diagnostics (Core)
       -> whisper_full() using the same immutable sample span
```

This is a diagnostic copy of an admitted inference job, not a second microphone
recording. The hook runs inside Whisper's cooperative cancellation admission,
after dequeue and endpoint preparation, immediately before `whisper_full()`.
It observes the very same `samples.data()` and sample count passed to inference,
including the endpoint padding that is actually present. It does not reconstruct
an utterance from delayed progress events. Queued jobs dropped or canceled before
admission have no WAV. Core closes admission after the first final result, timeout,
or operator no-result snapshot, preventing later queued jobs from creating files
outside the completed attempt. Several admitted jobs before a result (for example,
an inference failure followed by another utterance) have separate indices.

AudioEngine remains the sole physical-input owner; the ASR backend consumes PCM.
No new work or disk I/O was added to its realtime callback. Core scans/saves the
borrowed view synchronously on the inference worker, outside the backend queue
lock. Only scalar metadata is retained. No wxWidgets types or GUI object lifetime
are involved. Stop/Retry/Next join old workers before replacing the capture identity;
Stop waits for any active file write. A file-write failure is recorded without
vetoing inference or changing a command's acceptance.

### Enabling and locating recordings

1. Select the usual model, language, compute, decoder, VAD, profile and recognition
   hints. Open the benchmark window and choose a JSONL file.
2. Enable **Save ASR input WAVs** before Start. It freezes with the session and is
   disabled while that session is active. Default remains OFF.
3. Run an attempt normally. **Details** shows each inference's input levels,
   sample count, WAV path or save error. JSONL contains the same records.
4. Resolve `diagnostics.inference_inputs[*].wav_path` relative to the JSONL's
   parent directory. For `results/intercom.jsonl`, files are stored under
   `results/intercom_audio/`.

Names have the form:

```text
intercom_audio/s-<escaped-session>_c-<escaped-case>_a-<attempt>_g-<generation>_i-<inference>.wav
```

`attempt` and `inference` are one-based; retry number is attempt minus one. The
session ID separates appended sessions. Case/attempt/generation/inference separate
all jobs and retries. Components escape unsafe bytes as `_hh`, including literal
underscores, so punctuation cannot become path separators or collide with escapes.
Creation is exclusive: existing WAVs are never truncated or silently replaced.
The sibling directory is created only on the first actual inference when saving
is enabled. No-speech attempts with no admitted inference create no WAV; empty
successful ASR results and operator-no-result attempts retain any actual input
already admitted before their snapshot.

Files are RIFF/WAVE **mono IEEE float32 (format tag 3), 16,000 Hz**, with `fmt`,
`fact`, and `data` chunks and little-endian sample bits. No scaling, quantization,
resampling or normalization is performed during serialization. Signed zero and
finite float32 values, including samples above 1.0, round-trip bit for bit.
`timmy::audio::writeFloatWav` / `readFloatWav` own this small format utility. The
reader rejects unsupported formats, malformed/truncated chunks, nonfinite samples
and oversized files rather than silently converting them. Input is bounded to
9,600,000 samples (ten minutes at 16 kHz), well above current endpoint/window limits.
Generated WAVs are ignored by Git; selected recordings can later be copied to a
curated external corpus or deliberately added as small fixtures in a separate task.
Keep their originating JSONL row to preserve model/settings/provenance.

### Additive schema-version-2 fields

Existing field meanings, schema version and corpus are unchanged. Older v2 rows
without these additions remain valid. Readers should tolerate additional keys.

`recognition_settings.save_asr_input_wavs` records the frozen opt-in. Under
`diagnostics`, `inference_inputs` is null if no inference was admitted or the
backend does not report input observations; otherwise it is an ordered array:

| Field in each inference record | Meaning |
| --- | --- |
| `inference_index` | One-based job index within the attempt generation. |
| `sample_rate`, `sample_count`, `sample_format` | Exact backend input representation; Whisper uses 16000, submitted sample count, `ieee_float32`. |
| `wav_path` | Relative path from the JSONL parent; null when disabled or saving failed. |
| `wav_error` | Observable save failure; null when disabled or successful. Errors do not invalidate recognition. |
| `peak_linear`, `peak_dbfs` | Maximum absolute sample and `20*log10(peak)` of this inference buffer. |
| `rms_linear`, `rms_dbfs` | RMS over every submitted sample, including genuine endpoint padding, and `20*log10(RMS)`. |
| `near_full_scale_threshold` | 0.999, tested as `abs(sample) >= 0.999`. |
| `near_full_scale_samples`, `near_full_scale_ratio` | Count and count/sample_count satisfying that threshold. This is not proof of physical ADC clipping. |
| `speech_rms_dbfs`, `noise_rms_dbfs`, `estimated_snr_db` | Currently null: no reliable acoustic speech/noise sample windows are exported at this boundary. |

Silence has linear peak/RMS zero and null dBFS, avoiding non-JSON negative infinity.
These metrics remain available with WAV saving OFF and are independent of armed
input peak/RMS. Armed input includes pre-resampling audio throughout the open
attempt; inference metrics describe only each submitted backend buffer. VAD
speech-start/progress timestamps are worker wall times, not sample indices.
They must not be used to invent speech/noise RMS or SNR. Saved WAVs provide the
future evidence for carefully labeled, sample-based SNR analysis.

Input scanning is benchmark-only. Opt-in WAV saving adds diagnostic I/O on the
inference worker and can change
queue residence and end-to-result latency. `metrics.inference_duration_seconds`
and `diagnostics.inference_duration_seconds` still measure `whisper_full()` itself,
excluding the WAV write. Existing end-to-result/queue diagnostics expose the wider
latency. Audio samples and decoder/VAD defaults are unchanged.

### Headless replay and controlled comparisons

Replay uses the reusable Core `replayInferenceWav()` runner, the normal
`RecognitionPipeline`, backend worker, Whisper parameter/guidance construction,
and command parser/validator/renderer. It opens no AudioEngine or microphone and
never emits a command to a console. A finite, mono float32 WAV at exactly 16 kHz is
required; other WAV encodings/rates are explicitly rejected to preserve evidence.

```sh
timmy-cli asr replay --wav results/intercom_audio/<saved-name>.wav \
  --model models/<same-model>.bin --language es --compute cpu \
  --command-profile native --default-target channel --grammar on --prompt on
```

Select `--compute automatic|cpu|vulkan` and optional `--compute-device N` as in
`asr listen`. Choose `--command-profile native|hydra2`; both provide their normal
recognition hints without enabling transport. Decoder and endpoint values use the
same backend-owned descriptor IDs, types, ranges and defaults as the GUI:

```sh
# Examples: reproduce the frozen tuning values from recognition_settings.tuning.
timmy-cli asr replay --wav results/intercom_audio/<saved-name>.wav \
  --model models/<same-model>.bin --language es --compute cpu \
  --setting whisper.beam_size=5 --setting whisper.temperature=0.4 \
  --setting whisper.temperature_increment=1.0 --setting whisper.max_tokens=32 \
  --setting whisper.audio_context=0 --setting whisper.threads=4 \
  --setting whisper.grammar_penalty=20.0 --grammar off --prompt on
```

`--setting ID=VALUE`, `--grammar on|off` and `--prompt on|off` are shared with
`asr listen`; settings are not a separate replay-specific Whisper configuration.
`--setting` accepts booleans `on|off|true|false` and typed numeric values; invalid,
unknown, nonfinite and out-of-range settings fail explicitly. Grammar and compact
prompt switches are independent. With the same saved WAV, run all four pairs:

```sh
for grammar in on off; do
  for prompt in on off; do
    timmy-cli asr replay --wav results/intercom_audio/<saved-name>.wav \
      --model models/<same-model>.bin --language es --compute cpu \
      --grammar "$grammar" --prompt "$prompt" \
      > "replay-grammar-$grammar-prompt-$prompt.txt"
  done
done
```

Output includes raw/normalized/repaired transcript, repair reasons, semantic
intent, validation/rendering, detected language, input counts/levels, inference
seconds, RTF, requested settings, actual guidance-passed flags and compute
status. There is no fabricated benchmark PASS/FAIL: no expected utterance is
supplied to replay. `--timeout-seconds N` controls the inference wait (default
120); failure/timeout stops and joins the backend before returning nonzero.
A successful empty ASR result is an observed empty transcript, not a timeout.

The input is **already** a finalized inference buffer. Exact replay bypasses
channel selection, resampling, VAD, endpoint trimming/padding and five-second
window slicing, using `IAsrBackend::submitInferenceInput()` instead. VAD tuning
IDs and optional `--vad-model` remain accepted through the shared configuration,
but do not segment or modify this WAV; changing them cannot change its samples.
To investigate VAD itself would require continuous pre-endpoint input, a different
evidence boundary. Exact PCM is reproducible; model versions, runtime/device,
threading and decoding settings can still affect transcripts/timings. This task
does not promise cross-device bitwise deterministic inference.

No model/grammar/prompt/VAD defaults or live corpus cases changed. No gate,
compressor, limiter, AGC, denoiser, EQ, high-pass, new DSP dependency, or speculative
processing-plugin API was added. Future preprocessing comparisons can explicitly
produce a new WAV from this immutable corpus, then use the same replay path.

## Bounded Spanish comma repair

The confirmed existing meaning of **Canal 25 al 50** is **channel 25 at 50%**,
as defined by the corpus and semantic parser. It does not mean channels 25 through
50. Range selections continue to require their existing explicit grammar.

After ordinary transcript normalization, `CommandLexicalRepair` accepts only the
complete four-token form **`canal <positive identifier> , <0..100 percentage>`**.
The exact Spanish singular target must retain its canonical Channel role and `al`
must retain its normal separator role. One comma is replaced by `al`; parsing,
semantic validation and rendering remain authoritative. The lexical layer does
not instantiate a semantic parser or infer a selection range. Its repair record
has category `asr_separator`, replacement `al`, and an explicit bounded
identifier-to-percentage reason. Custom exact aliases still win.

Thus `canal 25, 50.` becomes `canal 25 al 50` -> `Channel 25 At 50`.
`canal, 35, 50,` only loses the neutral comma beside the target and remains
`canal 35, 50,`, a pending numeric list. Its trailing comma is evidence of a
possible continuation, so it is deliberately not forced into an intensity.
Plural targets, explicit list conjunctions/actions, a third numeric member,
English targets, unrelated syntax, invalid percentages and pending numeric
continuations do not qualify. Existing terminal punctuation behavior, CMD-LEX-010
root repairs, exact aliases and multi-utterance assembly retain their regression
coverage.

Deterministic tests cover bit-exact float WAV round trips (including signed zero,
subnormals and out-of-range levels), unsupported/malformed WAVs and odd RIFF chunks,
exclusive file creation and safe identity names, default OFF/no-inference behavior,
multiple jobs/retries/generation isolation, independent armed/inference metrics,
save failures without recognition failure, null SNR/silent dBFS, no-speech/empty/
operator-no-result outcomes, worker joins during shutdown, hardware-free replay,
replay errors/timeouts, independent grammar/prompt switches, strict additive v2
JSON decoding, GUI checkbox lifecycle and full command/pipeline regressions.
