#!/usr/bin/env python3
"""Executable-doc gate for docs/AUTHOR_GUIDE.md (Faz 6 Dilim 6).

The guide's every ```sh block is parsed and REALLY executed top-to-bottom
(source -> converters -> provenance -> pack -> raw-source gate ->
portable-zip -> .sh -> install -> verify -> uninstall). Each block's
first line must be a `# guide-probe: expect-ok|expect-fail` tag: untagged
blocks fail loudly, so a stale/undocumented command is impossible -- a
changed guide command changes what runs, and a changed flow contradicts
the guide.

Production code is only CALLED (converters, package_assets, export_game),
never modified.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUIDE = ROOT / "docs" / "AUTHOR_GUIDE.md"

TAG_RE = re.compile(r"^#\s*guide-probe:\s*(expect-ok|expect-fail)\s*$")
FENCE_RE = re.compile(r"^```(\w*)\s*$")


def run(arguments, **kwargs):
    return subprocess.run(arguments, capture_output=True, text=True,
                          check=False, **kwargs)


def require_tools(*names):
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise SystemExit(f"missing host tools for author-guide test: {missing}")


def resolve_converter(env_name, default_rel):
    import os
    candidate = pathlib.Path(os.environ.get(env_name, ROOT / default_rel))
    if not candidate.is_file():
        raise SystemExit(
            f"converter binary missing: {candidate} "
            f"(build it, or set {env_name})")
    return candidate


def extract_sh_blocks(text):
    blocks = []
    current = None
    for line in text.splitlines():
        match = FENCE_RE.match(line)
        if match:
            if current is None:
                if (match.group(1) or "text") == "sh":
                    current = []
            else:
                blocks.append("\n".join(current))
                current = None
            continue
        if current is not None:
            current.append(line)
    if current is not None:
        raise SystemExit("AUTHOR_GUIDE.md has an unterminated ```sh fence")
    return blocks


def main():
    require_tools("sh", "sha256sum", "unzip", "base64", "tail",
                  "python3", "ffmpeg")
    # Converter binaries are the MediaConverterService override names
    # (editor/Services/MediaConverterService.cs); defaults are the
    # build-tree host tools.
    resolve_converter("ROWL_OGGENC_PATH",
                      pathlib.Path("build") / "bin" / "rowl_oggenc")
    resolve_converter("ROWL_WEBP2PNG_PATH",
                      pathlib.Path("build") / "bin" / "rowl_webp2png")

    guide_text = GUIDE.read_text(encoding="utf-8")
    blocks = extract_sh_blocks(guide_text)
    if len(blocks) < 8:
        raise SystemExit(
            f"AUTHOR_GUIDE.md has only {len(blocks)} sh blocks, need >= 8 "
            "so the full convert/provenance/pack/publish flow stays executable")

    probed = []
    for index, block in enumerate(blocks):
        lines = [line for line in block.splitlines() if line.strip()]
        if not lines:
            raise SystemExit(f"sh block {index} is empty")
        tag = TAG_RE.match(lines[0])
        if not tag:
            raise SystemExit(
                f"sh block {index} lacks a `# guide-probe:` tag as its "
                "first line (stale-format guard)")
        probed.append((tag.group(1), block))

    if not any(mode == "expect-fail" for mode, _ in probed):
        raise SystemExit("no expect-fail probe: the failure-path coverage is gone")
    if "uninstall" not in guide_text:
        raise SystemExit("guide lost its uninstall coverage")

    with tempfile.TemporaryDirectory(prefix="rowl-author-guide-") as work:
        import os
        env = dict(os.environ, AUTHOR_WORK=work)
        # Blocks run with cwd=ROOT (guide commands use repo-relative paths).
        # Snapshot crash-logs/ and remove ONLY what this run created --
        # never touch pre-existing files, never leave litter behind
        # (same pattern as test_player_guide.py; this flow creates none,
        # but the guard stays so a future block cannot litter silently).
        crash_dir = ROOT / "crash-logs"
        before = (set(crash_dir.iterdir()) if crash_dir.is_dir() else None)
        try:
            for index, (mode, block) in enumerate(probed):
                proc = run(["sh", "-c", block], cwd=ROOT, env=env)
                ok = proc.returncode == 0 if mode == "expect-ok" \
                    else proc.returncode != 0
                if not ok:
                    raise SystemExit(
                        f"guide sh block {index} ({mode}) failed: "
                        f"exit={proc.returncode}\n"
                        f"--- block ---\n{block}\n"
                        f"--- stdout ---\n{proc.stdout}\n"
                        f"--- stderr ---\n{proc.stderr}")
                print(f"[AuthorGuide] block {index} ({mode}): green.")
        finally:
            if crash_dir.is_dir():
                before_names = ({p.name for p in before}
                                if before is not None else set())
                for child in crash_dir.iterdir():
                    if (child.name not in before_names
                            and (child.is_file() or child.is_symlink())):
                        child.unlink()
                if not before_names:
                    try:
                        crash_dir.rmdir()
                    except OSError:
                        pass

    print("[AuthorGuide] guide blocks executed: green.")


if __name__ == "__main__":
    main()
