#!/usr/bin/env python3
"""Executable-doc gate for docs/PLAYER_GUIDE.md (Faz 6 Dilim 5).

The guide's every ```sh block is parsed and REALLY executed top-to-bottom
(zip -> .sh -> install -> player smoke -> verify -> uninstall). Each block's
first line must be a `# guide-probe: expect-ok|expect-fail` tag: untagged
blocks fail loudly, so a stale/undocumented command is impossible -- a
changed guide command changes what runs, and a changed flow contradicts
the guide.

Flag drift is also impossible: every `--long-flag` on a `rowl_player` line
in the guide must exist in the real `build/bin/rowl_player --help` output,
and the core flag set must be documented in the guide (a removed flag
breaks the test just like a bogus one does).

Production code is only CALLED (export_game, package_assets, rowl_player),
never modified.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
GUIDE = ROOT / "docs" / "PLAYER_GUIDE.md"


def _with_windows_exe(candidate):
    """Append `.exe` on Windows when the suffixed binary is the real one.

    The repo builds to build/bin on every host; only the suffix differs.
    An explicit env override that already names the file wins untouched.
    """
    import os
    candidate = pathlib.Path(candidate)
    if (os.name == "nt" and candidate.suffix == ""
            and candidate.with_suffix(".exe").is_file()):
        return candidate.with_suffix(".exe")
    return candidate


PLAYER_BIN = _with_windows_exe(pathlib.Path(__import__("os").environ.get(
    "ROWL_PLAYER_BIN", ROOT / "build" / "bin" / "rowl_player")))

TAG_RE = re.compile(r"^#\s*guide-probe:\s*(expect-ok|expect-fail)\s*$")
FENCE_RE = re.compile(r"^```(\w*)\s*$")
LONG_FLAG_RE = re.compile(r"--[a-z][a-z-]*")

# Flags the guide must document (from engine/src/player/main.cpp --help).
CORE_FLAGS = {
    "--help", "--version", "--project", "--story", "--width", "--height",
    "--title", "--slot", "--no-vsync", "--gpu-smoke-test",
    "--package-smoke-test",
}


def run(arguments, **kwargs):
    return subprocess.run(arguments, capture_output=True, text=True,
                          check=False, **kwargs)


def require_tools(*names):
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise SystemExit(f"missing host tools for player-guide test: {missing}")


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
        raise SystemExit("PLAYER_GUIDE.md has an unterminated ```sh fence")
    return blocks


def main():
    require_tools("sh", "sha256sum", "unzip", "base64", "tail", "python3")
    guide_text = GUIDE.read_text(encoding="utf-8")
    blocks = extract_sh_blocks(guide_text)
    if len(blocks) < 8:
        raise SystemExit(
            f"PLAYER_GUIDE.md has only {len(blocks)} sh blocks, need >= 8 "
            "so the full install/play/verify/uninstall flow stays executable")

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
        raise SystemExit("no expect-fail probe: the arg-error smoke is gone")
    if "uninstall" not in guide_text:
        raise SystemExit("guide lost its uninstall coverage")

    # Flag-drift gate against the REAL player binary (help text is ground
    # truth; never hand-listed anywhere else in this test).
    if not PLAYER_BIN.is_file():
        raise SystemExit(f"player binary missing: {PLAYER_BIN}")
    help_proc = run([str(PLAYER_BIN), "--help"])
    if help_proc.returncode != 0:
        raise SystemExit(f"{PLAYER_BIN} --help exited {help_proc.returncode}")
    # Sürüm-içerik kilidi (Faz 6 Dilim 10): --version çıktısı beklenen
    # sürüm dizgisini içermeli. Beklenen değer teste elle yazılır (main.cpp
    # parse edilmez); sabit drift ederse bu test kızarır — bu istenendir.
    version_proc = run([str(PLAYER_BIN), "--version"])
    if version_proc.returncode != 0:
        raise SystemExit(
            f"{PLAYER_BIN} --version exited {version_proc.returncode}")
    if "1.0.0" not in version_proc.stdout:
        raise SystemExit(
            "player --version lost its 1.0.0 content lock: "
            f"stdout={version_proc.stdout!r}")
    help_flags = set(LONG_FLAG_RE.findall(help_proc.stdout))
    guide_player_lines = [line for line in guide_text.splitlines()
                          if "rowl_player" in line
                          or line.lstrip().startswith("|")]
    guide_flags = set()
    for line in guide_player_lines:
        guide_flags.update(LONG_FLAG_RE.findall(line))
    # --prefix belongs to the installer (export_game.py), not the player.
    bogus = sorted(guide_flags - help_flags - {"--prefix"})
    if bogus:
        raise SystemExit(f"guide documents flags the player lacks: {bogus}")
    missing = sorted(CORE_FLAGS - set(
        LONG_FLAG_RE.findall(guide_text)))
    if missing:
        raise SystemExit(f"guide no longer documents flags: {missing}")

    with tempfile.TemporaryDirectory(prefix="rowl-player-guide-") as work:
        import os
        env = dict(os.environ,
                   GUIDE_WORK=work,
                   SDL_AUDIODRIVER="dummy",
                   SDL_VIDEODRIVER="dummy")
        # Blocks run with cwd=ROOT (guide commands use repo-relative paths),
        # so player probes drop crash-logs/ into the repo root. Snapshot it
        # and remove ONLY what this run created -- never touch pre-existing
        # files, never leave litter behind (review BLOCKER fix).
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
                print(f"[PlayerGuide] block {index} ({mode}): green.")
        finally:
            if crash_dir.is_dir():
                before_names = ({p.name for p in before}
                                if before is not None else set())
                for child in crash_dir.iterdir():
                    if (child.name not in before_names
                            and (child.is_file() or child.is_symlink())):
                        child.unlink()
                if not before_names:
                    # Nothing pre-existing to preserve (the --help drift
                    # probe above may have created the empty dir itself).
                    try:
                        crash_dir.rmdir()
                    except OSError:
                        pass

    print("[PlayerGuide] guide blocks executed, flags match --help: green.")


if __name__ == "__main__":
    main()
