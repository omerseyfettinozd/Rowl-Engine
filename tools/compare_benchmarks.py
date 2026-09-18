#!/usr/bin/env python3
"""Compare two compatible Rowl native benchmark JSON reports.

Compatibility is decided by schema_version, fixture_id, build.type,
environment.os and environment.architecture only. cpu_model, cpu_count and
machine are informational and do not affect compatibility (machine is just
$(uname -m), not a host pin).

Skip rule: incompatible environments print a line containing "skipping" and
exit 0 so CI can skip gracefully. Unreadable or malformed files exit 1.
--warn-percent (default 20) only prints WARNING lines and exits 0, while
--fail-percent (default off) exits 2 when any regression exceeds it.
"""

import argparse
import json
import sys


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
    "first_frame_ms": (("metrics", "first_frame_ms"), False, 75.0),
    "steady_frame_ms": (("metrics", "steady_frame_ms"), False, 0.010),
    "transition_fps": (("metrics", "transition_fps"), True, 5.0),
    "vfs_io.avg_ms": (("metrics", "vfs_io", "avg_ms"), False, 0.050),
    "json_update.avg_ms": (("metrics", "json_update", "avg_ms"), False, 0.250),
    "process_memory_bytes": (("metrics", "process_memory_bytes"), False, None),
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
    }


def compare(baseline, candidate):
    base_key = compatibility_key(baseline)
    candidate_key = compatibility_key(candidate)
    mismatches = [name for name in base_key if base_key[name] != candidate_key[name]]
    if mismatches:
        details = ", ".join(f"{name}: {base_key[name]!r} != {candidate_key[name]!r}" for name in mismatches)
        raise ValueError("benchmark environments are incompatible: " + details)

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--output", help="write the machine-readable comparison JSON here")
    parser.add_argument("--warn-percent", type=float, default=20.0,
                        help="print a WARNING for regressions beyond this percent (default: 20)")
    parser.add_argument("--fail-percent", type=float, default=None,
                        help="exit nonzero when any regression exceeds this percent (default: off)")
    args = parser.parse_args()
    try:
        result = compare(load(args.baseline), load(args.candidate))
    except ValueError as error:
        if str(error).startswith("benchmark environments are incompatible"):
            print("[BenchmarkCompare] " + str(error) + "; skipping comparison.")
            return 0
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
    for row in result["metrics"]:
        print(f"  {row['metric']}: {row['baseline']:.6f} -> {row['candidate']:.6f} "
              f"({row['delta_percent']:+.2f}%)")
    for name in result["skipped"]:
        print(f"  {name}: not present on both sides, skipped")
        print(f"[BenchmarkCompare] WARNING: {name} not present on both sides, skipped")
    breached = [row for row in result["metrics"]
                if row["counts"] and row["regression_percent"] > args.warn_percent]
    for row in breached:
        print(f"[BenchmarkCompare] WARNING: {row['metric']} regressed "
              f"{row['regression_percent']:.2f}% (warn at {args.warn_percent:.2f}%)")
    if args.fail_percent is not None:
        failures = [row for row in result["metrics"]
                    if row["counts"] and row["regression_percent"] > args.fail_percent]
        if failures:
            names = ", ".join(row["metric"] for row in failures)
            print(f"[BenchmarkCompare] ERROR: regression threshold breached: {names}",
                  file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
