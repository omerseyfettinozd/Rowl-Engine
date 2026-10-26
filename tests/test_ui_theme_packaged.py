#!/usr/bin/env python3
"""T1: Assets/json/ui_theme.json travels in the package and the packaged game applies it.

Copies a sample, adds a ui_theme.json (one real colour, one bad value, a font, an
unknown key), packages it with tools/package_assets.py into a release layout with NO
loose Assets, mounts that release through the real C API and checks that
  * the theme in force is the package's file (RowlEngine_GetUiThemeJson: loaded,
    source = json/ui_theme.json, the colour, the bad value reported, the unknown key
    ignored),
  * the title screen paints the themed panel colour (offscreen frame),
  * the same package WITHOUT the file reports the built-in theme and paints the
    old panel colour (a missing file is the normal state).
"""

import ctypes
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"

THEME = {
    "format": 1,
    "menu": {"panel_fill": "#445566", "dim": "not-a-colour", "unknown_key": 1},
    "fonts": {"menu": "fonts/default.ttf"},
    "future_section": {"x": 1},
}


SKIN_THEME = {
    "format": 1,
    "menu": {"panel_skin": {"image": "ui/panel.png"}},
    "title_screen": {"background": {"image": "ui/backdrop.png", "mode": "stretch"}},
}


def solid_png(width, height, rgb):
    """Minimal RGBA PNG (zlib is in the standard library)."""
    def chunk(kind, body):
        data = kind + body
        return struct.pack(">I", len(body)) + data + struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF)
    row = b"\x00" + bytes(rgb + (255,)) * width
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(row * height)) + chunk(b"IEND", b""))


def fail(message):
    print(f"[UiThemePackaged] FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def ok(message):
    print(f"[PASS] {message}", flush=True)


def build_release(sample_dir, base, name, theme, images=None):
    project = base / (name + "_src")
    shutil.copytree(sample_dir / "Assets", project / "Assets")
    shutil.copy2(sample_dir / "project.rowlproj", project / "project.rowlproj")
    if theme is not None:
        (project / "Assets" / "json" / "ui_theme.json").write_text(json.dumps(theme), encoding="utf-8")
    for rel, data in (images or {}).items():
        target = project / "Assets" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    release = base / name
    package = release / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    done = subprocess.run(
        [sys.executable, str(PACKAGER), str(project / "Assets"), str(package),
         "--game-manifest", str(project / "project.rowlproj")],
        capture_output=True, text=True, check=False)
    if done.returncode != 0:
        fail(f"packager failed: {done.stdout}{done.stderr}")
    shutil.copy2(project / "project.rowlproj", release / "project.rowlproj")
    loose = [p for p in (release / "Assets").rglob("*") if p.is_file() and p.suffix != ".rowlpkg"
             and not p.name.endswith(".sha256")]
    if loose:
        fail(f"release has loose assets: {loose}")
    return release


def open_engine(lib, release):
    handle = lib.RowlEngine_Create()
    if lib.RowlEngine_Init(handle, 960, 540, 0) != 1:
        fail("RowlEngine_Init")
    lib.RowlEngine_SetProjectDirectory(handle, str(release).encode("utf-8"))
    return handle


def query(lib, handle, name):
    fn = getattr(lib, name)
    fn.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    fn.restype = ctypes.c_int32
    required = ctypes.c_uint32(0)
    if fn(handle, None, 0, ctypes.byref(required)) != 0:
        fail(f"{name} size query")
    buf = ctypes.create_string_buffer(required.value + 1)
    if fn(handle, buf, required.value, ctypes.byref(required)) != 0:
        fail(f"{name} copy")
    return json.loads(buf.value.decode("utf-8"))


def pixel(lib, handle, x, y):
    lib.RowlEngine_GetPixelBufferEx.restype = ctypes.c_void_p
    lib.RowlEngine_GetPixelBufferEx.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32),
                                                ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_uint32)]
    w, h, pitch = ctypes.c_uint32(), ctypes.c_uint32(), ctypes.c_uint32()
    base = lib.RowlEngine_GetPixelBufferEx(handle, ctypes.byref(w), ctypes.byref(h), ctypes.byref(pitch))
    if not base or w.value != 960 or h.value != 540:
        fail(f"pixel buffer {w.value}x{h.value}")
    data = ctypes.string_at(base + y * pitch.value + x * 4, 3)
    return data[0], data[1], data[2]


