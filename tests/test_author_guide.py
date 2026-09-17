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

import os
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
    # Windows builds the same binary with a `.exe` suffix; resolve it so
    # the is_file gate and the guide blocks (which read this env var)
    # use the exact path instead of relying on shell suffix magic.
    if (os.name == "nt" and candidate.suffix == ""
            and candidate.with_suffix(".exe").is_file()):
        candidate = candidate.with_suffix(".exe")
    if not candidate.is_file():
        raise SystemExit(
            f"converter binary missing: {candidate} "
            f"(build it, or set {env_name})")
    os.environ[env_name] = str(candidate)
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
    # Tur-8 pre-flight: prove both converter loaders resolve before the
    # guide blocks run. A parked tool fails here naming the binary instead
    # of hanging block 5 into a mystery ctest TIMEOUT (Windows CI tur-7).
    for var in ("ROWL_OGGENC_PATH", "ROWL_WEBP2PNG_PATH"):
        binary = os.environ[var]
        try:
            smoke = subprocess.run([binary, "--version"], capture_output=True,
                                   text=True, timeout=30)
        except subprocess.TimeoutExpired:
            raise SystemExit(
                f"author pre-flight hung: {var}={binary} never exited "
                "(missing DLL? loader dialog?)")
        if smoke.returncode != 0:
            raise SystemExit(
                f"author pre-flight failed: {var}={binary} "
                f"exit={smoke.returncode}")
        print(f"[AuthorGuide] pre-flight {var}={binary}: "
              f"{(smoke.stdout or '').strip() or '(no stdout)'}")

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
                # Tur-8: bound each block so a parked converter fails naming
                # the block instead of burning the ctest TIMEOUT silently.
                try:
                    proc = run(["sh", "-c", block], cwd=ROOT, env=env,
                               timeout=150)
                except subprocess.TimeoutExpired:
                    raise SystemExit(
                        f"guide sh block {index} ({mode}) hung >150s "
                        f"(converter tool parked? missing DLL dialog?)\n"
                        f"--- block ---\n{block}")
                ok = proc.returncode == 0 if mode == "expect-ok" \
                    else proc.returncode != 0
                if not ok:
                    # Tur-7: a bare exit code never names the culprit (e.g.
                    # block 5 failed on Windows with empty stdout/stderr, so
                    # the tool-vs-test -s split was invisible). Dump the
                    # work tree (relpath + size) plus tool identities so the
                    # next red run shows WHAT is missing, not just THAT.
                    tree_lines = []
                    for path in sorted(pathlib.Path(work).rglob("*")):
                        try:
                            rel = path.relative_to(work)
                            size = path.stat().st_size if path.is_file() else -1
                            tree_lines.append(f"{rel} ({size}b)")
                        except OSError:
                            tree_lines.append(f"{path} (stat-failed)")
                    tool_lines = []
                    for var in ("ROWL_OGGENC_PATH", "ROWL_WEBP2PNG_PATH"):
                        binary = env.get(var, "")
                        ver = run([binary, "--version"]) if binary else None
                        tool_lines.append(
                            f"{var}={binary} "
                            f"exists={pathlib.Path(binary).is_file() if binary else False} "
                            f"version={(ver.stdout.strip() + ver.stderr.strip()) if ver is not None else 'n/a'!r} "
                            f"rc={ver.returncode if ver is not None else 'n/a'}")
                    raise SystemExit(
                        f"guide sh block {index} ({mode}) failed: "
                        f"exit={proc.returncode}\n"
                        f"--- block ---\n{block}\n"
                        f"--- stdout ---\n{proc.stdout}\n"
                        f"--- stderr ---\n{proc.stderr}\n"
                        f"--- AUTHOR_WORK tree ---\n" + "\n".join(tree_lines) + "\n"
                        f"--- tools ---\n" + "\n".join(tool_lines))
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
