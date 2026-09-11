#!/usr/bin/env python3
"""Contract tests for the benchmark comparison tool."""

import json
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "compare_benchmarks.py"
EDITOR_COMPARE_TOOL = ROOT / "tools" / "compare_editor_benchmarks.py"
EDITOR_SUMMARY_TOOL = ROOT / "tools" / "summarize_editor_benchmarks.py"


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
            "transition_fps": 60.0,
            "process_memory_bytes": 5.0,
        },
    }


def editor_report(machine="test-machine"):
    return {
        "schema_version": 1,
        "build": {"id": "abc", "type": "Debug"},
        "environment": {"os": "Linux", "cpu_count": 8, "machine": machine},
        "fixture_id": "editor-headless-default-v1",
        "metrics": {
            "graph_drag_step_ms": 1.0,
            "preview_delivery_ms": 2.0,
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

    regressed = directory / "regressed.json"
    regressed_report = report()
    regressed_report["metrics"]["steady_frame_ms"] = 8.0
    regressed_report["metrics"]["transition_fps"] = 30.0
    regressed.write_text(json.dumps(regressed_report), encoding="utf-8")
    warned = subprocess.run([sys.executable, str(TOOL), str(baseline), str(regressed),
                             "--warn-percent", "20"],
                            capture_output=True, text=True, check=False)
    if warned.returncode != 0 or "WARNING" not in warned.stdout:
        raise SystemExit("regressed benchmark reports were not warned about")
    gated = subprocess.run([sys.executable, str(TOOL), str(baseline), str(regressed),
                            "--fail-percent", "20"],
                           capture_output=True, text=True, check=False)
    if gated.returncode == 0 or "threshold breached" not in gated.stderr:
        raise SystemExit("regressed benchmark reports did not breach the fail gate")

    editor_baseline = directory / "editor-baseline.json"
    editor_candidate = directory / "editor-candidate.json"
    editor_incompatible = directory / "editor-incompatible.json"
    editor_baseline.write_text(json.dumps(editor_report()), encoding="utf-8")
    editor_candidate.write_text(json.dumps(editor_report()), encoding="utf-8")
    editor_incompatible.write_text(json.dumps(editor_report("different-machine")), encoding="utf-8")

    editor_comparison_path = directory / "editor-comparison.json"
    editor_comparison = subprocess.run(
        [sys.executable, str(EDITOR_COMPARE_TOOL), str(editor_baseline), str(editor_candidate),
         "--output", str(editor_comparison_path)],
        capture_output=True, text=True, check=False)
    if editor_comparison.returncode != 0 or "+0.00%" not in editor_comparison.stdout:
        raise SystemExit("compatible editor benchmark reports were not compared")
    editor_comparison_output = json.loads(editor_comparison_path.read_text(encoding="utf-8"))
    if not editor_comparison_output.get("compatible") or len(editor_comparison_output.get("metrics", [])) != 2:
        raise SystemExit("editor benchmark comparison JSON was not published")

    editor_rejected = subprocess.run(
        [sys.executable, str(EDITOR_COMPARE_TOOL), str(editor_baseline), str(editor_incompatible)],
        capture_output=True, text=True, check=False)
    if editor_rejected.returncode == 0 or "incompatible" not in editor_rejected.stderr:
        raise SystemExit("different editor benchmark environments were not rejected")

    editor_summary = subprocess.run(
        [sys.executable, str(EDITOR_SUMMARY_TOOL), str(editor_baseline), str(editor_candidate)],
        capture_output=True, text=True, check=False)
    if editor_summary.returncode != 0 or "samples: 2" not in editor_summary.stdout:
        raise SystemExit("compatible editor benchmark reports were not summarized")

    single_sample = subprocess.run(
        [sys.executable, str(EDITOR_SUMMARY_TOOL), str(editor_baseline)],
        capture_output=True, text=True, check=False)
    if single_sample.returncode == 0 or "at least two" not in single_sample.stderr:
        raise SystemExit("single editor benchmark report was accepted as a series")
