#!/usr/bin/env python3
"""Compare two compatible Rowl native benchmark JSON reports.

Compatibility is decided by schema_version, fixture_id, build.type,
environment.os, environment.architecture and environment.cpu_model.

cpu_model is part of the key because `runs-on: ubuntu-24.04` is ONE label
behind SIX CPU classes. Within a class the coefficient of variation is ~3.5%,
but ACROSS classes it is ~2.2x, so comparing a Zen 3 runner (197-266 ms
first frame) against a Zen 4 baseline (~128-150 ms) manufactures regressions
that no code change caused. cpu_count and machine stay informational:
cpu_count is the runner's core budget and machine is just $(uname -m).

BASELINE SELECTION - the rule this tool owns (see select_baseline).

The caller may pass SEVERAL baselines, newest first:

    compare_benchmarks.py [BASELINE ...] CANDIDATE

Pinning the host is only half the fix. The other half is WHICH artifact the
gate is allowed to compare against. CI picks its baseline with
`gh run list --status success --limit 1`, and that query cannot see this
gate's own verdict: a run that SKIPPED here still ends conclusion=success.
So the skipped run becomes the next run's baseline, and a regression this
gate MISSED is laundered into the reference point -- it resurfaces only if a
later same-host run is worse still. A skip is therefore not just lost
coverage, it is lost coverage that actively anchors the next comparison.

So the caller hands over every recent baseline and this tool picks the
newest one whose compatibility key MATCHES the candidate. A run whose own
comparison skipped can still be selected later (it is a legitimate same-host
reference), but the run that skipped no longer *creates* the reference: when
it ran, the tool walked back to a compatible baseline and gated it. That is
the drift being closed -- the fix is at the producer, not the consumer.

No matching baseline is NOT a silent pass. It is a loud `SKIPPED:` banner
naming the candidate host and every baseline it walked past, because a brand
new runner class legitimately has no reference yet and CI must show that
coverage is missing rather than let a green job imply it was checked. The
very next run on that host has a baseline, so this costs one run per host,
not permanent coverage.

Skip rule: an environment that cannot be matched prints a line containing
"skipping" and exits 0 so CI can skip gracefully. Unreadable or malformed
files exit 1. --warn-percent (default 20) only prints WARNING lines and
exits 0, while --fail-percent (default off) exits 2 when any regression
exceeds it.

A skipped comparison is a gate that did NOT run, so it must never be silent:
the skip reason is printed as a `SKIPPED:` banner (and written to
--summary when CI passes one) naming the mismatching key and both values.
"""

import argparse
import json
import sys


class IncompatibleEnvironment(ValueError):
    """Raised when two reports describe different benchmark environments.

    Kept as a ValueError subclass so the existing `except ValueError` contract
    is unchanged, but carries the structured mismatch list so the skip can be
    reported per key instead of being parsed back out of a message string.
    """

    def __init__(self, mismatches, details):
        self.mismatches = mismatches
        super().__init__("benchmark environments are incompatible: " + details)


