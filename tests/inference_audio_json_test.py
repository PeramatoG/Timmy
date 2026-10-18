"""Strict independent decoding of additive v2 inference audio diagnostics."""
import json
from pathlib import PurePosixPath
import subprocess
import sys


def reject_constant(value):
    raise ValueError(value)


rows = [json.loads(line, parse_constant=reject_constant) for line in subprocess.check_output(
    [sys.argv[1], "--json-fixtures"], encoding="utf-8").splitlines()]
assert len(rows) == 4
for row in rows:
    assert row["schema_version"] == 2
    assert row["recognition_settings"]["save_asr_input_wavs"] is True
    inputs = row["diagnostics"]["inference_inputs"]
    if inputs is None:
        assert row["recognition_outcome"] == "no_speech"
        continue
    for audio in inputs:
        assert audio["sample_rate"] == 16000 and audio["sample_count"] == 11
        assert audio["sample_format"] == "ieee_float32"
        assert audio["peak_linear"] > 1.19 and audio["peak_dbfs"] > 0
        assert audio["rms_linear"] > 0 and audio["rms_dbfs"] < 0
        assert audio["near_full_scale_samples"] == 3
        assert audio["near_full_scale_ratio"] == 3 / 11
        assert audio["near_full_scale_threshold"] == 0.999
        assert all(audio[key] is None for key in ("speech_rms_dbfs", "noise_rms_dbfs", "estimated_snr_db"))
        if audio["wav_path"] is not None:
            path = PurePosixPath(audio["wav_path"])
            assert not path.is_absolute() and len(path.parts) == 2
            assert path.suffix == ".wav" and audio["wav_error"] is None
        else:
            assert audio["wav_error"]
assert len(rows[0]["diagnostics"]["inference_inputs"]) == 2
assert rows[0]["diagnostics"]["inference_inputs"][0]["inference_index"] != rows[0]["diagnostics"]["inference_inputs"][1]["inference_index"]
assert rows[1]["recognition_outcome"] == "empty_asr_result"
assert rows[3]["recognition_outcome"] == "operator_no_result"
assert rows[3]["raw_transcript"] is None
