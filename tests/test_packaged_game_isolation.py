#!/usr/bin/env python3
"""D01d: packaged games keep identity, saves and player profile apart.

Two releases are packaged from the SAME sample (same asset names) with
DIFFERENT game ids, then driven through the real standalone player with a
temporary XDG_DATA_HOME:

  * --print-game-identity reports distinct ids and distinct per-game data dirs
    (saves/ and profile/ beside each other), source "package";
  * --package-smoke-test on game A marks the first line read, sets a
    preference and flushes -> game B's profile and saves stay untouched;
  * a save written by game A is invisible to game B;
  * relaunching A finds the read id and the preference on disk, applies the
    stored preferences (read back from the engine) and keeps them;
  * a hand-written future-version profile ("version": 99) stays byte-identical
    through a smoke run and the player reports it read-only;
  * the release launcher (cd into the game dir) still sends data to the
    per-game XDG dir, wherever it is started from, and the install folder never
    receives saves/ or profile/.

Usage: test_packaged_game_isolation.py <player-bin> <engine-lib> <sample-dir>
"""

import ctypes
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
LAUNCHER_TOOL = ROOT / "tools" / "make_release_launchers.py"
sys.path.insert(0, str(ROOT / "tools"))
import launcher_contract  # noqa: E402

ID_A = "7c1d9a3e-2b54-4f68-8e0a-5d3b1c9f2a64"
ID_B = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90"
UUID_RE = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
SMOKE_PROFILE_RE = re.compile(
    r"\[Player\] Package smoke profile: status=(\w+) read_only=(true|false) "
    r"read_count=(\d+) preference_set=(true|false) flushed=(true|false)")


def fail(message):
    print(f"[PackagedGameIsolation] FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def require(condition, message):
    if not condition:
        fail(message)


def package_release(sample_dir, destination, game_id, player_bin, engine_lib):
    package = destination / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    done = subprocess.run(
        [sys.executable, str(PACKAGER), str(sample_dir / "Assets"), str(package),
         "--game-id", game_id], capture_output=True, text=True, check=False)
    require(done.returncode == 0, f"packager failed: {done.stdout}{done.stderr}")
    player_name = "RowlGame.exe" if os.name == "nt" else "RowlGame"
    shutil.copy2(player_bin, destination / player_name)
    shutil.copy2(engine_lib, destination / engine_lib.name)
    return destination / player_name


def run_player(player, release, env, *args, cwd=None):
    return subprocess.run([str(player), "--project", str(release), *args],
                          capture_output=True, text=True, check=False, env=env,
                          cwd=str(cwd) if cwd else None, timeout=120)


def identity_of(done):
    """The single JSON line --print-game-identity prints (engine logs share stdout)."""
    require(done.returncode == 0,
            f"--print-game-identity exited {done.returncode}: {done.stdout}{done.stderr}")
    lines = [line for line in done.stdout.splitlines() if line.startswith('{"')]
    require(len(lines) == 1, f"expected one JSON identity line, got {len(lines)}:\n{done.stdout}")
    info = json.loads(lines[0])
    for key in ("game_id", "identity_source", "data_dir", "save_dir", "profile_dir", "profile"):
        require(key in info, f"identity JSON lacks {key!r}: {lines[0]}")
    return info


def smoke_profile(done):
    require(done.returncode == 0,
            f"--package-smoke-test exited {done.returncode}: {done.stdout}{done.stderr}")
    require("Package smoke frame rendered" in done.stdout, f"no smoke frame:\n{done.stdout}")
    match = SMOKE_PROFILE_RE.search(done.stdout)
    require(match is not None, f"smoke printed no profile line:\n{done.stdout}")
    return {"status": match.group(1), "read_only": match.group(2) == "true",
            "read_count": int(match.group(3)), "preference_set": match.group(4) == "true",
            "flushed": match.group(5) == "true"}


def content_ids_of(sample_dir):
    graph = json.loads((sample_dir / "Assets" / "json" / "full_story_graph.json")
                       .read_text(encoding="utf-8"))
    found = set()

    def walk(node):
        if isinstance(node, dict):
            value = node.get("content_id")
            if isinstance(value, str):
                found.add(value.lower())
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)

    walk(graph)
    return found


