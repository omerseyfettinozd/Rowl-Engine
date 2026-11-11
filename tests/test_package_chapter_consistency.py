#!/usr/bin/env python3
"""D06-3 (discovery F8): split chapter files vs the full story graph at pack time.

The editor's "split into chapters" command writes json/chapters/ next to
json/full_story_graph.json. The player plays the full graph; the chapter
files only feed prefetch, and the engine ignores them when they disagree with
the graph. tools/package_assets.py therefore says so BEFORE the package is
written: [Packer][WARN][chapter-drift] (and chapter-unreadable /
chapter-index-missing). The package is still produced - the chapter files stay
in it for prefetch - and a consistent split is silent.
"""

import contextlib
import io
import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_assets as packager  # noqa: E402

PACKER = ROOT / "tools" / "package_assets.py"
FAILURES = []


def check(name, condition, detail=""):
    if condition:
        print(f"  [PASS] {name}")
    else:
        print(f"  [FAIL] {name} {detail}")
        FAILURES.append(name)


def node(node_id, chapter):
    return {"id": node_id, "chapter_id": chapter, "objects": [], "next_nodes": []}


def write_project(root, graph_nodes, chapter_nodes, index=True, extra_files=None):
    assets = pathlib.Path(root) / "Assets"
    (assets / "json" / "chapters").mkdir(parents=True)
    (assets / "json" / "full_story_graph.json").write_text(json.dumps({
        "format_version": 5, "start_node_id": 1, "nodes": graph_nodes,
        "chapters": [{"id": "ch1"}, {"id": "ch2"}]}), encoding="utf-8")
    if index:
        (assets / "json" / "chapters" / "chapter_index.json").write_text(json.dumps({
            "format_version": 5, "chapters": [{"id": "ch1"}, {"id": "ch2"}]}), encoding="utf-8")
    for chapter, nodes in chapter_nodes.items():
        (assets / "json" / "chapters" / f"{chapter}.json").write_text(json.dumps({
            "format_version": 5, "chapter_id": chapter, "nodes": nodes}), encoding="utf-8")
    for name, text in (extra_files or {}).items():
        (assets / "json" / "chapters" / name).write_text(text, encoding="utf-8")
    return assets


def run_check(assets):
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        codes = packager.check_chapter_consistency(str(assets))
    return codes, err.getvalue()


def main():
    full = [node(1, "ch1"), node(2, "ch1"), node(3, "ch2")]

    with tempfile.TemporaryDirectory(prefix="rowl-chapters-") as tmp:
        # No split at all: silent.
        plain = pathlib.Path(tmp) / "plain" / "Assets"
        (plain / "json").mkdir(parents=True)
        (plain / "json" / "full_story_graph.json").write_text(
            json.dumps({"format_version": 4, "nodes": full}), encoding="utf-8")
        codes, text = run_check(plain)
        check("no chapters folder: silent", codes == [] and text == "", text)

        # A consistent split: silent.
        good = write_project(pathlib.Path(tmp) / "good", full,
                             {"ch1": full[:2], "ch2": full[2:]})
        codes, text = run_check(good)
        check("consistent split: silent", codes == [] and text == "", text)

        # Graph gained a node after the split.
        stale = write_project(pathlib.Path(tmp) / "stale", full + [node(4, "ch2")],
                              {"ch1": full[:2], "ch2": full[2:]})
        codes, text = run_check(stale)
        check("graph node missing from the chapter files: chapter-drift",
              codes == ["chapter-drift"] and "[Packer][WARN][chapter-drift]" in text
              and "1 graph node(s) missing" in text and "(4)" in text, text)

        # A chapter file kept a node the graph dropped.
        extra = write_project(pathlib.Path(tmp) / "extra", full,
                              {"ch1": full[:2], "ch2": full[2:] + [node(9, "ch2")]})
        codes, text = run_check(extra)
        check("chapter-file node not in the graph: chapter-drift",
              codes == ["chapter-drift"] and "1 chapter-file node(s) not in the graph" in text, text)

        # Chapter files without their index.
        no_index = write_project(pathlib.Path(tmp) / "noindex", full,
                                 {"ch1": full[:2], "ch2": full[2:]}, index=False)
        codes, text = run_check(no_index)
        check("chapter files without chapter_index.json: chapter-index-missing",
              codes == ["chapter-index-missing"], text)

        # An unreadable chapter file.
        broken = write_project(pathlib.Path(tmp) / "broken", full,
                               {"ch1": full[:2]}, extra_files={"ch2.json": "{not json"})
        codes, text = run_check(broken)
        check("unreadable chapter file: chapter-unreadable (and the missing nodes drift)",
              "chapter-unreadable" in codes and "chapter-drift" in codes, text)

        # The CLI still writes the package for a drifted split, warnings on stderr,
        # chapter files included (they stay for prefetch).
        package = pathlib.Path(tmp) / "stale.rowlpkg"
        result = subprocess.run([sys.executable, str(PACKER), str(stale), str(package)],
                                capture_output=True, text=True, check=False)
        check("a drifted split still packs (exit 0) and warns on stderr",
              result.returncode == 0 and package.is_file()
              and "[Packer][WARN][chapter-drift]" in result.stderr,
              f"exit {result.returncode}: {result.stderr}")
        check("the chapter files stay in the package for prefetch",
              b"json/chapters/chapter_index.json" in package.read_bytes()
              if package.is_file() else False)

    if FAILURES:
        print(f"FAILED: {', '.join(FAILURES)}", file=sys.stderr)
        return 1
    print("[ChapterConsistency] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
