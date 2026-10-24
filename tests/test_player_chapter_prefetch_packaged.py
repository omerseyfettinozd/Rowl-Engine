#!/usr/bin/env python3
"""D06-3: the packaged player feeds chapters and warms assets (prefetch) from its own files.

Builds a small two-chapter game with the editor's split chapter files, packs it
with tools/package_assets.py and runs the real player (--package-smoke-test,
offscreen) against the release layout - and against the same game as loose
files. The smoke prints the prefetch driver's outcome:

  [Player] Package smoke prefetch: chapters_fed=... fed_files=... windows=...
      refused=... total=... ready=... missing=... complete=...

Cases: consistent split (fed from the package, assets warmed, queue drained),
stale split (chapter files ignored, the game still plays and prefetches from
the graph), no split (graph chapters), loose files (same as packaged).
"""

import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
GAME_UUID = "5b3f9c1e-6d24-4a7b-9e18-2c4d8a0f7b53"
LINE = re.compile(r"\[Player\] Package smoke prefetch: (.*)")


def png_bytes():
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + b"\x20\x40\x80\xff" * 4 for _ in range(4))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def wav_bytes(seconds=1):
    rate = 22050
    data = b"\x00\x00" * rate * seconds
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
            + b"data" + struct.pack("<I", len(data)) + data)


def node(node_id, chapter, nxt):
    components = [
        {"type": "dialogue", "id": f"d{node_id}", "enabled": True,
         "data": {"speaker": "S", "dialogue": f"Line {node_id}", "typewriter_enabled": False,
                  "content_id": f"40000000-0000-4000-8000-0000000000{node_id:02d}"}},
        {"type": "background", "id": f"b{node_id}", "enabled": True, "data": {"texture": "images/bg.png"}},
        {"type": "audio", "id": f"a{node_id}", "enabled": True,
         "data": {"bgm_track": "audio/theme.wav", "sfx_track": "audio/blip.wav"}},
    ]
    return {"id": node_id, "chapter_id": chapter, "title": f"N{node_id}",
            "objects": [{"id": f"o{node_id}", "name": "D", "is_active": True, "components": components}],
            "next_nodes": [{"id": nxt}] if nxt else []}


def write_game(root, split, drift):
    assets = root / "Assets"
    (assets / "json").mkdir(parents=True)
    (assets / "images").mkdir()
    (assets / "audio").mkdir()
    (assets / "images" / "bg.png").write_bytes(png_bytes())
    (assets / "audio" / "theme.wav").write_bytes(wav_bytes())
    (assets / "audio" / "blip.wav").write_bytes(wav_bytes())
    nodes = [node(1, "ch1", 2), node(2, "ch1", 3), node(3, "ch2", 4), node(4, "ch2", 0)]
    chapters = [{"id": "ch1", "title": "One", "order": 1, "start_node_id": 1},
                {"id": "ch2", "title": "Two", "order": 2, "start_node_id": 3}]
    graph = {"format_version": 5, "start_node_id": 1, "chapters": chapters, "nodes": nodes}
    (assets / "json" / "full_story_graph.json").write_text(json.dumps(graph), encoding="utf-8")
    if split:
        directory = assets / "json" / "chapters"
        directory.mkdir()
        (directory / "chapter_index.json").write_text(json.dumps(
            {"format_version": 5, "start_node_id": 1, "chapters": chapters,
             "node_order": [n["id"] for n in nodes]}), encoding="utf-8")
        for chapter in chapters:
            members = [n for n in nodes if n["chapter_id"] == chapter["id"]]
            if drift and chapter["id"] == "ch2":
                members = members[:-1]  # the split is older than the graph
            (directory / f"{chapter['id']}.json").write_text(json.dumps(
                {"format_version": 5, "chapter_id": chapter["id"], "nodes": members}), encoding="utf-8")
    (root / "project.rowlproj").write_text(json.dumps(
        {"project_uuid": GAME_UUID, "name": "Chapter Prefetch Smoke"}), encoding="utf-8")


def run_smoke(release, player_name, env):
    return subprocess.run([str(release / player_name), "--project", str(release), "--package-smoke-test"],
                          capture_output=True, text=True, check=False, env=env)


def parse(smoke):
    match = LINE.search(smoke.stdout)
    if not match:
        return None
    return dict(part.split("=", 1) for part in match.group(1).split())