def frame(lib, handle, steps=6):
    lib.RowlEngine_Step.argtypes = [ctypes.c_void_p, ctypes.c_float]
    for _ in range(steps):
        lib.RowlEngine_Step(handle, 0.016)


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <engine-lib> <sample-dir>", file=sys.stderr)
        return 2
    engine_lib = pathlib.Path(sys.argv[1]).resolve()
    sample_dir = pathlib.Path(sys.argv[2]).resolve()
    if not engine_lib.is_file() or not (sample_dir / "Assets").is_dir():
        print("missing input", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="rowl-ui-theme-pkg-") as directory:
        base = pathlib.Path(directory)
        os.environ["XDG_DATA_HOME"] = str(base / "xdg")
        os.environ["LOCALAPPDATA"] = str(base / "xdg")
        os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
        os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
        themed = build_release(sample_dir, base, "themed", THEME)
        plain = build_release(sample_dir, base, "plain", None)
        skinned = build_release(sample_dir, base, "skinned", SKIN_THEME,
                                {"ui/panel.png": solid_png(8, 8, (0, 0, 200)),
                                 "ui/backdrop.png": solid_png(8, 8, (20, 180, 20))})

        path = str(engine_lib)
        lib = ctypes.CDLL(path, winmode=0) if os.name == "nt" else ctypes.CDLL(path)
        lib.RowlEngine_Create.restype = ctypes.c_void_p
        lib.RowlEngine_Init.restype = ctypes.c_int
        lib.RowlEngine_Init.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int]
        lib.RowlEngine_SetProjectDirectory.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        lib.RowlEngine_OpenTitleScreen.argtypes = [ctypes.c_void_p]
        lib.RowlEngine_SetPlayState.argtypes = [ctypes.c_void_p, ctypes.c_int]

        # --- the package carries the theme and the packaged game applies it -------------
        handle = open_engine(lib, themed)
        info = query(lib, handle, "RowlEngine_GetUiThemeJson")
        if not (info["loaded"] is True and info["source"] == "json/ui_theme.json"):
            fail(f"theme not loaded from the package: {info['source']!r} loaded={info['loaded']}")
        ok("the package carries ui_theme.json (loaded from the .rowlpkg, no loose file)")
        if info["theme"]["menu"]["panel_fill"] != "#445566" or info["theme"]["fonts"]["menu"] != "fonts/default.ttf":
            fail(f"theme values: {info['theme']['menu']} {info['theme']['fonts']}")
        if info["theme"]["menu"]["dim"] != "#000000A0":
            fail("a bad colour must keep its default")
        if not any("menu.dim" in line for line in info["diagnostics"]) or \
                any("unknown_key" in line or "future_section" in line for line in info["diagnostics"]):
            fail(f"diagnostics: {info['diagnostics']}")
        ok("bad value -> default + diagnostic; unknown keys ignored")
        lib.RowlEngine_OpenTitleScreen(handle)
        lib.RowlEngine_SetPlayState(handle, 1)
        frame(lib, handle)
        got = pixel(lib, handle, 290, 64)
        if got != (0x44, 0x55, 0x66) and any(abs(a - b) > 8 for a, b in zip(got, (0x44, 0x55, 0x66))):
            fail(f"themed panel pixel {got}")
        ok(f"the title screen paints the themed panel colour {got}")
        lib.RowlEngine_Shutdown(handle)
        lib.RowlEngine_Destroy(handle)

        # --- the same package without the file: the built-in look ----------------------
        handle = open_engine(lib, plain)
        info = query(lib, handle, "RowlEngine_GetUiThemeJson")
        if info["loaded"] is not False or info["source"] != "" or info["diagnostics"]:
            fail(f"plain release should report the built-in theme: {info}")
        lib.RowlEngine_OpenTitleScreen(handle)
        lib.RowlEngine_SetPlayState(handle, 1)
        frame(lib, handle)
        got = pixel(lib, handle, 290, 64)
        if any(abs(a - b) > 10 for a, b in zip(got, (15, 15, 26))):
            fail(f"built-in panel pixel {got}")
        ok(f"without the file the title screen keeps the old panel colour {got}")
        lib.RowlEngine_Shutdown(handle)
        lib.RowlEngine_Destroy(handle)

        # --- T2: pictures travel in the package and the packaged player draws them ------
        handle = open_engine(lib, skinned)
        info = query(lib, handle, "RowlEngine_GetUiThemeJson")
        if info["diagnostics"]:
            fail(f"skin images not found in the package: {info['diagnostics']}")
        if info["theme"]["menu"]["panel_skin"]["image"] != "ui/panel.png":
            fail("the theme JSON does not report the skin")
        lib.RowlEngine_OpenTitleScreen(handle)
        lib.RowlEngine_SetPlayState(handle, 1)
        frame(lib, handle)
        got = pixel(lib, handle, 560, 67)
        if any(abs(a - b) > 3 for a, b in zip(got, (0, 0, 200))):
            fail(f"skinned panel pixel {got}")
        back = pixel(lib, handle, 100, 300)
        if any(abs(a - b) > 3 for a, b in zip(back, (20, 180, 20))):
            fail(f"title backdrop pixel {back}")
        ok(f"pictures in the .rowlpkg: panel skin {got}, title background {back}")
        lib.RowlEngine_Shutdown(handle)
        lib.RowlEngine_Destroy(handle)
    print("[UiThemePackaged] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
