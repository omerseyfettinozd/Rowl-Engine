#!/usr/bin/env python3
"""Compare two compatible Rowl editor interaction benchmark JSON reports."""

import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as source:
        document = json.load(source)
    if document.get("schema_version") != 1:
        raise ValueError(f"{path} is not editor benchmark schema v1")
    if not isinstance(document.get("metrics"), dict):
        raise ValueError(f"{path} has no metrics object")
    return document


def compatibility_key(report):
    return {
        "fixture_id": report.get("fixture_id"),
        "build.type": report.get("build", {}).get("type"),
        "environment.os": report.get("environment", {}).get("os"),
        "environment.machine": report.get("environment", {}).get("machine"),
        "environment.cpu_count": report.get("environment", {}).get("cpu_count"),
    }


def compare(baseline, candidate):
    before, after = compatibility_key(baseline), compatibility_key(candidate)
    mismatches = [key for key in before if before[key] != after[key]]
    if mismatches:
        raise ValueError("benchmark environments are incompatible: " + ", ".join(mismatches))
    if set(baseline["metrics"]) != set(candidate["metrics"]):
        raise ValueError("benchmark metric sets are incompatible")

    metrics = []
    for name in sorted(baseline["metrics"]):
        old, new = baseline["metrics"][name], candidate["metrics"][name]
        if not isinstance(old, (int, float)) or not isinstance(new, (int, float)) or old < 0 or new < 0:
            raise ValueError("metric must be a non-negative number: " + name)
        metrics.append({
            "metric": name,
            "baseline": old,
            "candidate": new,
            "delta_percent": None if old == 0 else (new - old) / old * 100.0,
        })
    return {"compatible": True, "environment": before, "metrics": metrics}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--output", help="write the machine-readable comparison JSON here")
    args = parser.parse_args()
    try:
        baseline, candidate = load(args.baseline), load(args.candidate)
        result = compare(baseline, candidate)
        if args.output:
            with open(args.output, "w", encoding="utf-8") as destination:
                json.dump(result, destination, indent=2)
                destination.write("\n")
        print("[EditorBenchmarkCompare] Compatible benchmark reports")
        for metric in result["metrics"]:
            old, new = metric["baseline"], metric["candidate"]
            if metric["delta_percent"] is None:
                print(f"  {metric['metric']}: {old:.6f} -> {new:.6f} (n/a from zero baseline)")
            else:
                print(f"  {metric['metric']}: {old:.6f} -> {new:.6f} ({metric['delta_percent']:+.2f}%)")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print("[EditorBenchmarkCompare] ERROR: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