def expect(name, condition, detail=""):
    if not condition:
        print(f"[FAIL] {name} {detail}", file=sys.stderr)
        return False
    print(f"[PASS] {name}")
    return True


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <player-bin> <engine-lib>", file=sys.stderr)
        return 2
    player_bin, engine_lib = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    player_name = "RowlGame.exe" if os.name == "nt" else "RowlGame"
    ok = True
    with tempfile.TemporaryDirectory(prefix="rowl-chapter-prefetch-") as directory:
        base = pathlib.Path(directory)
        env = dict(os.environ)
        env["SDL_AUDIODRIVER"] = "dummy"
        env["SDL_VIDEODRIVER"] = "dummy"
        env["XDG_DATA_HOME"] = str(base / "xdg")
        env["LOCALAPPDATA"] = env["XDG_DATA_HOME"]

        def packaged(name, split, drift):
            source = base / f"{name}-src"
            write_game(source, split, drift)
            release = base / f"{name}-release"
            (release / "Assets" / "packages").mkdir(parents=True)
            pack = subprocess.run([sys.executable, str(PACKAGER), str(source / "Assets"),
                                   str(release / "Assets" / "packages" / "game.rowlpkg"),
                                   "--game-manifest", str(source / "project.rowlproj")],
                                  capture_output=True, text=True, check=False)
            if pack.returncode != 0:
                print(f"packager failed: {pack.stdout}{pack.stderr}", file=sys.stderr)
                sys.exit(1)
            shutil.copy2(player_bin, release / player_name)
            shutil.copy2(engine_lib, release / engine_lib.name)
            shutil.copy2(source / "project.rowlproj", release / "project.rowlproj")
            return pack, run_smoke(release, player_name, env)

        # Consistent split, packaged: fed from the package, assets warmed.
        pack, smoke = packaged("good", split=True, drift=False)
        result = parse(smoke)
        ok &= expect("packaged, consistent split: the player smoke runs", smoke.returncode == 0 and result is not None,
                     smoke.stdout + smoke.stderr)
        if result:
            ok &= expect("packaged, consistent split: chapters fed from the package (index + 2 files)",
                         result["chapters_fed"] == "true" and result["fed_files"] == "3", str(result))
            ok &= expect("packaged, consistent split: windows started, none refused",
                         int(result["windows"]) >= 1 and result["refused"] == "0", str(result))
            ok &= expect("packaged, consistent split: assets warmed from the package, queue drained",
                         int(result["total"]) >= 1 and result["ready"] == result["total"]
                         and result["missing"] == "0" and result["complete"] == "true", str(result))
        ok &= expect("packaged, consistent split: packer stays quiet", "[Packer][WARN]" not in pack.stderr, pack.stderr)

        # Stale split: the packer warns, the player ignores the chapter files and plays on.
        pack, smoke = packaged("stale", split=True, drift=True)
        result = parse(smoke)
        ok &= expect("stale split: the packer warns with chapter-drift", "[Packer][WARN][chapter-drift]" in pack.stderr,
                     pack.stderr)
        ok &= expect("stale split: the player still starts and renders",
                     smoke.returncode == 0 and "Package smoke frame rendered" in smoke.stdout and result is not None,
                     smoke.stdout + smoke.stderr)
        if result:
            ok &= expect("stale split: chapter files ignored, prefetch follows the graph",
                         result["chapters_fed"] == "false" and int(result["windows"]) >= 1
                         and result["refused"] == "0" and result["missing"] == "0", str(result))

        # No split: the graph's own chapters.
        pack, smoke = packaged("plain", split=False, drift=False)
        result = parse(smoke)
        ok &= expect("no split: prefetch follows the graph's chapters",
                     smoke.returncode == 0 and result is not None and result["chapters_fed"] == "false"
                     and int(result["windows"]) >= 1 and result["ready"] == result["total"]
                     and result["missing"] == "0", smoke.stdout + smoke.stderr)

        # Loose files (no package): the same behavior.
        loose = base / "loose"
        write_game(loose, split=True, drift=False)
        shutil.copy2(player_bin, loose / player_name)
        shutil.copy2(engine_lib, loose / engine_lib.name)
        smoke = run_smoke(loose, player_name, env)
        result = parse(smoke)
        ok &= expect("loose files: chapters fed from Assets and assets warmed",
                     smoke.returncode == 0 and result is not None and result["chapters_fed"] == "true"
                     and result["fed_files"] == "3" and result["ready"] == result["total"]
                     and result["missing"] == "0", smoke.stdout + smoke.stderr)

    print("[ChapterPrefetchPackaged] OK" if ok else "[ChapterPrefetchPackaged] FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
