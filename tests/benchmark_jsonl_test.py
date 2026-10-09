"""Validate benchmark output with an independent strict JSON decoder."""
import json
from pathlib import Path
import subprocess
import sys

line = subprocess.check_output([sys.argv[1], "--json-fixture"], encoding="utf-8")
assert len(line.splitlines()) == 1
entry = json.loads(line, parse_constant=lambda value: (_ for _ in ()).throw(ValueError(value)))
required = {
    "schema_version", "corpus_version", "session_id", "timestamp", "case_id", "category",
    "displayed_phrase", "expected_semantic_result", "expected_no_command", "raw_transcript",
    "normalized_transcript", "assembly_state", "assembly_text", "actual_semantic_description",
    "validation_state", "rendered_command", "result", "detected_language", "asr_engine_id",
    "model_filename", "vad_model_filename", "selected_language", "recognition_settings",
    "attempt_number", "retry_number", "metrics", "recognition_outcome", "audio_source",
}
assert required <= entry.keys()
assert entry["raw_transcript"] == '"quoted"\\path\n\t\r\b\f\x01 — español'
assert entry["asr_engine_id"] == 'fake"\\engine'
assert entry["model_filename"] == "niño.bin"
assert entry["schema_version"] == 2 and entry["corpus_version"] == "timmy-live-v1"
assert entry["result"] == "PASS" and entry["expected_no_command"] is False
assert entry["attempt_number"] == 2 and entry["retry_number"] == 1
assert entry["metrics"] == {"audio_duration_seconds": 2.5, "inference_duration_seconds": 0.75}
snapshot = entry["recognition_settings"]
assert snapshot["model_path"] == str(Path("models/niño.bin"))
assert snapshot["vad_model_path"] == str(Path("models/vad.bin"))
assert snapshot["language"] == "es" and snapshot["engine_id"] == entry["asr_engine_id"]
assert len(snapshot["tuning"]) == 14
assert type(snapshot["tuning"]["whisper.threads"]) is int
assert any(type(value) is bool for value in snapshot["tuning"].values())
assert any(type(value) is float for value in snapshot["tuning"].values())
assert snapshot["recognition_hints"]["constraint_intent"] == "command_phrase"
assert snapshot["recognition_hints"]["vocabulary"] and snapshot["recognition_hints"]["expected_phrases"]

assert entry["recognition_outcome"] == "final_result"
assert entry["audio_source"] == {
    "device_display_name": 'Device "one"\\input\n',
    "channel_index": 2,
    "channel_display_name": 'Input "three"\t',
}
no_result_lines = subprocess.check_output([sys.argv[1], "--no-result-fixture"], encoding="utf-8").splitlines()
assert len(no_result_lines) == 2
for index, line in enumerate(no_result_lines):
    absent = json.loads(line)
    assert required <= absent.keys()
    assert absent["recognition_outcome"] == "operator_no_result"
    assert absent["result"] == ("FAIL" if index == 0 else "PASS")
    assert absent["expected_no_command"] == (index == 1)
    for field in ("raw_transcript", "normalized_transcript", "assembly_text", "actual_semantic_description",
                  "rendered_command", "detected_language"):
        assert absent[field] is None, (field, absent[field])
    assert absent["assembly_state"] == "not_attempted"
    assert absent["validation_state"] == "not_attempted"
    assert absent["validation_diagnostic"] == "" and absent["rendering_diagnostic"] == ""
    assert absent["metrics"] == {"audio_duration_seconds": None, "inference_duration_seconds": None}
    assert absent["audio_source"]["device_display_name"] == entry["audio_source"]["device_display_name"]
    assert absent["audio_source"]["channel_index"] == 2
    assert absent["audio_source"]["channel_display_name"] == (entry["audio_source"]["channel_display_name"] if index == 0 else None)

assert entry["attempt_generation"] == 0
assert entry["diagnostics"]["speech_start_seconds"] is None
assert entry["diagnostics"]["dropped_vad_frames"] is None

rows = [json.loads(line) for line in subprocess.check_output(
    [sys.argv[1], "--outcome-fixtures"], encoding="utf-8").splitlines()]
assert len(rows) == 8
for index, outcome in enumerate(("final_result", "no_speech", "empty_asr_result", "operator_no_result")):
    positive, negative = rows[2 * index:2 * index + 2]
    for row in (positive, negative):
        assert row["schema_version"] == 2 and row["recognition_outcome"] == outcome
        assert row["attempt_generation"] == 42
        assert row["diagnostics"] == {
            "audio_samples": 48000, "audio_duration_seconds": 3,
            "input_peak": 0.5, "input_rms": 0.25, "speech_detected": True,
            "speech_start_seconds": 0.2, "utterance_duration_seconds": 2,
            "inference_duration_seconds": 0.75, "dropped_audio_blocks": 1,
            "dropped_vad_frames": 2, "dropped_inference_jobs": 3,
        }
        if outcome in ("no_speech", "operator_no_result"):
            assert row["raw_transcript"] is None and row["metrics"]["inference_duration_seconds"] is None
        elif outcome == "empty_asr_result":
            assert row["raw_transcript"] == row["normalized_transcript"] == ""
            assert row["actual_semantic_description"] is None
            assert row["metrics"]["inference_duration_seconds"] == 0.75
    assert positive["result"] == ("PASS" if outcome == "final_result" else "FAIL")
    assert negative["result"] == ("FAIL" if outcome == "final_result" else "PASS")