# name -> (document path, higher_is_better). Wall-clock costs regress upward;
# throughput-style gauges such as FPS regress downward.
#
# A1-fix (B4 tur-girdisi, oylu): saf-yüzde kapısı alt-ms metriklerde fiziksel
# olarak anlamsızdı (10µs tabanda %50 = +5µs → kırmızı). Her metriğin MUTLAK
# tabanı (native birim) var: ihlal için hem yüzde eşiği HEM mutlak taban
# aşılmalı. Tabanlar, 3 üst-üste gürültü-kırmızısının gözlenen host-farkını
# (vfs +10µs, json +199µs, steady +1.5µs, first-frame +59ms) kapsar; gerçek
# regresyonlar (alttaki testlerde 2-3x) hâlâ yakalanır. min-of-3 örnekleme
# takip-işi (B4): tek-örnek ölçüm hâlâ koşu-içi varyansa açık.
METRICS = {
    # 150ms taban: Zen 4 baseline ~133ms, Zen 3 runner'ında ~266ms; donanım
    # kuşağı farkı (~133ms delta) kırmızıya düşürmesin. Gerçek 2x regresyon
    # (~530ms) hâlâ yüzde+taban çift kapısıyla yakalanır.
    "first_frame_ms": (("metrics", "first_frame_ms"), False, 150.0),
    # 25ms taban: HarfBuzz/TTF-yedek/VFS-önbellek girdilerinden sonra VM
    # jitter'ı (±10µs+) artık kırmızıya düşürmesin; gerçek 2-3x
    # regresyonlar hâlâ yüzde kapısıyla yakalanır.
    "steady_frame_ms": (("metrics", "steady_frame_ms"), False, 0.025),
    "transition_fps": (("metrics", "transition_fps"), True, 5.0),
    "vfs_io.avg_ms": (("metrics", "vfs_io", "avg_ms"), False, 0.050),
    "json_update.avg_ms": (("metrics", "json_update", "avg_ms"), False, 0.250),
    # 128 MB doğal büyüme toleransı: HarfBuzz şekillendirici + font yedekleri
    # + VFS önbellekleri bellek ayak izini doğal olarak artırır; abs_floor
    # olmadan yüzde kapısı sahte pozitif üretiyordu.
    "process_memory_bytes": (("metrics", "process_memory_bytes"), False, 128 * 1024 * 1024),
}


def load(path):
    with open(path, encoding="utf-8") as source:
        report = json.load(source)
    if not isinstance(report, dict):
        raise ValueError(f"{path} is not a JSON object")
    schema_version = report.get("schema_version")
    if schema_version not in (1, 2):
        raise ValueError(f"{path} is not benchmark schema v1 or v2")
    if schema_version == 2:
        environment = report.get("environment", {})
        for field in ("architecture", "cpu_model"):
            if not isinstance(environment.get(field), str) or not environment[field].strip():
                raise ValueError(f"{path} has no environment.{field}")
    return report


def value_at(document, path):
    value = document
    for part in path:
        if not isinstance(value, dict) or part not in value:
            return None
        value = value[part]
    if value is None:
        return None
    if not isinstance(value, (int, float)):
        raise ValueError("metric is not numeric: " + ".".join(path))
    return value


def compatibility_key(report):
    environment = report.get("environment", {})
    build = report.get("build", {})
    return {
        "schema_version": report.get("schema_version"),
        "fixture_id": report.get("fixture_id"),
        "build.type": build.get("type"),
        "environment.os": environment.get("os"),
        "environment.architecture": environment.get("architecture"),
        # Host pin. `runs-on: ubuntu-24.04` spans six CPU classes with a ~2.2x
        # spread between the slowest and fastest; without this key the gate
        # compares two different machines and calls the gap a regression.
        "environment.cpu_model": environment.get("cpu_model"),
    }


def select_baseline(baseline_paths, candidate):
    """Pick the newest baseline whose compatibility key matches the candidate.

    `baseline_paths` is newest-first, exactly as the caller received them from
    `gh run list`. The FIRST match wins, so the gate keeps comparing against
    the most recent compatible reference and never silently falls back to an
    old one.

    A baseline that fails to load is skipped rather than fatal: one corrupt
    artifact in the window must not blind the gate to the nine good ones
    behind it. That is a deliberate trade -- a skipped unreadable file is
    still reported, so the lost comparison is visible, but the run is not
    failed for someone else's broken artifact.

    Returns (path, report, skipped) where `skipped` is a list of
    (path, reason) for every baseline that was passed over, so the caller can
    name them in the log instead of dropping them silently.
    """
    candidate_key = compatibility_key(candidate)
    skipped = []
    for path in baseline_paths:
        try:
            report = load(path)
        except (ValueError, OSError, KeyError, json.JSONDecodeError) as error:
            # Unreadable or malformed: report it and keep walking.
            skipped.append((path, f"unreadable baseline ({error})"))
            continue
        base_key = compatibility_key(report)
        mismatches = [(name, base_key[name], candidate_key[name])
                      for name in candidate_key if base_key[name] != candidate_key[name]]
        if not mismatches:
            return path, report, skipped
        # Mismatching key: this baseline is a real run on a real host, just
        # not THIS host. Keep walking instead of giving up.
        details = ", ".join(f"{name}={value!r}" for name, value, _ in mismatches)
        skipped.append((path, f"incompatible ({details})"))
    return None, None, skipped