def tree(path):
    if not path.exists():
        return []
    return sorted(str(p.relative_to(path)) for p in path.rglob("*"))


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <player-bin> <engine-lib> <sample-dir>", file=sys.stderr)
        return 2
    player_bin = pathlib.Path(sys.argv[1])
    engine_lib = pathlib.Path(sys.argv[2])
    sample_dir = pathlib.Path(sys.argv[3])
    for path in (player_bin, engine_lib, sample_dir / "Assets" / "json" / "full_story_graph.json"):
        if not path.is_file():
            print(f"missing input: {path}", file=sys.stderr)
            return 2
    sample_ids = content_ids_of(sample_dir)
    require(sample_ids, "the sample must carry dialogue content ids for the read-state check")

    with tempfile.TemporaryDirectory(prefix="rowl-pkg-isolation-") as directory:
        base = pathlib.Path(directory)
        xdg = base / "xdg"
        xdg.mkdir()
        env = dict(os.environ, XDG_DATA_HOME=str(xdg), LOCALAPPDATA=str(xdg),
                   SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
        release_a = base / "install" / "game_a"
        release_b = base / "install" / "game_b"
        player_a = package_release(sample_dir, release_a, ID_A, player_bin, engine_lib)
        player_b = package_release(sample_dir, release_b, ID_B, player_bin, engine_lib)
        elsewhere = base / "elsewhere"
        elsewhere.mkdir()

        # --- identity and data layout --------------------------------------
        info_a = identity_of(run_player(player_a, release_a, env, "--print-game-identity", cwd=elsewhere))
        info_b = identity_of(run_player(player_b, release_b, env, "--print-game-identity", cwd=elsewhere))
        require(info_a["game_id"] == ID_A, f"game A id is {info_a['game_id']}")
        require(info_b["game_id"] == ID_B, f"game B id is {info_b['game_id']}")
        require(info_a["game_id"] != info_b["game_id"], "two different games share an id")
        require(info_a["identity_source"] == "package" and info_b["identity_source"] == "package",
                f"identity must come from the package: {info_a['identity_source']}/{info_b['identity_source']}")
        data_a, data_b = pathlib.Path(info_a["data_dir"]), pathlib.Path(info_b["data_dir"])
        require(data_a != data_b, "two different games share a data directory")
        for data, game_id in ((data_a, ID_A), (data_b, ID_B)):
            require(data.name == game_id and data.parent.name == "games"
                    and xdg in data.parents, f"data dir is not <XDG>/.../games/<id>: {data}")
        require(pathlib.Path(info_a["save_dir"]) == data_a / "saves"
                and pathlib.Path(info_a["profile_dir"]) == data_a / "profile",
                f"saves/profile are not beside each other: {info_a}")
        require(info_a["profile"]["status"] == "missing" and info_b["profile"]["status"] == "missing",
                "fresh games must start without a profile")

        # --- game A: smoke writes read id + preference; a save lands in A's dir
        first = smoke_profile(run_player(player_a, release_a, env, "--package-smoke-test", cwd=elsewhere))
        require(first == {"status": "missing", "read_only": False, "read_count": 1,
                          "preference_set": True, "flushed": True}, f"unexpected first smoke: {first}")
        profile_a = data_a / "profile" / "player-profile.json"
        written = json.loads(profile_a.read_text(encoding="utf-8"))
        require(written.get("game_id") == ID_A and written.get("version") == 1,
                f"profile v1 of game A is stamped wrong: {written.get('version')}/{written.get('game_id')}")
        require(len(written["read_content_ids"]) == 1 and written["read_content_ids"][0] in sample_ids,
                f"game A did not record the first line as read: {written['read_content_ids']}")
        require(abs(written["text_speed_multiplier"] - 1.0) < 1e-6,
                "smoke run changed the player's text speed (must restore it)")

        os.environ.update(XDG_DATA_HOME=str(xdg), LOCALAPPDATA=str(xdg),
                          SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
        # Windows: dependent DLLs (SDL3, Lua, zstd…) live next to the engine or on PATH;
        # Python 3.8+ ignores PATH for them unless winmode=0 restores the standard search.
        engine_path = str(engine_lib.resolve())
        lib = ctypes.CDLL(engine_path, winmode=0) if os.name == "nt" else ctypes.CDLL(engine_path)
        lib.RowlEngine_Create.restype = ctypes.c_void_p
        lib.RowlEngine_Init.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.RowlEngine_SetProjectDirectory.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.RowlEngine_SaveGameSlot.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.RowlEngine_HasSaveSlot.argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.RowlEngine_Destroy.argtypes = [ctypes.c_void_p]

        def with_session(release, action):
            handle = lib.RowlEngine_Create()
            require(handle and lib.RowlEngine_Init(handle, 64, 64, 0) == 1, "engine init failed")
            lib.RowlEngine_SetProjectDirectory(handle, str(release).encode("utf-8"))
            try:
                return action(handle)
            finally:
                lib.RowlEngine_Destroy(handle)

        require(with_session(release_a, lambda h: lib.RowlEngine_SaveGameSlot(h, 3)) == 1,
                "game A could not save slot 3")
        require((data_a / "saves").is_dir() and any((data_a / "saves").iterdir()),
                "game A's save did not land in its per-game saves dir")

        # --- game B must be untouched ---------------------------------------
        require(not (data_b / "profile" / "player-profile.json").exists(),
                "game B grew a profile although only game A ran")
        require(not any(p.is_file() for p in (data_b / "saves").rglob("*")),
                "game B's saves dir received files from game A")
        require(with_session(release_b, lambda h: lib.RowlEngine_HasSaveSlot(h, 3)) == 0,
                "game B sees game A's save slot")
        after_b = identity_of(run_player(player_b, release_b, env, "--print-game-identity", cwd=elsewhere))
        require(after_b["profile"]["status"] == "missing" and after_b["profile"]["read_count"] == 0,
                f"game B's profile changed: {after_b['profile']}")

        # --- relaunch A: persisted read id + preference ---------------------
        again = identity_of(run_player(player_a, release_a, env, "--print-game-identity", cwd=elsewhere))
        require(again["profile"]["status"] == "loaded" and again["profile"]["read_count"] == 1,
                f"relaunch lost the read id: {again['profile']}")
        # A player who set volumes earlier: stored values must be applied at start
        # (the reported values are read back from the engine's getters).
        edited = json.loads(profile_a.read_text(encoding="utf-8"))
        edited["master_volume"] = 0.4
        edited["bgm_volume"] = 0.25
        edited["text_scale"] = 1.5
        edited["text_speed_multiplier"] = 1.5
        profile_a.write_text(json.dumps(edited, indent=2), encoding="utf-8")
        second = run_player(player_a, release_a, env, "--package-smoke-test", cwd=elsewhere)
        profile_line = smoke_profile(second)
        require(profile_line["status"] == "loaded" and profile_line["read_count"] == 1
                and not profile_line["read_only"], f"relaunch smoke profile: {profile_line}")
        applied = [line for line in second.stdout.splitlines()
                   if line.startswith("[Player] Applied stored preferences:")]
        require(len(applied) == 1, f"player did not report applying preferences:\n{second.stdout}")
        for expected in ("master_volume=0.4", "bgm_volume=0.25", "text_scale=1.5",
                         "text_speed_multiplier=1.5"):
            require(expected in applied[0].split(), f"{expected} not applied: {applied[0]}")
        persisted = json.loads(profile_a.read_text(encoding="utf-8"))
        require(persisted["master_volume"] == 0.4 and persisted["text_speed_multiplier"] == 1.5
                and persisted["read_content_ids"] == written["read_content_ids"],
                "a relaunch altered the stored profile")

        # --- release launcher: data still goes to the per-game XDG dir -------
        if os.name != "nt":
            launched = subprocess.run(
                [sys.executable, str(LAUNCHER_TOOL), str(release_a), "--platform", "posix"],
                capture_output=True, text=True, check=False)
            require(launched.returncode == 0, f"launcher generation failed: {launched.stdout}{launched.stderr}")
            launcher_text = (release_a / "run_game.sh").read_text(encoding="utf-8")
            require(not launcher_contract.data_dir_override_problems(launcher_text, "run_game.sh"),
                    "the shipped launcher redirects the user data directory")
            via_launcher = identity_of(subprocess.run(
                [str(release_a / "run_game.sh"), "--print-game-identity"],
                capture_output=True, text=True, check=False, env=env, cwd=str(elsewhere), timeout=120))
            require(via_launcher["data_dir"] == info_a["data_dir"],
                    f"launcher changed the data dir: {via_launcher['data_dir']} != {info_a['data_dir']}")

        # --- future-version profile stays byte-identical, reported read-only --
        future = json.dumps({"version": 99, "game_id": ID_A, "read_content_ids": [],
                             "from_the_future": {"keep": ["me"]}}, indent=1).encode("utf-8") + b"\n"
        profile_a.write_bytes(future)
        listing = tree(data_a)
        done = run_player(player_a, release_a, env, "--package-smoke-test", cwd=elsewhere)
        read_only = smoke_profile(done)
        require(read_only["status"] == "unsupported_version" and read_only["read_only"],
                f"future-version profile not reported read-only: {read_only}")
        require("read-only (unsupported_version)" in done.stdout,
                f"player output lacks the read-only message:\n{done.stdout}")
        require(profile_a.read_bytes() == future, "a future-version profile was modified")
        require(tree(data_a) == listing, f"files appeared next to a read-only profile: {tree(data_a)}")
        future_info = identity_of(run_player(player_a, release_a, env, "--print-game-identity", cwd=elsewhere))
        require(future_info["profile"]["status"] == "unsupported_version" and future_info["profile"]["read_only"],
                f"identity JSON hides the read-only state: {future_info['profile']}")
        require(profile_a.read_bytes() == future, "--print-game-identity modified a future-version profile")

        # --- the install folders never receive user data ---------------------
        for release in (release_a, release_b):
            for name in ("saves", "profile"):
                require(not (release / name).exists(), f"user data leaked into the install folder: {release / name}")

    print("[PackagedGameIsolation] two packaged games keep ids, saves and profiles apart; "
          "read state and preferences persist; future-version profile stays read-only.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
