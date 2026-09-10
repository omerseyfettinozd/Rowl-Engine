#!/usr/bin/env python3
"""Summarize compatible Rowl editor interaction benchmark reports."""

import argparse
import json
import statistics
import sys


def load(path):
    with open(path, encoding="utf-8") as source:
        report = json.load(source)
    if report.get("schema_version") != 1 or not isinstance(report.get("metrics"), dict):
        raise ValueError(f"{path} is not editor benchmark schema v1")
    return report


def key(report):
    return (report.get("fixture_id"), report.get("build", {}).get("type"),
            report.get("environment", {}).get("os"), report.get("environment", {}).get("machine"),
            report.get("environment", {}).get("cpu_count"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", help="at least two report paths")
    args = parser.parse_args()
    try:
        if len(args.reports) < 2:
            raise ValueError("at least two reports are required")
        reports = [load(path) for path in args.reports]
        if any(key(report) != key(reports[0]) for report in reports[1:]):
            raise ValueError("benchmark environments are incompatible")
        names = set(reports[0]["metrics"])
        if any(set(report["metrics"]) != names for report in reports[1:]):
            raise ValueError("benchmark metric sets are incompatible")
        print("[EditorBenchmarkSummary] Compatible benchmark reports")
        print(f"  samples: {len(reports)}")
        for name in sorted(names):
            values = [report["metrics"][name] for report in reports]
            if any(not isinstance(value, (int, float)) or value < 0 for value in values):
                raise ValueError("metric must be a non-negative number: " + name)
            print(f"  {name}: min {min(values):.6f} ms, median {statistics.median(values):.6f} ms, max {max(values):.6f} ms")
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print("[EditorBenchmarkSummary] ERROR: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
