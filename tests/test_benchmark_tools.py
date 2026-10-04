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


def report(machine="test-machine", architecture="x86_64", cpu_model="Test CPU"):
    return {
        "schema_version": 2,
        "build": {"id": "abc", "type": "Release"},
        "environment": {"os": "Linux", "architecture": architecture, "cpu_model": cpu_model,
                        "cpu_count": 8, "machine": machine},
        "fixture_id": "native-default-v2",
        "metrics": {
            "vfs_io": {"avg_ms": 1.0},
            "json_update": {"avg_ms": 2.0},
            "first_frame_ms": 3.0,
            "steady_frame_ms": 4.0,
            "transition_fps": 60.0,
            "process_memory_bytes": 5.0,
        },
    }


def editor_report(machine="test-machine", architecture="x86_64", cpu_model="Test CPU"):
    return {
        "schema_version": 2,
        "build": {"id": "abc", "type": "Debug"},
        "environment": {"os": "Linux", "architecture": architecture, "cpu_model": cpu_model,
                        "cpu_count": 8, "machine": machine},
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
    incompatible.write_text(json.dumps(report(architecture="arm64")), encoding="utf-8")

    compatible = subprocess.run([sys.executable, str(TOOL), str(baseline), str(candidate)],
                                capture_output=True, text=True, check=False)
    if compatible.returncode != 0 or "+0.00%" not in compatible.stdout:
        raise SystemExit("compatible benchmark reports were not compared")

    machine_only = directory / "machine-only-diff.json"
    machine_only.write_text(json.dumps(report("different-machine")), encoding="utf-8")
    machine_compatible = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(machine_only)],
        capture_output=True, text=True, check=False)
    if machine_compatible.returncode != 0:
        raise SystemExit("machine-only differences should stay compatible")

    rejected = subprocess.run([sys.executable, str(TOOL), str(baseline), str(incompatible)],
                              capture_output=True, text=True, check=False)
    if rejected.returncode != 0 or "skipping" not in (rejected.stdout + rejected.stderr):
        raise SystemExit("incompatible benchmark environments did not skip with exit 0")

    # Host pin (regression guard for the 149ddf4 false-red incident):
    # `runs-on: ubuntu-24.04` spans six CPU classes with a ~2.2x spread, so a
    # different cpu_model must NOT be compared as if it were the same host.
    other_cpu = directory / "other-cpu.json"
    other_cpu.write_text(json.dumps(report(cpu_model="AMD EPYC 7763 64-Core Processor")),
                         encoding="utf-8")
    cpu_rejected = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(other_cpu),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if cpu_rejected.returncode != 0:
        raise SystemExit("different cpu_model must skip with exit 0, got "
                         f"{cpu_rejected.returncode}")
    cpu_output = cpu_rejected.stdout + cpu_rejected.stderr
    if "skipping" not in cpu_output:
        raise SystemExit("cpu_model mismatch did not report a skip")
    # A skipped gate is not a passing gate: it must say so loudly and name both
    # hosts, otherwise the lost coverage is invisible on a green job.
    if "SKIPPED" not in cpu_output or "environment.cpu_model" not in cpu_output:
        raise SystemExit("cpu_model mismatch did not emit a loud SKIPPED banner")
    if "Test CPU" not in cpu_output or "AMD EPYC 7763" not in cpu_output:
        raise SystemExit("SKIPPED banner did not name both baseline and candidate hosts")

    # The skip must reach the step summary, not just the log.
    summary = directory / "summary.md"
    subprocess.run([sys.executable, str(TOOL), str(baseline), str(other_cpu),
                    "--summary", str(summary)], capture_output=True, text=True, check=False)
    if "SKIPPED" not in summary.read_text(encoding="utf-8"):
        raise SystemExit("host-mismatch skip did not reach the step summary")

    # Same host, worse numbers -> still a REAL gate failure (exit 2), never
    # masked by the host pin.
    slow_host = directory / "same-cpu-regression.json"
    regressed_same_cpu = report()
    regressed_same_cpu["metrics"]["steady_frame_ms"] = 8.0
    regressed_same_cpu["metrics"]["transition_fps"] = 30.0
    slow_host.write_text(json.dumps(regressed_same_cpu), encoding="utf-8")
    still_red = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(slow_host),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if still_red.returncode != 2:
        raise SystemExit("same-CPU regression must still fail with exit 2, got "
                         f"{still_red.returncode}")

    # A gate that ran must announce the host it ran on, so the summary shows
    # "compared" rather than silence.
    ran_summary = directory / "ran-summary.md"
    subprocess.run([sys.executable, str(TOOL), str(baseline), str(candidate),
                    "--summary", str(ran_summary)], capture_output=True, text=True, check=False)
    if "Test CPU" not in ran_summary.read_text(encoding="utf-8"):
        raise SystemExit("completed comparison did not record its host in the step summary")

    # ── Baseline selection (drift fix) ─────────────────────────────────────
    # CI picks its baseline with `gh run list --status success --limit 1`,
    # which cannot see this gate's verdict: a run that SKIPPED still ends
    # conclusion=success. So the skipped run becomes the next baseline and the
    # regression the gate missed is laundered into the reference. The tool
    # therefore takes a NEWEST-FIRST WINDOW of baselines and picks the first
    # one whose compatibility key matches the candidate.

    # Newest baseline is on another host, an older one is on this host.
    # The gate must walk past the newer one instead of skipping -- this is the
    # whole point: today's CI would skip and hand its ungated numbers on.
    newest_other_host = directory / "newest-other-host.json"
    newest_other_host.write_text(json.dumps(report(cpu_model="AMD EPYC 7763 64-Core Processor")),
                                 encoding="utf-8")
    windowed = subprocess.run(
        [sys.executable, str(TOOL), str(newest_other_host), str(baseline), str(candidate),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    windowed_output = windowed.stdout + windowed.stderr
    if windowed.returncode != 0 or "Compatible benchmark reports" not in windowed_output:
        raise SystemExit("a same-host baseline further back in the window must be used; "
                         f"got exit {windowed.returncode}: {windowed_output}")
    if "SKIPPED" in windowed_output:
        raise SystemExit("the tool skipped although a compatible baseline existed in the window")
    if str(baseline) not in windowed_output:
        raise SystemExit("the gate did not report WHICH baseline it measured against")
    if "passed over" not in windowed_output:
        raise SystemExit("the gate did not report that it walked past a newer baseline")

    # Newest-first ordering is load-bearing: the FIRST match wins, so handing
    # the same window reversed must pick a different (the other) same-host
    # baseline. Without this, a reversed window would silently compare against
    # an arbitrarily old reference.
    older_same_host = directory / "older-same-host.json"
    older_same_host_report = report()
    older_same_host_report["metrics"]["steady_frame_ms"] = 4.5
    older_same_host.write_text(json.dumps(older_same_host_report), encoding="utf-8")
    reversed_window = subprocess.run(
        [sys.executable, str(TOOL), str(older_same_host), str(baseline), str(candidate)],
        capture_output=True, text=True, check=False)
    reversed_output = reversed_window.stdout + reversed_window.stderr
    if str(older_same_host) not in reversed_output or str(baseline) in reversed_output:
        raise SystemExit("baseline selection ignored the newest-first ordering")

    # No matching baseline anywhere in the window -> loud SKIP, never a silent
    # pass. This is the "brand new runner class" case: it costs one run per
    # host, and CI must be able to SEE that it happened.
    all_other_hosts = directory / "all-other-hosts.json"
    all_other_hosts.write_text(json.dumps(report(cpu_model="AMD EPYC 9V74 80-Core Processor")),
                               encoding="utf-8")
    no_match = subprocess.run(
        [sys.executable, str(TOOL), str(all_other_hosts), str(newest_other_host), str(candidate)],
        capture_output=True, text=True, check=False)
    no_match_output = no_match.stdout + no_match.stderr
    if no_match.returncode != 0:
        raise SystemExit(f"no compatible baseline must skip with exit 0, got {no_match.returncode}")
    if "SKIPPED" not in no_match_output or "no compatible baseline" not in no_match_output:
        raise SystemExit("no matching baseline did not emit a loud SKIPPED banner")
    if "Test CPU" not in no_match_output:
        raise SystemExit("the no-baseline skip did not name the candidate host it lacked a "
                         "reference for")
    if str(all_other_hosts) not in no_match_output:
        raise SystemExit("the no-baseline skip did not report which baselines it walked past")
    # And it must reach the step summary, or a green job hides it.
    no_match_summary = directory / "no-match-summary.md"
    subprocess.run([sys.executable, str(TOOL), str(all_other_hosts), str(candidate),
                    "--summary", str(no_match_summary)], capture_output=True, text=True, check=False)
    if "SKIPPED" not in no_match_summary.read_text(encoding="utf-8"):
        raise SystemExit("the no-baseline skip did not reach the step summary")

    # A corrupt artifact in the window must not blind the gate to the good
    # baselines behind it: one unreadable file is skipped, the walk continues.
    corrupt = directory / "corrupt.json"
    corrupt.write_text("{not json", encoding="utf-8")
    corrupt_window = subprocess.run(
        [sys.executable, str(TOOL), str(corrupt), str(newest_other_host), str(baseline),
         str(candidate)], capture_output=True, text=True, check=False)
    corrupt_output = corrupt_window.stdout + corrupt_window.stderr
    if corrupt_window.returncode != 0 or "Compatible benchmark reports" not in corrupt_output:
        raise SystemExit("an unreadable baseline blinded the gate to a usable one behind it")
    if "unreadable baseline" not in corrupt_output:
        raise SystemExit("the unreadable baseline was passed over silently")

    # Every baseline corrupt and none matching -> still a skip (exit 0), not a
    # crash: the candidate itself was readable, so this is missing coverage,
    # not a tool failure.
    corrupt_only = subprocess.run(
        [sys.executable, str(TOOL), str(corrupt), str(all_other_hosts), str(candidate)],
        capture_output=True, text=True, check=False)
    if corrupt_only.returncode != 0:
        raise SystemExit(f"an all-unusable window must skip with exit 0, got {corrupt_only.returncode}")

    # The regression must still be caught THROUGH the window path: a same-host
    # regression whose window starts with another host must go red, not skip.
    windowed_red = subprocess.run(
        [sys.executable, str(TOOL), str(newest_other_host), str(baseline), str(slow_host),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if windowed_red.returncode != 2:
        raise SystemExit("a real regression behind a newer cross-host baseline must still fail "
                         f"with exit 2, got {windowed_red.returncode}")

    # A malformed CANDIDATE is a tool failure (exit 1) and must never be
    # softened into a skip by the new selection logic.
    broken_candidate = directory / "broken-candidate.json"
    broken_candidate.write_text("{oops", encoding="utf-8")
    bad_candidate = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(broken_candidate)],
        capture_output=True, text=True, check=False)
    if bad_candidate.returncode != 1:
        raise SystemExit(f"a malformed candidate must exit 1, got {bad_candidate.returncode}")

    wrong_architecture = directory / "wrong-architecture.json"
    wrong_architecture.write_text(json.dumps(report(architecture="arm64")), encoding="utf-8")
    architecture_rejected = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(wrong_architecture)],
        capture_output=True, text=True, check=False)
    if architecture_rejected.returncode != 0 or "skipping" not in (
            architecture_rejected.stdout + architecture_rejected.stderr):
        raise SystemExit("different benchmark architectures did not skip with exit 0")

    legacy = directory / "legacy-v1.json"
    legacy_report = report()
    legacy_report["schema_version"] = 1
    legacy_report["environment"].pop("architecture")
    legacy_report["environment"].pop("cpu_model")
    legacy.write_text(json.dumps(legacy_report), encoding="utf-8")
    legacy_comparison = subprocess.run(
        [sys.executable, str(TOOL), str(legacy), str(legacy)],
        capture_output=True, text=True, check=False)
    if legacy_comparison.returncode != 0:
        raise SystemExit("legacy benchmark schema v1 reports were not preserved")

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
    if gated.returncode != 2 or "threshold breached" not in gated.stderr:
        raise SystemExit("regressed benchmark reports did not breach the fail gate with exit 2")

    ci_failed = directory / "ci-failed.json"
    ci_failed_report = report()
    ci_failed_report["metrics"]["steady_frame_ms"] = 4.0 * 1.40
    ci_failed.write_text(json.dumps(ci_failed_report), encoding="utf-8")
    ci_breached = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(ci_failed),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if ci_breached.returncode != 2 or "threshold breached" not in ci_breached.stderr:
        raise SystemExit("CI boundary +40% regression did not breach the fail gate with exit 2")

    ci_warned = directory / "ci-warned.json"
    ci_warned_report = report()
    ci_warned_report["metrics"]["steady_frame_ms"] = 4.0 * 1.25
    ci_warned.write_text(json.dumps(ci_warned_report), encoding="utf-8")
    ci_warning = subprocess.run(
        [sys.executable, str(TOOL), str(baseline), str(ci_warned),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if ci_warning.returncode != 0 or "WARNING" not in ci_warning.stdout:
        raise SystemExit("CI boundary +25% regression did not warn with exit 0")

    # A1-fix (B4): sub-floor big-percent noise must NOT breach. Mirrors the
    # three consecutive CI reds (vfs_io +50..95% at +5..10µs absolute).
    floor_baseline = directory / "floor-baseline.json"
    floor_report = report()
    floor_report["metrics"]["vfs_io"]["avg_ms"] = 0.010799
    floor_report["metrics"]["json_update"]["avg_ms"] = 0.189400
    floor_baseline.write_text(json.dumps(floor_report), encoding="utf-8")
    floor_candidate = directory / "floor-candidate.json"
    floor_candidate_report = report()
    floor_candidate_report["metrics"]["vfs_io"]["avg_ms"] = 0.021041  # +94.8%, +10µs
    floor_candidate_report["metrics"]["json_update"]["avg_ms"] = 0.287375  # +51.7%, +98µs
    floor_candidate.write_text(json.dumps(floor_candidate_report), encoding="utf-8")
    floor_pass = subprocess.run(
        [sys.executable, str(TOOL), str(floor_baseline), str(floor_candidate),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if floor_pass.returncode != 0:
        raise SystemExit("sub-floor host noise breached the fail gate "
                         f"(exit {floor_pass.returncode}): {floor_pass.stderr.strip()}")

    # Tripwire proof (bilerek-boz): a REAL regression clearing both the
    # percent gate and the absolute floor must still breach with exit 2.
    trip_baseline = directory / "trip-baseline.json"
    trip_baseline.write_text(json.dumps(floor_report), encoding="utf-8")
    trip_candidate = directory / "trip-candidate.json"
    trip_candidate_report = report()
    trip_candidate_report["metrics"]["vfs_io"]["avg_ms"] = 0.010799
    trip_candidate_report["metrics"]["json_update"]["avg_ms"] = 0.189400 * 3.0  # +200%, +379µs
    trip_candidate.write_text(json.dumps(trip_candidate_report), encoding="utf-8")
    trip_breach = subprocess.run(
        [sys.executable, str(TOOL), str(trip_baseline), str(trip_candidate),
         "--warn-percent", "20", "--fail-percent", "35"],
        capture_output=True, text=True, check=False)
    if trip_breach.returncode != 2 or "json_update.avg_ms" not in trip_breach.stderr:
        raise SystemExit("genuine 3x json_update regression did not breach the fail gate")

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

    editor_wrong_cpu = directory / "editor-wrong-cpu.json"
    editor_wrong_cpu.write_text(json.dumps(editor_report(cpu_model="Other CPU")), encoding="utf-8")
    editor_cpu_rejected = subprocess.run(
        [sys.executable, str(EDITOR_COMPARE_TOOL), str(editor_baseline), str(editor_wrong_cpu)],
        capture_output=True, text=True, check=False)
    if editor_cpu_rejected.returncode == 0 or "environment.cpu_model" not in editor_cpu_rejected.stderr:
        raise SystemExit("different editor benchmark CPU models were not rejected")

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
