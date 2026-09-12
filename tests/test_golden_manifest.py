#!/usr/bin/env python3
"""Contract tests for Golden Project manifest checksum validation.

The fixture checksums are canonical LF: a Windows CRLF checkout must still
verify, while genuinely changed content must still be rejected.
"""

import hashlib
import importlib.util
import json
import pathlib
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "rowl_test_demo_packaged", ROOT / "tests" / "test_demo_packaged.py"
)
PACKAGED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGED)

LF_PROJ = b'{\n  "name": "CRLF probe"\n}\n'
LF_GRAPH = b'{"nodes": []}\n'


def write_fixture(directory, proj_bytes, graph_bytes):
    sample = pathlib.Path(directory)
    (sample / "Assets" / "json").mkdir(parents=True)
    (sample / "project.rowlproj").write_bytes(proj_bytes)
    (sample / "Assets" / "json" / "full_story_graph.json").write_bytes(graph_bytes)
    manifest = {
        "schema_version": 1,
        "fixture_id": "rowl-golden-manifest-probe",
        "sha256": {
            "project.rowlproj": hashlib.sha256(LF_PROJ).hexdigest(),
            "Assets/json/full_story_graph.json": hashlib.sha256(LF_GRAPH).hexdigest(),
        },
    }
    (sample / "golden_project.json").write_text(json.dumps(manifest), encoding="utf-8")
    return sample


def expect_ok(sample):
    PACKAGED.validate_golden_manifest(sample)


def expect_rejected(sample):
    try:
        PACKAGED.validate_golden_manifest(sample)
    except RuntimeError:
        return
    raise SystemExit(f"CRLF probe accepted drifted fixture in {sample}")


with tempfile.TemporaryDirectory() as directory:
    expect_ok(write_fixture(pathlib.Path(directory) / "lf", LF_PROJ, LF_GRAPH))

    crlf_proj = LF_PROJ.replace(b"\n", b"\r\n")
    crlf_graph = LF_GRAPH.replace(b"\n", b"\r\n")
    expect_ok(write_fixture(pathlib.Path(directory) / "crlf", crlf_proj, crlf_graph))

    expect_rejected(
        write_fixture(
            pathlib.Path(directory) / "drifted",
            LF_PROJ.replace(b"probe", b"changed"),
            LF_GRAPH,
        )
    )

print("Golden manifest checksum tests passed.")
