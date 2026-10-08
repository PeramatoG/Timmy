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
assert entry["schema_version"] == 1 and entry["corpus_version"] == "timmy-live-v1"
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
    assert absent["recognition_outcome"] == "no_result"
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
