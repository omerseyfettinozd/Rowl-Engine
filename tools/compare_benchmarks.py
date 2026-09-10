#!/usr/bin/env python3
"""Compare two compatible Rowl native benchmark JSON reports."""

import argparse
import json
import sys


METRICS = {
    "first_frame_ms": ("metrics", "first_frame_ms"),
    "steady_frame_ms": ("metrics", "steady_frame_ms"),
    "vfs_io.avg_ms": ("metrics", "vfs_io", "avg_ms"),
    "json_update.avg_ms": ("metrics", "json_update", "avg_ms"),
    "process_memory_bytes": ("metrics", "process_memory_bytes"),
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
        value = value[part]
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
    for name, path in METRICS.items():
        before = value_at(baseline, path)
        after = value_at(candidate, path)
        if before == 0:
            raise ValueError("cannot calculate percentage from zero baseline: " + name)
        rows.append({"metric": name, "baseline": before, "candidate": after,
                     "delta_percent": ((after - before) / before) * 100.0})
    return {"compatible": True, "environment": base_key, "metrics": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--output", help="write the machine-readable comparison JSON here")
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
