#!/usr/bin/env python3
"""Compare two compatible Rowl native benchmark JSON reports."""

import argparse
import json
import sys


# name -> (document path, higher_is_better). Wall-clock costs regress upward;
# throughput-style gauges such as FPS regress downward.
METRICS = {
    "first_frame_ms": (("metrics", "first_frame_ms"), False),
    "steady_frame_ms": (("metrics", "steady_frame_ms"), False),
    "transition_fps": (("metrics", "transition_fps"), True),
    "vfs_io.avg_ms": (("metrics", "vfs_io", "avg_ms"), False),
    "json_update.avg_ms": (("metrics", "json_update", "avg_ms"), False),
    "process_memory_bytes": (("metrics", "process_memory_bytes"), False),
}


def load(path):
    with open(path, encoding="utf-8") as source:
        report = json.load(source)
    if report.get("schema_version") != 1:
        raise ValueError(f"{path} is not benchmark schema v1")
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
        "fixture_id": report.get("fixture_id"),
        "build.type": build.get("type"),
        "environment.os": environment.get("os"),
        "environment.cpu_count": environment.get("cpu_count"),
        "environment.machine": environment.get("machine"),
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
    for name, (path, higher_is_better) in METRICS.items():
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
        rows.append({"metric": name, "baseline": before, "candidate": after,
                     "delta_percent": delta_percent, "regression_percent": regression})
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
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
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
    breached = [row for row in result["metrics"]
                if row["regression_percent"] > args.warn_percent]
    for row in breached:
        print(f"[BenchmarkCompare] WARNING: {row['metric']} regressed "
              f"{row['regression_percent']:.2f}% (warn at {args.warn_percent:.2f}%)")
    if args.fail_percent is not None:
        failures = [row for row in result["metrics"]
                    if row["regression_percent"] > args.fail_percent]
        if failures:
            names = ", ".join(row["metric"] for row in failures)
            print(f"[BenchmarkCompare] ERROR: regression threshold breached: {names}",
                  file=sys.stderr)
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
