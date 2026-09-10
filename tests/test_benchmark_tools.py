#!/usr/bin/env python3
"""Contract tests for the benchmark comparison tool."""

import json
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "compare_benchmarks.py"


def report(machine="test-machine"):
    return {
        "schema_version": 1,
        "build": {"id": "abc", "type": "Release"},
        "environment": {"os": "Linux", "cpu_count": 8, "machine": machine},
        "fixture_id": "native-default-v1",
        "metrics": {
            "vfs_io": {"avg_ms": 1.0},
            "json_update": {"avg_ms": 2.0},
            "first_frame_ms": 3.0,
            "steady_frame_ms": 4.0,
            "process_memory_bytes": 5.0,
        },
    }


with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    baseline = directory / "baseline.json"
    candidate = directory / "candidate.json"
    incompatible = directory / "incompatible.json"
    baseline.write_text(json.dumps(report()), encoding="utf-8")
    candidate.write_text(json.dumps(report()), encoding="utf-8")
    incompatible.write_text(json.dumps(report("different-machine")), encoding="utf-8")

    compatible = subprocess.run([sys.executable, str(TOOL), str(baseline), str(candidate)],
                                capture_output=True, text=True, check=False)
    if compatible.returncode != 0 or "+0.00%" not in compatible.stdout:
        raise SystemExit("compatible benchmark reports were not compared")

    rejected = subprocess.run([sys.executable, str(TOOL), str(baseline), str(incompatible)],
                              capture_output=True, text=True, check=False)
    if rejected.returncode == 0 or "incompatible" not in rejected.stderr:
        raise SystemExit("different benchmark environments were not rejected")
