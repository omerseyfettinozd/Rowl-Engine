#!/usr/bin/env python3
"""Generate the deterministic Phase-0 editor scale story graph fixture."""

import argparse
import json
from pathlib import Path


NODE_COUNT = 2_000
EDGES_PER_NODE = 3
FIXTURE_ID = "editor-scale-2000n-6000e-v1"


def build_fixture():
    nodes = []
    columns = 50
    for index in range(NODE_COUNT):
        node_id = index + 1
        next_nodes = []
        for offset in range(1, EDGES_PER_NODE + 1):
            target_id = ((index + offset) % NODE_COUNT) + 1
            next_nodes.append({
                "id": target_id,
                "label": f"Route {offset}",
                "option_id": f"scale_{node_id}_{offset}",
            })
        nodes.append({
            "id": node_id,
            "title": f"Scale Node {node_id:04d}",
            "x": (index % columns) * 360,
            "y": (index // columns) * 220,
            "objects": [{
                "id": f"dialogue_object_{node_id}",
                "name": "Dialogue",
                "is_active": True,
                "components": [{
                    "type": "dialogue",
                    "id": f"dialogue_{node_id}",
                    "enabled": True,
                    "data": {
                        "speaker": f"Speaker {node_id % 20:02d}",
                        "dialogue": f"Deterministic scale fixture line {node_id:04d}.",
                    },
                }],
            }],
            "next_nodes": next_nodes,
        })
    return {
        "format_version": 4,
        "fixture_id": FIXTURE_ID,
        "start_node_id": 1,
        "nodes": nodes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(build_fixture(), ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