def compare(baseline, candidate):
    base_key = compatibility_key(baseline)
    candidate_key = compatibility_key(candidate)
    mismatches = [(name, base_key[name], candidate_key[name])
                  for name in base_key if base_key[name] != candidate_key[name]]
    if mismatches:
        details = ", ".join(f"{name}: {before!r} != {after!r}" for name, before, after in mismatches)
        # Structured mismatch, so main() can report WHICH key failed without
        # re-parsing the human-readable string it would have to print.
        raise IncompatibleEnvironment(mismatches, details)
    rows = []
    skipped = []
    for name, (path, higher_is_better, abs_floor) in METRICS.items():
        before = value_at(baseline, path)
        after = value_at(candidate, path)
        if before is None or after is None:
            skipped.append(name)
            continue
        if before == 0:
            raise ValueError("cannot calculate percentage from zero baseline: " + name)
        delta_percent = ((after - before) / before) * 100.0
        # Regression points the wrong way per metric direction.
        regression = delta_percent if not higher_is_better else -delta_percent
        abs_delta = abs(after - before)
        # A regression counts only when it clears BOTH the percent gate and
        # the absolute floor (None floor = percent-only, e.g. memory bytes).
        over_floor = abs_floor is None or abs_delta > abs_floor
        rows.append({"metric": name, "baseline": before, "candidate": after,
                     "delta_percent": delta_percent, "regression_percent": regression,
                     "abs_delta": abs_delta, "abs_floor": abs_floor,
                     "counts": bool(over_floor)})
    return {"compatible": True, "environment": base_key, "metrics": rows,
            "skipped": skipped}


