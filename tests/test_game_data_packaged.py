#!/usr/bin/env python3
"""D01b: packaged games keep their saves per game id, not per folder.

Packages the sample twice with two different game ids (rowl/game.json in the
package), mounts them through the real C API with a temp XDG_DATA_HOME and
checks that
  * the two games never see each other's save slots,
  * moving a game folder keeps its saves (same id, new path),
  * the install folder itself receives no saves/ directory.
"""

import ctypes
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
ID_A = "7c1d9a3e-2b54-4f68-8e0a-5d3b1c9f2a64"
ID_B = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90"


def fail(message):
    print(f"[GameDataPackaged] FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def package_game(sample_dir, destination, game_id):
    package = destination / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    done = subprocess.run(
        [sys.executable, str(PACKAGER), str(sample_dir / "Assets"), str(package),
         "--game-id", game_id], capture_output=True, text=True, check=False)
    if done.returncode != 0:
        fail(f"packager failed: {done.stdout}{done.stderr}")


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <engine-lib> <sample-dir>", file=sys.stderr)
        return 2
    engine_lib = pathlib.Path(sys.argv[1])
    sample_dir = pathlib.Path(sys.argv[2])
    if not engine_lib.is_file() or not (sample_dir / "Assets").is_dir():
        print("missing input", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="rowl-game-data-pkg-") as directory:
        base = pathlib.Path(directory)
        os.environ["XDG_DATA_HOME"] = str(base / "xdg")
        os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
        os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
        game_a = base / "install" / "game_a"
        game_b = base / "install" / "game_b"
        package_game(sample_dir, game_a, ID_A)
        package_game(sample_dir, game_b, ID_B)

        lib = ctypes.CDLL(str(engine_lib))
        lib.RowlEngine_Create.restype = ctypes.c_void_p
        for name in ("RowlEngine_Init", "RowlEngine_SaveGameSlot", "RowlEngine_HasSaveSlot"):
            getattr(lib, name).restype = ctypes.c_int
        lib.RowlEngine_Init.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.RowlEngine_SetProjectDirectory.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        for name in ("RowlEngine_SaveGameSlot", "RowlEngine_HasSaveSlot"):
            getattr(lib, name).argtypes = [ctypes.c_void_p, ctypes.c_int32]
        lib.RowlEngine_Destroy.argtypes = [ctypes.c_void_p]
        getter = lib.RowlEngine_GetGameDataDirectoryUtf8
        getter.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32,
                           ctypes.POINTER(ctypes.c_uint32)]

        def data_dir(handle):
            buffer = ctypes.create_string_buffer(4096)
            required = ctypes.c_uint32(0)
            if getter(handle, buffer, 4096, ctypes.byref(required)) != 0:
                fail("GetGameDataDirectoryUtf8 failed")
            return buffer.value.decode("utf-8")

        def session(project):
            handle = lib.RowlEngine_Create()
            if not handle or lib.RowlEngine_Init(handle, 64, 64, 0) != 1:
                fail("engine init failed")
            lib.RowlEngine_SetProjectDirectory(handle, str(project).encode("utf-8"))
            return handle

        user_root = base / "xdg" / "rowl-engine" / "games"

        h = session(game_a)
        if pathlib.Path(data_dir(h)) != user_root / ID_A:
            fail(f"game A data dir is {data_dir(h)}")
        if not lib.RowlEngine_SaveGameSlot(h, 1):
            fail("game A could not save slot 1")
        lib.RowlEngine_Destroy(h)

        h = session(game_b)
        if pathlib.Path(data_dir(h)) != user_root / ID_B:
            fail(f"game B data dir is {data_dir(h)}")
        if lib.RowlEngine_HasSaveSlot(h, 1):
            fail("game B sees game A's save slot 1")
        if not lib.RowlEngine_SaveGameSlot(h, 2):
            fail("game B could not save slot 2")
        lib.RowlEngine_Destroy(h)

        moved = base / "elsewhere" / "renamed_game_a"
        moved.parent.mkdir(parents=True)
        shutil.move(str(game_a), str(moved))
        h = session(moved)
        if not lib.RowlEngine_HasSaveSlot(h, 1):
            fail("moving the game folder lost the saves")
        if lib.RowlEngine_HasSaveSlot(h, 2):
            fail("game A sees game B's save slot 2 after the move")
        lib.RowlEngine_Destroy(h)

        for folder in (moved, game_b):
            if (folder / "saves").exists():
                fail(f"save data leaked into the install folder {folder}")

    print("[GameDataPackaged] per-game saves are isolated and survive a folder move.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
