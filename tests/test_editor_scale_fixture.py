#!/usr/bin/env python3
"""Contract test for the deterministic 2,000-node editor scale fixture."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_editor_scale_fixture.py"
CANONICAL = ROOT / "editor" / "Tests" / "Fixtures" / "editor_scale_2000.json"


with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    first = directory / "first.json"
    second = directory / "second.json"
    for output in (first, second):
        result = subprocess.run(
            [sys.executable, str(GENERATOR), str(output)],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            raise SystemExit("scale fixture generation failed: " + result.stderr)

    if hashlib.sha256(first.read_bytes()).digest() != hashlib.sha256(second.read_bytes()).digest():
        raise SystemExit("scale fixture generation is not deterministic")
    if not CANONICAL.exists() or hashlib.sha256(first.read_bytes()).digest() != hashlib.sha256(CANONICAL.read_bytes()).digest():
        raise SystemExit("canonical scale fixture is stale; regenerate it with the fixture generator")

    document = json.loads(first.read_text(encoding="utf-8"))
    nodes = document.get("nodes", [])
    if document.get("format_version") != 4 or document.get("fixture_id") != "editor-scale-2000n-6000e-v1":
        raise SystemExit("scale fixture identity or format is invalid")
    if len(nodes) != 2_000 or sum(len(node.get("next_nodes", [])) for node in nodes) != 6_000:
        raise SystemExit("scale fixture must contain exactly 2,000 nodes and 6,000 edges")

    node_ids = {node.get("id") for node in nodes}
    option_ids = []
    for node in nodes:
        if not node.get("objects") or not node["objects"][0].get("components"):
            raise SystemExit("every scale node must contain an editable dialogue component")
        for edge in node["next_nodes"]:
            if edge.get("id") not in node_ids:
                raise SystemExit("scale fixture contains a missing edge target")
            option_ids.append(edge.get("option_id"))
    if len(option_ids) != len(set(option_ids)):
        raise SystemExit("scale fixture option IDs must be unique")