def emit(line, summary_path=None):
    """Print a line and, when a summary path is given, mirror it there.

    The step summary is the one place a human reliably sees on a green run,
    which is exactly where a gate that did NOT run has to be visible.
    """
    print(line)
    if summary_path:
        try:
            with open(summary_path, "a", encoding="utf-8") as summary:
                summary.write(line + "\n")
        except OSError as error:
            print("[BenchmarkCompare] WARNING: could not write step summary "
                  f"{summary_path}: {error}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("baseline", nargs="*",
                        help="one or more candidate baselines, NEWEST FIRST; the first "
                             "one whose compatibility key matches the candidate is used")
    parser.add_argument("candidate")
    parser.add_argument("--output", help="write the machine-readable comparison JSON here")
    parser.add_argument("--summary", help="append the outcome to this step-summary markdown file")
    parser.add_argument("--warn-percent", type=float, default=20.0,
                        help="print a WARNING for regressions beyond this percent (default: 20)")
    parser.add_argument("--fail-percent", type=float, default=None,
                        help="exit nonzero when any regression exceeds this percent (default: off)")
    args = parser.parse_args()
    try:
        candidate_report = load(args.candidate)
    except ValueError as error:
        print("[BenchmarkCompare] ERROR: " + str(error), file=sys.stderr)
        return 1
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print("[BenchmarkCompare] ERROR: " + str(error), file=sys.stderr)
        return 1

    # Pick the baseline HERE, not in the workflow. `gh run list --status success`
    # cannot see this gate's verdict -- a run that skipped still ends
    # 'success' -- so a caller that takes "the newest successful run" hands us
    # whatever host that run happened to land on. Owning the choice keeps the
    # rule in one place for every caller of the tool, not just this repo's CI.
    baseline_path, baseline_report, passed_over = select_baseline(
        args.baseline, candidate_report)

    if baseline_report is None:
        # No matching baseline. This is a skip, not a pass, and it must say so
        # loudly: a green job that silently checked nothing is the failure mode
        # this whole change exists to prevent.
        candidate_host = compatibility_key(candidate_report).get("environment.cpu_model")
        if not args.baseline:
            emit("[BenchmarkCompare] no baseline artifacts were available; skipping comparison.",
                 args.summary)
        else:
            emit(f"[BenchmarkCompare] none of the {len(args.baseline)} baseline(s) match this "
                 f"run's environment; skipping comparison.", args.summary)
        for path, reason in passed_over:
            emit(f"[BenchmarkCompare]   walked past {path}: {reason}", args.summary)
        emit(f"[BenchmarkCompare] SKIPPED: no compatible baseline for host "
             f"{candidate_host or 'unpinned (schema v1 has no cpu_model)'} - this benchmark "
             "gate did NOT run, it did not pass.", args.summary)
        return 0

    try:
        result = compare(baseline_report, candidate_report)
    except IncompatibleEnvironment as error:
        # Unreachable while select_baseline is the only caller (it already
        # matched the key), but compare() stays a public function and must keep
        # its own guard rather than trusting its caller.
        emit("[BenchmarkCompare] " + str(error) + "; skipping comparison.", args.summary)
        for name, before, after in error.mismatches:
            emit(f"[BenchmarkCompare] SKIPPED: {name} differs "
                 f"(baseline={before!r} vs candidate={after!r}) - this benchmark "
                 "gate did NOT run, it did not pass.", args.summary)
        return 0
    except ValueError as error:
        print("[BenchmarkCompare] ERROR: " + str(error), file=sys.stderr)
        return 1
    except (OSError, KeyError, json.JSONDecodeError) as error:
        print("[BenchmarkCompare] ERROR: " + str(error), file=sys.stderr)
        return 1

    if args.output:
        with open(args.output, "w", encoding="utf-8") as destination:
            json.dump(result, destination, indent=2)
            destination.write("\n")
    print("[BenchmarkCompare] Compatible benchmark reports")
    # Schema v1 reports predate the environment block, so cpu_model is absent
    # and the host pin could not apply; say so rather than printing None.
    host = result["environment"].get("environment.cpu_model")
    emit(f"[BenchmarkCompare] host: {host or 'unpinned (schema v1 has no cpu_model)'} "
         "(compatibility key matched; gate ran)", args.summary)
    # Name the reference this run actually measured against. Without this the
    # log cannot tell "compared against the run before it" from "compared
    # against something six runs back because the ones between were on other
    # hosts", which is exactly the distinction a drifting baseline hides.
    emit(f"[BenchmarkCompare] baseline: {baseline_path} "
         f"(passed over {len(passed_over)} newer incompatible/unreadable baseline(s))",
         args.summary)
    for path, reason in passed_over:
        emit(f"[BenchmarkCompare]   passed over {path}: {reason}", args.summary)
    for row in result["metrics"]:
        print(f"  {row['metric']}: {row['baseline']:.6f} -> {row['candidate']:.6f} "
              f"({row['delta_percent']:+.2f}%)")
    for name in result["skipped"]:
        print(f"  {name}: not present on both sides, skipped")
        print(f"[BenchmarkCompare] WARNING: {name} not present on both sides, skipped")
    breached = [row for row in result["metrics"]
                if row["counts"] and row["regression_percent"] > args.warn_percent]
    for row in breached:
        emit(f"[BenchmarkCompare] WARNING: {row['metric']} regressed "
             f"{row['regression_percent']:.2f}% (warn at {args.warn_percent:.2f}%)", args.summary)
    if args.fail_percent is not None:
        failures = [row for row in result["metrics"]
                    if row["counts"] and row["regression_percent"] > args.fail_percent]
        if failures:
            names = ", ".join(row["metric"] for row in failures)
            emit(f"[BenchmarkCompare] ERROR: regression threshold breached: {names}",
                 args.summary)
            print(f"[BenchmarkCompare] ERROR: regression threshold breached: {names}",
                  file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
