#!/usr/bin/env python3
"""D-K1: Stage D acceptance journey - the packaged game, no editor, end to end.

Roadmap acceptance for Stage D: "open the package -> new game -> second choice
-> save -> quit/restart -> continue -> history -> preferences -> auto/skip ->
rewind, without the editor or any CLI help", plus the D07 extras: a CG and an
achievement unlock, survive a restart, and an Extras tour changes no progress.

The game is the real sample samples/kayip_tablo, packaged by tools/package_assets.py
into a release layout (RowlGame + engine library + project.rowlproj +
Assets/packages/game.rowlpkg; no loose game assets). It is driven through the
player's own input path: a standalone SDL window (dummy video driver) that
receives real SDL key / mouse events (Enter, Esc, arrows, digits, B, A, S,
Backspace, wheel, left click on the choice buttons) pushed with SDL_PushEvent;
the engine runs the same Engine::step the player's loop runs, so time is
simulated (no sleeps). Menus, choices and state are read back through the
public C API (pause-menu JSON, QueryWorldJson, profile / extras / metadata
JSON). What the story must do is NOT hard-coded: a small reference model
reads samples/kayip_tablo's story graph (node variable effects, conditions,
choices, unlock components, backgrounds / music) and computes the expected
node, variables, unlock sets and the shortest path to an ending.

Process layout (state travels through the on-disk profile / saves and a small
state file, exactly like a real player's restarts):

  P1  play      boot -> title -> New Game -> 2 choices -> pause -> save slot 1
                -> read on -> quit through the menu
  P2  resume    boot (restart) -> title: Continue ready -> Preferences (text
                speed, text size) -> Continue -> history (B / wheel) -> quit
  P3  player    the real RowlGame binary (--package-smoke-test) applies the
                stored preferences on its own start-up (third start)
  P4  play on   boot -> preferences applied -> Continue -> Skip stops at the first
                unread line -> Auto timing -> Rewind (no side effects replayed)
                -> shortest path to an ending -> achievement toast -> quit
  P5  extras    boot (restart) -> unlocks persisted -> Extras tour (gallery /
                music room / achievements) leaves saves and profile untouched

Usage: test_d_acceptance_journey.py <player-bin> <engine-lib> <sample-dir>
           [--shots DIR]   (optional: write a PNG per main step; renders the
                            journey on the offscreen backend through the C API)
Exit code 77 (ctest SKIP) when the SDL3 shared library cannot be located.
"""

import collections
import ctypes
import ctypes.util
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"
LAUNCHER_TOOL = ROOT / "tools" / "make_release_launchers.py"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# Log lines the engine prints on a perfectly healthy start (listed, not hidden).
KNOWN_ERRORS = (
    # Engine::initialize looks for a story graph before the player has called
    # RowlEngine_SetProjectDirectory; fail-loud by design (#122), once per boot.
    "No story graph found: probed VFS candidates",
)


class Fail(Exception):
    pass


class Skip(Exception):
    pass


def check(condition, name, detail=""):
    if not condition:
        print(f"[FAIL] {name} {detail}", flush=True)
        raise Fail(f"{name} {detail}")
    print(f"[PASS] {name}", flush=True)


def note(text):
    print(f"[NOTE] {text}", flush=True)


# ===========================================================================
# Reference model: what the story graph says should happen
# ===========================================================================
class _Vars(dict):
    def __missing__(self, key):
        return 0.0


SCRIPT_EFFECTS = {"scripts/hoca.lua": ("ipucu", 1.0)}  # rowl.var_set("ipucu", n + 1)


class Model:
    def __init__(self, sample):
        self.sample = pathlib.Path(sample)
        assets = self.sample / "Assets"
        graph = json.loads((assets / "json" / "full_story_graph.json").read_text(encoding="utf-8"))
        self.start = graph["start_node_id"]
        self.nodes = {n["id"]: n for n in graph["nodes"]}
        self.extras = json.loads((assets / "json" / "extras.json").read_text(encoding="utf-8"))
        catalog = json.loads((assets / "locales" / "tr.json").read_text(encoding="utf-8"))
        self.catalog = catalog["entries"]
        self.project = json.loads((self.sample / "project.rowlproj").read_text(encoding="utf-8"))
        self.script_text = {
            path: (assets / path).read_text(encoding="utf-8") for path in SCRIPT_EFFECTS
        }

    def comps(self, nid, kind):
        for obj in self.nodes[nid].get("objects", []):
            if not obj.get("is_active", True):
                continue
            for comp in obj.get("components", []):
                if comp.get("type") == kind and comp.get("enabled", True):
                    yield comp["data"]

    def content_ids(self, nid):
        return [d["content_id"].lower() for d in self.comps(nid, "dialogue")]

    def text(self, nid):
        dlg = next(self.comps(nid, "dialogue"))
        entry = self.catalog.get(dlg["content_id"])
        return entry["text"] if entry else dlg["dialogue"]

    def options(self, nid):
        for choice in self.comps(nid, "choice"):
            return choice["options"]
        return []

    def is_choice(self, nid):
        return bool(self.options(nid))

    def ending_unlock(self, nid):
        return [d["id"] for d in self.comps(nid, "unlock") if d["kind"] == "achievement"]

    def enter(self, variables, nid):
        """Variables after the player enters node `nid` (set/add components, scripts)."""
        out = dict(variables)
        for data in self.comps(nid, "variable"):
            key, op, value = data["key"], data.get("operation", "set"), float(data["value"])
            out[key] = out.get(key, 0.0) + value if op == "add" else value
        for data in self.comps(nid, "script"):
            key, delta = SCRIPT_EFFECTS[data["path"]]
            assert key in self.script_text[data["path"]]
            out[key] = out.get(key, 0.0) + delta
        return out

    @staticmethod
    def holds(expr, variables):
        py = re.sub(r"\btrue\b", "True", re.sub(r"\bfalse\b", "False", expr))
        return bool(eval(py, {"__builtins__": {}}, _Vars(variables)))  # noqa: S307 (graph data)

    def selectable(self, nid, variables):
        return [o for o in self.options(nid)
                if o.get("enabled", True) and self.holds(o.get("condition", "true"), variables)]

    def successors(self, nid, variables):
        """(option_id or None, target) pairs the player can take from `nid`."""
        if self.is_choice(nid):
            return [(o["option_id"], o["target_node_id"]) for o in self.selectable(nid, variables)]
        return [(None, n["id"]) for n in self.nodes[nid].get("next_nodes", [])]

    def linear_next(self, nid):
        nxt = self.nodes[nid].get("next_nodes", [])
        assert len(nxt) == 1 and not self.is_choice(nid), f"node {nid} is not linear"
        return nxt[0]["id"]

    def textures(self, nid):
        """Image files a node shows (background + character sprites)."""
        found = [d["texture"] for d in self.comps(nid, "background") if d.get("texture")]
        for data in self.comps(nid, "character"):
            found += [v for v in [data.get("sprite")] + list(data.get("layers", {}).values()) if v]
        return found

    def music(self, nid):
        return [d["bgm_track"] for d in self.comps(nid, "audio") if d.get("bgm_track")]

    def unlocks_of(self, nodes):
        """CG / music ids the catalog would unlock for the nodes entered (extras.json)."""
        textures = {t for nid in nodes for t in self.textures(nid)}
        tracks = {t for nid in nodes for t in self.music(nid)}
        cg = {g["id"] for g in self.extras["gallery"] if g["image"] in textures}
        music = {m["id"] for m in self.extras["music"] if m["track"] in tracks}
        ach = {a for nid in nodes for a in self.ending_unlock(nid)}
        return {"cg": cg, "music": music, "achievements": ach}

    def walk_linear(self, nid, count):
        out = []
        for _ in range(count):
            nid = self.linear_next(nid)
            out.append(nid)
        return out

    def run_to_choice(self, nid):
        """Linear nodes after `nid` up to and including the next choice (or an end)."""
        out = []
        while not self.is_choice(nid) and self.nodes[nid].get("next_nodes"):
            nid = self.linear_next(nid)
            out.append(nid)
        return out

    def plan_to_ending(self, nid, variables):
        """Shortest node path (BFS over node + variables) to an achievement ending.

        Returns [(node, option_id or None), ...] for every node AFTER `nid`.
        """
        key = lambda n, v: (n, tuple(sorted((k, round(x, 6)) for k, x in v.items())))  # noqa: E731
        start = key(nid, variables)
        parent = {start: None}
        queue = collections.deque([(nid, dict(variables))])
        while queue:
            node, state = queue.popleft()
            if self.ending_unlock(node) and node != nid:
                path = []
                cur = key(node, state)
                while parent[cur] is not None:
                    prev, option = parent[cur]
                    path.append((cur[0], option))
                    cur = prev
                return list(reversed(path)), node
            for option, target in self.successors(node, state):
                nxt_state = self.enter(state, target)
                k = key(target, nxt_state)
                if k not in parent:
                    parent[k] = (key(node, state), option)
                    queue.append((target, nxt_state))
        raise Fail("no ending reachable in the graph model")


def number(text):
    return float(text)


def ids_of(records):
    """Profile unlock records: plain ids (cg, music) or {"id", "at"} objects (achievements)."""
    return {r["id"] if isinstance(r, dict) else r for r in records}


# ===========================================================================
# Engine wrapper (ctypes) + the player's start-up / input path
# ===========================================================================
def load_sdl():
    candidates = [os.environ.get("ROWL_SDL3_LIB", ""), ctypes.util.find_library("SDL3") or "",
                  "libSDL3.so.0", "libSDL3.dylib", "SDL3.dll"]
    for name in candidates:
        if not name:
            continue
        try:
            return ctypes.CDLL(name)
        except OSError:
            continue
    raise Skip("SDL3 shared library not found")


KEYS = {"enter": 0x0D, "esc": 0x1B, "space": 0x20, "bksp": 0x08, "up": 0x40000052,
        "down": 0x40000051, "left": 0x40000050, "right": 0x4000004F,
        "a": 0x61, "s": 0x73, "b": 0x62}
for _d in range(10):
    KEYS[str(_d)] = 0x30 + _d

SDL_KEY_DOWN, SDL_KEY_UP, SDL_MOUSE_DOWN, SDL_MOUSE_UP, SDL_WHEEL = 0x300, 0x301, 0x401, 0x402, 0x403
HANDLE = ctypes.c_void_p


class Engine:
    """One engine process-life: the same call order as engine/src/player/main.cpp."""

    def __init__(self, engine_lib, release, shots=None):
        self.lib = ctypes.CDLL(str(engine_lib))
        self.shots = pathlib.Path(shots) if shots else None
        self.sdl = None
        self.release = pathlib.Path(release)
        self.shot_index = 0
        self.shot_tag = ""
        self._bind()

    # -- ctypes plumbing ----------------------------------------------------
    def _fn(self, name, res, *args):
        fn = getattr(self.lib, name)
        fn.restype, fn.argtypes = res, list(args)
        return fn

    def _bind(self):
        c = ctypes
        f = self._fn
        self.Create = f("RowlEngine_Create", HANDLE)
        self.Init = f("RowlEngine_Init", c.c_int, HANDLE, c.c_uint32, c.c_uint32, c.c_int)
        self.InitStandalone = f("RowlEngine_InitStandalone", c.c_int, HANDLE, c.c_char_p,
                                c.c_uint32, c.c_uint32, c.c_int)
        self.SetProject = f("RowlEngine_SetProjectDirectory", None, HANDLE, c.c_char_p)
        self.SetQuickSlot = f("RowlEngine_SetQuickSaveSlot", c.c_int, HANDLE, c.c_int32)
        self.StepC = f("RowlEngine_Step", None, HANDLE, c.c_float)
        self.Shutdown = f("RowlEngine_Shutdown", None, HANDLE)
        self.Destroy = f("RowlEngine_Destroy", None, HANDLE)
        self.IsRunning = f("RowlEngine_IsRunning", c.c_int, HANDLE)
        self.MenuC = f("RowlEngine_GetPauseMenuJson", c.c_char_p, HANDLE)
        self.NodeC = f("RowlEngine_GetCurrentNodeId", c.c_uint64, HANDLE)
        self.QueryWorld = f("RowlEngine_QueryWorldJson", c.c_int, HANDLE, c.c_char_p, c.c_char_p, c.c_int)
        self.OpenTitle = f("RowlEngine_OpenTitleScreen", c.c_int32, HANDLE)
        self.SetPlay = f("RowlEngine_SetPlayState", None, HANDLE, c.c_int)
        self.IsTransition = f("RowlEngine_IsTransitionActive", c.c_int, HANDLE)
        self.IdentitySource = f("RowlEngine_GetGameIdentitySource", c.c_int32, HANDLE)
        self.TextScale = f("RowlEngine_GetTextScale", c.c_float, HANDLE)
        # offscreen/API backend (--shots)
        self.PixelBuf = f("RowlEngine_GetPixelBufferEx", c.c_void_p, HANDLE, c.POINTER(c.c_uint32),
                          c.POINTER(c.c_uint32), c.POINTER(c.c_uint32))
        self.PauseCmd = f("RowlEngine_PauseMenuCommand", None, HANDLE, c.c_int)
        self.SetPaused = f("RowlEngine_SetPaused", None, HANDLE, c.c_int)
        self.IsPaused = f("RowlEngine_IsPaused", c.c_int, HANDLE)
        self.PointerDown = f("RowlEngine_PointerDown", c.c_int, HANDLE, c.c_float, c.c_float)
        self.AdvanceC = f("RowlEngine_AdvanceNode", None, HANDLE, c.c_uint32)
        self.RewindC = f("RowlEngine_Rewind", c.c_int, HANDLE, c.c_uint32)
        self.OpenBacklogC = f("RowlEngine_OpenBacklog", c.c_int32, HANDLE)
        self.SetAutoC = f("RowlEngine_SetAutoMode", c.c_int32, HANDLE, c.c_int)
        self.SetSkipC = f("RowlEngine_SetSkipActive", c.c_int32, HANDLE, c.c_int)
        self.Flush = f("RowlEngine_ProfileFlush", c.c_int32, HANDLE)
        self.GetCaps = f("RowlEngine_GetCapabilities", c.c_int32, c.POINTER(c.c_uint64))
        # setters the player applies from the stored preferences
        self.SetVol = {k: f(n, None, HANDLE, c.c_float) for k, n in (
            ("master_volume", "RowlEngine_SetMasterVolume"), ("bgm_volume", "RowlEngine_SetBgmVolume"),
            ("voice_volume", "RowlEngine_SetVoiceVolume"), ("sfx_volume", "RowlEngine_SetSfxVolume"),
            ("ambience_volume", "RowlEngine_SetAmbienceVolume"), ("ui_volume", "RowlEngine_SetUiVolume"))}
        self.SetTextSpeed = f("RowlEngine_SetTextSpeedMultiplier", None, HANDLE, c.c_float)
        self.SetTextScale = f("RowlEngine_SetTextScale", None, HANDLE, c.c_float)
        self.SetAutoDelay = f("RowlEngine_SetAutoDelay", c.c_int32, HANDLE, c.c_float)
        self.SetSkipPolicy = f("RowlEngine_SetSkipPolicy", c.c_int32, HANDLE, c.c_int)
        self.SetLocale = f("RowlEngine_SetLocale", c.c_int32, HANDLE, c.c_char_p)
        self.text = {}
        for key, name, extra in (
                ("locale", "RowlEngine_GetLocale", ()), ("profile", "RowlEngine_ProfileGetJson", ()),
                ("profile_status", "RowlEngine_ProfileGetStatusJson", ()),
                ("extras", "RowlEngine_GetExtrasJson", ()),
                ("meta", "RowlEngine_GetSaveSlotMetadataJson", (c.c_int32,)),
                ("driver", "RowlEngine_GetPlayerDriverStateJsonUtf8", ()),
                ("history", "RowlEngine_GetDialogueHistoryJsonUtf8", ()),
                ("save_dir", "RowlEngine_GetSaveDirectoryUtf8", ()),
                ("profile_dir", "RowlEngine_GetProfileDirectoryUtf8", ()),
                ("data_dir", "RowlEngine_GetGameDataDirectoryUtf8", ()),
                ("game_id", "RowlEngine_GetGameIdUtf8", ())):
            fn = getattr(self.lib, name)
            fn.argtypes = [HANDLE, *extra, c.c_char_p, c.c_uint32, c.POINTER(c.c_uint32)]
            fn.restype = c.c_int32
            self.text[key] = fn

    def query(self, key, *pre):
        fn = self.text[key]
        required = ctypes.c_uint32(0)
        rc = fn(self.h, *pre, None, 0, ctypes.byref(required))
        if rc != 0:
            return None
        buf = ctypes.create_string_buffer(required.value + 1)
        rc = fn(self.h, *pre, buf, required.value, ctypes.byref(required))
        return buf.value.decode("utf-8") if rc == 0 else None

    def json(self, key, *pre):
        text = self.query(key, *pre)
        check(text is not None, f"C API {key} answers")
        return json.loads(text)

    # -- lifecycle ----------------------------------------------------------
    def boot(self, prefs=True):
        """RowlEngine_Create..SetProjectDirectory..stored preferences..title..Run's play state."""
        self.h = self.Create()
        check(bool(self.h), "engine handle allocated")
        self.api = self.shots is not None  # offscreen backend: input goes through the C API
        if self.api:
            ok = self.Init(self.h, 1920, 1080, 0)
        else:
            self.sdl = load_sdl()
            ok = self.InitStandalone(self.h, b"Kayip Tablo (D-K1)", 1920, 1080, 0)
        check(ok == 1, "engine initialised (standalone window, SDL dummy video)" if not self.api
              else "engine initialised (offscreen)")
        check(self.SetQuickSlot(self.h, 0) == 1, "quick-save slot 0 selected")
        self.SetProject(self.h, str(self.release).encode("utf-8"))
        if not self.api:
            self.sdl.SDL_GetWindows.restype = ctypes.POINTER(ctypes.c_void_p)
            self.sdl.SDL_GetWindowID.argtypes = [ctypes.c_void_p]
            self.sdl.SDL_GetWindowID.restype = ctypes.c_uint32
            self.sdl.SDL_PushEvent.argtypes = [ctypes.c_void_p]
            self.sdl.SDL_PushEvent.restype = ctypes.c_bool
            count = ctypes.c_int(0)
            windows = self.sdl.SDL_GetWindows(ctypes.byref(count))
            check(count.value == 1, "one engine window exists", str(count.value))
            self.wid = self.sdl.SDL_GetWindowID(windows[0])
        self.profile_path = pathlib.Path(self.json("profile_status")["path"])
        self.applied = self.apply_stored_preferences() if prefs else []
        check(self.OpenTitle(self.h) == 0, "title screen opens before the story")
        self.SetPlay(self.h, 1)  # RowlEngine_Run's first act
        self.step(1 / 60, 2)

    def apply_stored_preferences(self):
        """Mirror of applyStoredPreferences (engine/src/player/main.cpp) for the keys the journey uses."""
        status = self.json("profile_status")
        if status.get("status") != "loaded":
            return []
        prefs = self.json("profile")
        applied = []
        for key, setter in self.SetVol.items():
            if isinstance(prefs.get(key), (int, float)):
                setter(self.h, prefs[key])
                applied.append(key)
        if isinstance(prefs.get("text_speed_multiplier"), (int, float)):
            self.SetTextSpeed(self.h, prefs["text_speed_multiplier"])
            applied.append("text_speed_multiplier")
        if isinstance(prefs.get("auto_advance_delay"), (int, float)):
            self.SetAutoDelay(self.h, prefs["auto_advance_delay"])
            applied.append("auto_advance_delay")
        if prefs.get("auto_enabled"):
            self.SetAutoC(self.h, 1)
            applied.append("auto_enabled")
        mode = prefs.get("skip_mode", "read_only")
        self.SetSkipPolicy(self.h, 0 if mode == "off" else (2 if mode == "all" else 1))
        if isinstance(prefs.get("text_scale"), (int, float)):
            self.SetTextScale(self.h, prefs["text_scale"])
            applied.append("text_scale")
        language = prefs.get("language", "")
        if language and language != self.query("locale"):
            self.SetLocale(self.h, language.encode())
            applied.append("language")
        return applied

    def close(self):
        self.step(1 / 60, 2)
        self.Shutdown(self.h)
        self.Destroy(self.h)

    # -- time + input -------------------------------------------------------
    def step(self, dt=1 / 60, count=1):
        for _ in range(count):
            self.StepC(self.h, dt)

    def _push(self, fmt, *values):
        raw = bytearray(128)
        struct.pack_into(fmt, raw, 0, *values)
        buf = (ctypes.c_char * 128).from_buffer(raw)
        check(self.sdl.SDL_PushEvent(ctypes.byref(buf)), "synthetic SDL event queued")

    def press(self, name, settle=2):
        """One real key press + release (SDL_EVENT_KEY_DOWN/UP), then a couple of frames."""
        if self.api:
            return self._press_api(name, settle)
        code = KEYS[name]
        self._push("<IIQIIIIHHBB", SDL_KEY_DOWN, 0, 0, self.wid, 0, 0, code, 0, 0, 1, 0)
        self.step(1 / 60, settle)
        self._push("<IIQIIIIHHBB", SDL_KEY_UP, 0, 0, self.wid, 0, 0, code, 0, 0, 0, 0)
        self.step(1 / 60, settle)

    def click(self, x, y):
        """Left click at window coordinates (the window is the 1920x1080 virtual canvas)."""
        if self.api:
            self.PointerDown(self.h, x, y)
            self.step(1 / 60, 2)
            return
        for kind, down in ((SDL_MOUSE_DOWN, 1), (SDL_MOUSE_UP, 0)):
            self._push("<IIQIIBBBBff", kind, 0, 0, self.wid, 0, 1, down, 1, 0, x, y)
            self.step(1 / 60, 2)

    def wheel(self, dy):
        if self.api:
            self.OpenBacklogC(self.h)
            self.step(1 / 60, 2)
            return
        self._push("<IIQIIffI", SDL_WHEEL, 0, 0, self.wid, 0, 0.0, float(dy), 0)
        self.step(1 / 60, 2)

    def _press_api(self, name, settle):
        """Offscreen backend (shots only): the C API equivalent of each key."""
        menu = self.menu()
        paused = bool(menu.get("open"))
        if name in ("up", "down", "left", "right"):
            self.PauseCmd(self.h, {"up": 0, "down": 1, "left": 2, "right": 3}[name])
        elif name == "enter":
            if paused:
                self.PauseCmd(self.h, 5)
            else:
                self.AdvanceC(self.h, 0)
        elif name == "esc":
            if menu.get("title_active") or menu.get("mode") in (4, 5, 6, 7, 8, 9, 1, 2):
                self.PauseCmd(self.h, 4)
            else:
                self.SetPaused(self.h, 0 if paused else 1)
        elif name == "bksp":
            if not paused:
                self.RewindC(self.h, 1)
        elif name == "b":
            if menu.get("mode") == 5:
                self.PauseCmd(self.h, 4)
            elif not paused:
                self.OpenBacklogC(self.h)
        elif name == "a":
            self.SetAutoC(self.h, 0 if self.driver()["auto"] else 1)
        elif name == "s":
            self.SetSkipC(self.h, 0 if self.driver()["skip_active"] else 1)
        elif name in "0123456789":
            # slot pages: rows are slots 0-9 in order
            for _ in range(10):
                if self.menu()["selected"] == int(name):
                    break
                self.PauseCmd(self.h, 1)
            self.PauseCmd(self.h, 5)
        self.step(1 / 60, settle * 2)

    # -- observation ----------------------------------------------------------
    def menu(self):
        return json.loads(self.MenuC(self.h).decode("utf-8"))

    def world(self):
        size = abs(self.QueryWorld(self.h, None, None, 0))
        buf = ctypes.create_string_buffer(size + 1)
        self.QueryWorld(self.h, None, buf, size + 1)
        return json.loads(buf.value)

    def node(self):
        return int(self.NodeC(self.h))

    def vars(self):
        return {k: number(v) for k, v in self.world()["vars"].items()}

    def driver(self):
        return self.json("driver")

    def profile(self):
        return self.json("profile")

    def extras(self):
        return self.json("extras")

    def history(self):
        return self.json("history")

    def settle(self):
        """Let the typewriter finish and any transition end (simulated time)."""
        text_len = len(self.world()["story"]["dlg"])
        self.step(0.25, int(text_len / 32 / 0.25) + 4)
        for _ in range(200):
            if not self.IsTransition(self.h):
                break
            self.step(0.1)

    def select_row(self, index):
        for _ in range(16):
            if self.menu()["selected"] == index:
                return
            self.press("down")
        check(self.menu()["selected"] == index, f"menu row {index} reachable")

    def menu_rows(self):
        return [(r["label"], r["value"]) for r in self.menu()["rows"]]

    def choices(self):
        return self.world()["story"]["choices"]

    def click_choice(self, option_id):
        for choice in self.choices():
            if choice["id"] == option_id:
                b = choice["bounds"]
                self.click(b["x"] + b["w"] / 2, b["y"] + b["h"] / 2)
                return
        raise Fail(f"choice {option_id!r} not on screen: {[c['id'] for c in self.choices()]}")

    # -- pictures ----------------------------------------------------------
    def shot(self, name, settle=False):
        """PNG of the offscreen frame (--shots only); `settle` lets the line type out first."""
        if not self.shots:
            return
        if settle:
            self.settle()
        self.shots.mkdir(parents=True, exist_ok=True)
        w, h, pitch = ctypes.c_uint32(), ctypes.c_uint32(), ctypes.c_uint32()
        self.step(1 / 60, 3)
        ptr = self.PixelBuf(self.h, ctypes.byref(w), ctypes.byref(h), ctypes.byref(pitch))
        if not ptr:
            return
        raw = ctypes.string_at(ptr, pitch.value * h.value)
        rows = b"".join(b"\x00" + raw[y * pitch.value:y * pitch.value + w.value * 4] for y in range(h.value))

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w.value, h.value, 8, 6, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))
        self.shot_index += 1
        path = self.shots / f"{self.shot_tag}{self.shot_index:02d}_{name}.png"
        path.write_bytes(png)
        print(f"[SHOT] {path}", flush=True)


# ===========================================================================
# Shared journey pieces
# ===========================================================================
def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def profile_file(eng):
    return eng.profile_path


def disk_profile(eng):
    return json.loads(profile_file(eng).read_text(encoding="utf-8"))


def expect_vars(eng, expected, name):
    actual = eng.vars()
    ok = all(abs(actual.get(k, 0.0) - v) < 1e-6 for k, v in expected.items()) and set(actual) == set(expected)
    check(ok, f"{name}: variables follow the graph", f"expected {expected} got {actual}")


def step_forward(eng, model, state, expect_node=None):
    """Settle, press Enter (the player's advance key) and verify the model's next node."""
    cur = state["node"]
    nxt = expect_node if expect_node is not None else model.linear_next(cur)
    eng.settle()
    eng.press("enter")
    check(eng.node() == nxt, f"Enter advances node {cur} -> {nxt}", f"engine is on node {eng.node()}")
    state["vars"] = model.enter(state["vars"], nxt)
    state["node"] = nxt
    state["entered"].append(nxt)
    state["path"].append(nxt)
    world = eng.world()
    check(world["story"]["dlg"] == model.text(nxt), f"node {nxt}: the line shown is the graph's line",
          f"{world['story']['dlg']!r} vs {model.text(nxt)!r}")
    expect_vars(eng, state["vars"], f"node {nxt}")


def walk_to_choice(eng, model, state):
    while not model.is_choice(state["node"]):
        step_forward(eng, model, state)
    ids = [o["option_id"] for o in model.options(state["node"])]
    shown = [c["id"] for c in eng.choices()]
    check(shown == ids, f"node {state['node']}: the choice buttons are the graph's options", f"{shown} vs {ids}")


def pick_with_keyboard(eng, options, option, digit=False):
    """D-K3: answer a choice menu from the keyboard. No option is focused when the menu
    opens; the first Down focuses the first selectable one and Down walks the rest (or digit n picks the n-th);
    Enter picks the focused one. (The offscreen shots backend has no key path: click.)"""
    ids = [o["option_id"] for o in options]
    if eng.api:
        eng.click_choice(option["option_id"])
        return
    focused = [c["id"] for c in eng.choices() if c.get("focused")]
    check(focused == [], "choice menu: it opens without a focus (Enter/Space mashing must not pick)",
          f"focused {focused}, selectable {ids}")
    if digit:
        eng.press(str(ids.index(option["option_id"]) + 1))
        return
    for _ in range(len(ids)):
        eng.press("down")  # the first Down focuses the first selectable option
        focused = [c["id"] for c in eng.choices() if c.get("focused")]
        check(len(focused) == 1, "exactly one choice option holds the focus", f"{focused}")
        if focused[0] == option["option_id"]:
            break
    check([c["id"] for c in eng.choices() if c.get("focused")] == [option["option_id"]],
          "Down moved the focus onto the wanted option")
    eng.press("enter")


def choose(eng, model, state, option_id=None, keyboard=None):
    """Click a choice button (keyboard='arrows' / 'digit': use the keyboard instead).
    Default: the first option whose condition holds."""
    options = model.selectable(state["node"], state["vars"])
    option = next(o for o in options if option_id in (None, o["option_id"]))
    eng.settle()
    if keyboard:
        pick_with_keyboard(eng, options, option, digit=(keyboard == "digit"))
    else:
        eng.click_choice(option["option_id"])
    target = option["target_node_id"]
    check(eng.node() == target, f"{keyboard or 'clicking'} {option['option_id']!r} leads to node {target}",
          f"engine is on node {eng.node()}")
    state["vars"] = model.enter(state["vars"], target)
    state["node"] = target
    state["entered"].append(target)
    state["path"].append(target)
    expect_vars(eng, state["vars"], f"after choice {option['option_id']}")
    return option["option_id"]


def pause_menu(eng):
    eng.press("esc")
    menu = eng.menu()
    check(menu["open"] and menu["mode"] == 0, "Esc opens the pause menu")
    return menu


def close_menus(eng, limit=4):
    for _ in range(limit):
        if not eng.menu()["open"]:
            return
        eng.press("esc")
    check(not eng.menu()["open"], "menus close back to the story")


def quit_through_menu(eng, from_title=False):
    """Çıkış needs a second confirmation; a stray Enter never quits."""
    if not from_title:
        pause_menu(eng)
        eng.select_row(6)
    else:
        eng.select_row(5)
    check(eng.menu()["rows"][eng.menu()["selected"]]["label"] == "Çıkış", "the Exit row is selected")
    eng.press("enter")
    check(eng.menu()["confirm_quit"] and eng.IsRunning(eng.h) == 1,
          "first Exit confirmation only arms the quit; the game keeps running")
    eng.press("enter")
    check(eng.IsRunning(eng.h) == 0, "second Exit confirmation ends the run loop")


def save_to_slot(eng, slot, save_dir):
    pause_menu(eng)
    eng.select_row(1)
    check(eng.menu()["rows"][1]["label"] == "Oyunu Kaydet", "pause menu offers Oyunu Kaydet")
    eng.press("enter")
    check(eng.menu()["mode"] == 1, "the save page opens (slot list)")
    check(eng.menu()["rows"][slot]["value"] == "boş", f"slot {slot} is empty before the save")
    eng.press(str(slot))
    rows = eng.menu()["rows"]
    check(rows[slot]["value"] == "dolu" and rows[slot]["detail"], f"slot {slot} reads 'dolu' with date + summary",
          str(rows[slot]))
    close_menus(eng)
    path = pathlib.Path(save_dir) / f"save_slot_{slot}.json"
    check(path.is_file(), f"the save file {path.name} is on disk")
    return path


def load_from_slot(eng, slot):
    pause_menu(eng)
    eng.select_row(2)
    check(eng.menu()["rows"][2]["label"] == "Oyunu Yükle", "pause menu offers Oyunu Yükle")
    eng.press("enter")
    check(eng.menu()["mode"] == 2, "the load page opens (slot list)")
    eng.press(str(slot))
    check(eng.menu()["open"] and eng.menu()["mode"] == 2,
          f"choosing slot {slot} loads it; the pause menu stays on the slot page (by design)")
    close_menus(eng)


def parse_utc(stamp):
    return datetime.datetime.strptime(stamp, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


def load_state(ctx):
    return json.loads(pathlib.Path(ctx["state"]).read_text(encoding="utf-8"))


def save_state(ctx, state):
    pathlib.Path(ctx["state"]).write_text(json.dumps(state), encoding="utf-8")


def new_state():
    return {"node": 0, "vars": {}, "entered": [], "path": []}


# ===========================================================================
# Phases
# ===========================================================================
def phase_play(ctx, model, eng):
    eng.shot_tag = "p1_"
    eng.boot()
    caps = ctypes.c_uint64(0)
    check(eng.GetCaps(ctypes.byref(caps)) == 0 and caps.value != 0, "engine reports its capabilities")
    # --- 1. the package opens on the title screen --------------------------
    check(eng.query("game_id") == model.project["project_uuid"], "game identity comes from the package")
    check(eng.IdentitySource(eng.h) == 1, "identity source is the package")
    data_dir = pathlib.Path(eng.query("data_dir"))
    check(pathlib.Path(ctx["xdg"]) in data_dir.parents, "per-game data lives in the player's data dir")
    check(eng.json("profile_status")["status"] == "missing", "a fresh install has no player profile yet")
    menu = eng.menu()
    check(menu["mode"] == 3 and menu["title_active"] and menu["title"] == model.project["name"],
          "title screen is up with the game name")
    check([r["label"] for r in menu["rows"]] ==
          ["Yeni Oyun", "Devam Et", "Oyunu Yükle", "Ekstralar", "Tercihler", "Çıkış"],
          "title rows: Yeni Oyun / Devam Et / Oyunu Yükle / Ekstralar / Tercihler / Çıkış")
    check(menu["rows"][1]["value"] == "kayıt yok" and menu["rows"][1]["enabled"] is False
          and menu["continue_slot"] == -1 and menu["selected"] == 0,
          "Devam Et reads 'kayıt yok' (disabled), Yeni Oyun is preselected")
    eng.shot("title_fresh")
    # --- 2. New Game -----------------------------------------------------
    eng.press("enter")
    check(eng.menu()["open"] is False and eng.node() == model.start, "Yeni Oyun starts at the graph's first node")
    state = new_state()
    state.update(node=model.start, entered=[model.start], path=[model.start],
                 vars=model.enter({}, model.start))
    expect_vars(eng, state["vars"], "first node")
    check(len(eng.history()) == 1 and eng.history()[0]["node_id"] == model.start,
          "the history begins with the first line")
    read_ids = set(eng.profile()["read_content_ids"])
    check(read_ids == set(model.content_ids(model.start)), "the first line is marked read", str(read_ids))
    eng.shot("story_first_node", settle=True)
    # --- 3. first and second choice -----------------------------------------
    walk_to_choice(eng, model, state)
    state["choice1_node"] = state["node"]
    eng.shot("first_choice", settle=True)
    state["choice1"] = choose(eng, model, state)
    walk_to_choice(eng, model, state)
    check(state["node"] != state["choice1_node"], "the second choice is a different node")
    state["choice2_node"] = state["node"]
    # D-K3: the second choice is answered from the keyboard (arrows + Enter).
    state["choice2"] = choose(eng, model, state, keyboard="arrows")
    for _ in range(2):
        step_forward(eng, model, state)
    # --- 4. save ---------------------------------------------------------------
    save_dir = eng.query("save_dir")
    step_id = eng.world()["stepId"]
    slot_file = save_to_slot(eng, 1, save_dir)
    eng.shot("saved")
    meta = eng.json("meta", 1)
    saved_at = parse_utc(meta["saved_at"])
    check(abs((datetime.datetime.now(datetime.timezone.utc) - saved_at).total_seconds()) < 600,
          "metadata: saved_at is the real UTC save time", meta["saved_at"])
    check(meta["summary"] and model.text(state["node"]).startswith(meta["summary"].rstrip("…").rstrip(".")[:30]),
          "metadata: the summary is the line on screen", meta["summary"])
    check(meta["playtime_seconds"] > 5.0, "metadata: playtime counts the simulated time", str(meta["playtime_seconds"]))
    check(meta["slot"] == 1, "metadata: slot index")
    check(meta["chapter_id"] == "" and meta["chapter_title"] == "",
          "metadata: kayip_tablo declares no chapters, so chapter fields are empty")
    check(eng.json("profile_status")["status"] in ("missing", "loaded"), "profile readable while playing")
    state["save"] = {"node": state["node"], "vars": dict(state["vars"]), "step_id": step_id,
                     "hash": sha(slot_file), "summary": meta["summary"], "path_len": len(state["path"]),
                     "path": list(state["path"]), "meta_summary": meta["summary"]}
    # --- read on past the save: this region is "read" when the game is resumed --
    for _ in range(3):
        step_forward(eng, model, state)
    state["read_ahead"] = state["path"][state["save"]["path_len"]:]
    check(sha(slot_file) == state["save"]["hash"], "playing on does not touch the saved slot")
    eng.shot("read_on", settle=True)
    # --- 5. quit through the menu, profile + save must be on disk ---------------
    quit_through_menu(eng)
    eng.close()
    disk = disk_profile(eng)
    expected_read = {cid for nid in state["path"] for cid in model.content_ids(nid)}
    check(set(disk["read_content_ids"]) == expected_read,
          "quit flushed the profile: every line shown is marked read", f"{len(disk['read_content_ids'])} vs {len(expected_read)}")
    want = model.unlocks_of(state["entered"])
    check(set(disk["unlocks"]["cg"]) == want["cg"] and set(disk["unlocks"]["music"]) == want["music"],
          "quit flushed the profile: seen backgrounds / music are unlocked", f"{disk['unlocks']} vs {want}")
    state["read_all_so_far"] = sorted(expected_read)
    check(sha(slot_file) == state["save"]["hash"], "the save file is byte-identical after quitting")
    save_state(ctx, state)


def phase_resume(ctx, model, eng):
    eng.shot_tag = "p2_"
    state = load_state(ctx)
    eng.boot()
    save_dir = eng.query("save_dir")
    slot_file = pathlib.Path(save_dir) / "save_slot_1.json"
    check(sha(slot_file) == state["save"]["hash"], "restart: the save survived the process")
    # --- 6a. title: Continue is ready ------------------------------------------
    menu = eng.menu()
    check(menu["mode"] == 3 and menu["rows"][1]["enabled"] and menu["rows"][1]["value"].startswith("Yuva 1")
          and menu["continue_slot"] == 1 and menu["selected"] == 1,
          "restart: Devam Et names slot 1 and is preselected", str(menu["rows"][1]))
    eng.shot("title_with_save")
    # --- 8. preferences, from the title (no pause menu) ---------------------------
    before = eng.profile()
    check(before["text_speed_multiplier"] == 1.0 and before["text_scale"] == 1.0,
          "preferences start at the defaults")
    eng.select_row(4)
    eng.press("enter")
    check(eng.menu()["mode"] == 4, "Tercihler opens from the title")
    labels = [r["label"] for r in eng.menu()["rows"]]
    speed_row, size_row, lang_row = labels.index("Metin Hızı"), labels.index("Metin Boyutu"), labels.index("Dil")
    eng.select_row(speed_row)
    eng.press("right")
    eng.press("right")
    check(eng.menu_rows()[speed_row][1] == "1.50x", "Metin Hızı reads 1.50x", str(eng.menu_rows()[speed_row]))
    eng.select_row(size_row)
    eng.press("right")
    eng.press("right")
    check(eng.menu_rows()[size_row][1] == "150%", "Metin Boyutu reads 150%", str(eng.menu_rows()[size_row]))
    eng.select_row(lang_row)
    language_before = eng.menu_rows()[lang_row]
    eng.press("right")
    check(eng.menu_rows()[lang_row] == language_before and eng.profile()["language"] == "",
          "Dil: a single-language game offers nothing to switch to, the profile keeps the game's language")
    eng.shot("preferences")
    prof = eng.profile()
    check(prof["text_speed_multiplier"] == 1.5 and prof["text_scale"] == 1.5,
          "the profile holds the new text speed and text size", str((prof["text_speed_multiplier"], prof["text_scale"])))
    eng.step(0.25, 12)  # the profile writes itself ~2 s after the first change
    disk = disk_profile(eng)
    check(disk["text_speed_multiplier"] == 1.5 and disk["text_scale"] == 1.5,
          "the preferences reached the disk by themselves (auto flush)")
    eng.press("esc")
    check(eng.menu()["mode"] == 3 and eng.menu()["title_active"], "Geri/Esc returns to the title")
    # --- 6b. Continue -------------------------------------------------------------
    eng.select_row(1)
    eng.press("enter")
    check(eng.menu()["open"] is False and eng.node() == state["save"]["node"],
          "Devam Et loads the saved node", f"engine node {eng.node()} saved {state['save']['node']}")
    expect_vars(eng, state["save"]["vars"], "continued game")
    check(eng.world()["stepId"] == state["save"]["step_id"], "continued game: the step counter is restored")
    check(eng.world()["story"]["dlg"] == model.text(state["save"]["node"]), "continued game shows the saved line")
    check(sha(slot_file) == state["save"]["hash"], "loading does not rewrite the slot")
    hist = eng.history()
    saved_path = state["save"]["path"]
    check([h["node_id"] for h in hist] == saved_path and
          [h["dialogue"] for h in hist] == [model.text(n) for n in saved_path],
          "the history was restored line by line", f"{len(hist)} rows")
    meta = eng.json("meta", 1)
    check(meta["summary"] == state["save"]["meta_summary"] and meta["chapter_id"] == "",
          "the slot metadata still matches what was saved")
    eng.shot("continued", settle=True)
    # --- 7. history (B key, wheel) ----------------------------------------------------
    def snapshot():
        return (eng.node(), eng.world()["stepId"], eng.vars() and sorted(eng.vars().items()), len(eng.history()),
                tuple(sorted(eng.profile()["read_content_ids"])), sha(slot_file))

    snap = snapshot()
    eng.press("b")
    menu = eng.menu()
    check(menu["mode"] == 5, "B opens the history (Geçmiş)")
    backlog = menu["backlog"]
    check(backlog["total"] == len(saved_path) and backlog["selected"] == backlog["total"] - 1,
          "history lists every line, the newest selected", f"{backlog['total']} {backlog['selected']}")
    window = backlog["entries"]
    for entry in window:
        check(entry["dialogue"] == model.text(saved_path[entry["index"]]),
              f"history row {entry['index']} is the graph's line for node {saved_path[entry['index']]}")
    eng.shot("history")
    for _ in range(3):
        eng.press("up")
    check(eng.menu()["backlog"]["selected"] == backlog["total"] - 4, "Up walks back through the lines")
    voiced = [e for e in eng.menu()["backlog"]["entries"] if e["has_voice"]]
    check(not voiced, "no line of this sample carries a voice clip (replay has nothing to play)")
    eng.press("enter")  # replay on a row without voice must change nothing
    eng.press("b")
    check(not eng.menu()["open"], "B closes the history")
    check(snapshot() == snap, "browsing the history changes no node, step, variable, history, read mark or save")
    eng.wheel(1.0)
    check(eng.menu()["mode"] == 5, "mouse wheel up also opens the history")
    eng.press("esc")
    check(not eng.menu()["open"] and snapshot() == snap, "Esc closes it again, nothing changed")
    # --- quit through the pause menu --------------------------------------------------
    quit_through_menu(eng)
    eng.close()
    final = disk_profile(eng)
    check(final["text_speed_multiplier"] == 1.5 and final["text_scale"] == 1.5,
          "preferences are on disk after quitting")
    check(set(final["read_content_ids"]) >= set(state["read_all_so_far"]), "read marks kept")
    check(sha(slot_file) == state["save"]["hash"], "the save is byte-identical after the second session")


def phase_player(ctx, model, eng):
    """Third start: the real player binary applies the stored preferences itself."""
    state = load_state(ctx)
    env = dict(os.environ, XDG_DATA_HOME=ctx["xdg"], LOCALAPPDATA=ctx["xdg"],
               SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
    release = pathlib.Path(ctx["release"])
    player = release / ("RowlGame.exe" if os.name == "nt" else "RowlGame")
    slot_file = None
    # find the save through the engine-reported layout (print-game-identity)
    ident = subprocess.run([str(player), "--project", str(release), "--print-game-identity"],
                           capture_output=True, text=True, env=env, timeout=60, cwd=str(release))
    print(ident.stdout, end="")
    print(ident.stderr, end="", file=sys.stderr)
    line = next(l for l in ident.stdout.splitlines() if l.startswith('{"'))
    info = json.loads(line)
    slot_file = pathlib.Path(info["save_dir"]) / "save_slot_1.json"
    profile_path = pathlib.Path(info["profile"]["path"])
    before = json.loads(profile_path.read_text(encoding="utf-8"))
    check(slot_file.is_file() and sha(slot_file) == state["save"]["hash"], "the real player sees the saved game")
    check(info["profile"]["status"] == "loaded" and info["profile"]["read_count"] == len(before["read_content_ids"]),
          "the real player loads the profile of the earlier sessions")
    smoke = subprocess.run([str(player), "--project", str(release), "--package-smoke-test"],
                           capture_output=True, text=True, env=env, timeout=120, cwd=str(release))
    print(smoke.stdout, end="")
    print(smoke.stderr, end="", file=sys.stderr)
    check(smoke.returncode == 0 and "Package smoke frame rendered" in smoke.stdout, "the real player starts and renders",
          smoke.stdout[-400:])
    applied = next((l for l in smoke.stdout.splitlines() if "[Player] Applied stored preferences:" in l), "")
    check("text_speed_multiplier=1.5" in applied and "text_scale=1.5" in applied,
          "restart: the real player applied the new text speed and text size", applied)
    check("Package smoke title: ok continue_slot=1" in smoke.stdout,
          "the real player's title offers Continue for slot 1")
    after = json.loads(profile_path.read_text(encoding="utf-8"))
    check(after["text_speed_multiplier"] == 1.5 and after["text_scale"] == 1.5,
          "the player's smoke leaves the player's preferences alone")
    check(set(after["read_content_ids"]) >= set(before["read_content_ids"])
          and set(after["unlocks"]["cg"]) >= set(before["unlocks"]["cg"]), "no progress was lost")
    check(sha(slot_file) == state["save"]["hash"], "the save is untouched by the real player's start-up")


def phase_playon(ctx, model, eng):
    eng.shot_tag = "p4_"
    state = load_state(ctx)
    eng.boot()
    prefs = eng.profile()
    check("text_speed_multiplier" in eng.applied and "text_scale" in eng.applied,
          "restart: the stored preferences are applied before the first frame")
    check(abs(eng.TextScale(eng.h) - 1.5) < 1e-6, "restart: the engine's text size is the stored 150%")
    save_dir = eng.query("save_dir")
    slot_file = pathlib.Path(save_dir) / "save_slot_1.json"
    read_before = set(prefs["read_content_ids"])
    eng.select_row(1)
    eng.press("enter")
    saved = state["save"]
    check(eng.node() == saved["node"], "Continue returns to the saved node")
    cur = dict(new_state(), node=saved["node"], vars=dict(saved["vars"]),
               entered=state["entered"], path=list(saved["path"]))
    # --- a mid-session load, then back into the very node the player had just left ----------------
    # (the history keeps one row per node entry; the one-shot "already recorded" marker must follow
    # a load, or the node reached right after it is dropped from the backlog)
    step_forward(eng, model, cur)
    first_after_save = cur["node"]
    load_from_slot(eng, 1)
    check(eng.node() == saved["node"], "Oyunu Yükle returns to the saved node mid-session")
    cur.update(node=saved["node"], vars=dict(saved["vars"]), path=list(saved["path"]))
    step_forward(eng, model, cur, expect_node=first_after_save)
    check([h["node_id"] for h in eng.history()] == cur["path"],
          "after a mid-session load, walking back into the same node records its line once",
          f"{[h['node_id'] for h in eng.history()][-6:]} vs {cur['path'][-6:]}")
    # --- 9a. Skip (read lines only) -----------------------------------------------------
    check(eng.driver()["skip_policy"] == "read_only" and not eng.driver()["skip_active"], "Skip policy is 'read only'")
    walked = []
    node = cur["node"]
    while True:
        node = model.linear_next(node)
        walked.append(node)
        if not set(model.content_ids(node)) <= read_before or model.is_choice(node):
            break
    expected_stop = walked[-1]
    check(set(walked[:-1]) | {first_after_save} == set(state["read_ahead"])
          and len(walked) + 1 == len(state["read_ahead"]) + 1,
          "model: the read region after the save is exactly what was read on in the first session",
          f"{walked} vs {state['read_ahead']}")
    eng.press("s")
    check(eng.driver()["skip_active"], "S starts Skip")
    for _ in range(200):
        eng.step(0.05)
        if not eng.driver()["skip_active"]:
            break
    check(not eng.driver()["skip_active"] and eng.node() == expected_stop,
          f"Skip passes the read lines and stops on the first unread one (node {expected_stop})",
          f"engine node {eng.node()}")
    for nid in walked:
        cur["path"].append(nid)
        cur["entered"].append(nid)
        cur["vars"] = model.enter(cur["vars"], nid)
    cur["node"] = expected_stop
    expect_vars(eng, cur["vars"], "after Skip")
    check(eng.driver()["line_read_before"] is False, "the unread line is reported as not read before")
    eng.shot("skip_stopped")
    # --- 9b. Auto: waits delay + characters * 0.04 / speed ------------------------------------
    speed = prefs["text_speed_multiplier"]
    delay = prefs["auto_advance_delay"]
    eng.press("a")
    # (the A key remembers Auto in the profile; the offscreen C API backend of --shots has no such key)
    check(eng.driver()["auto"] and (eng.api or eng.profile()["auto_enabled"]), "A turns Auto on (and remembers it)")
    here = eng.node()
    text = model.text(here)
    # The line types out first (graph text_speed x the player's multiplier), then Auto waits
    # delay + characters * 0.04 / multiplier (engine/include/rowl/c_api.h, D04-1).
    typing = len(text) / (next(model.comps(here, "dialogue"))["text_speed"] * speed)
    wait = typing + min(30.0, max(0.8, delay + len(text) * 0.04 / speed))
    elapsed = 0.0
    while eng.node() == here and elapsed < 40.0:
        eng.step(0.05)
        elapsed += 0.05
    nxt = model.linear_next(here)
    check(eng.node() == nxt, "Auto advances by itself", f"node {eng.node()}")
    check(abs(elapsed - wait) <= 0.35, f"Auto advances after typing + delay + chars*0.04/speed = {wait:.2f}s for {len(text)} characters (took {elapsed:.2f}s)")
    cur["node"] = nxt
    cur["path"].append(nxt)
    cur["entered"].append(nxt)
    cur["vars"] = model.enter(cur["vars"], nxt)
    eng.shot("auto", settle=True)
    eng.press("a")
    check(not eng.driver()["auto"] and not eng.profile()["auto_enabled"], "A turns Auto off again")
    eng.step(0.25, 40)
    check(eng.node() == nxt, "with Auto off the line stays put")
    # --- 10. Rewind: no side effect replays -----------------------------------------------------
    walk_to_choice(eng, model, cur)
    rewind_node = cur["node"]
    option = model.selectable(cur["node"], cur["vars"])[0]
    before_vars = dict(cur["vars"])
    hist_before = len(eng.history())
    eng.settle()
    eng.click_choice(option["option_id"])
    target = option["target_node_id"]
    check(eng.node() == target, "choice taken before the rewind")
    after_vars = model.enter(before_vars, target)
    expect_vars(eng, after_vars, "after the choice")
    check(after_vars != before_vars, "the choice node has a variable side effect to watch")
    hist_after = len(eng.history())
    presses = 0
    stepid = eng.world()["stepId"]
    while eng.node() != rewind_node and presses < 6:
        eng.press("bksp")
        presses += 1
    check(eng.node() == rewind_node, "Backspace rewinds to the choice screen",
          f"engine node {eng.node()} after {presses} presses")
    note(f"rewind: {presses} Backspace press(es) to leave node {target} (steps {stepid} -> {eng.world()['stepId']})")
    expect_vars(eng, before_vars, "after the rewind the variables are the earlier ones")
    check(len(eng.history()) == hist_before, "after the rewind the history is the earlier one",
          f"{len(eng.history())} rows vs {hist_before}")
    check(bool(eng.choices()), "the rewound choice screen offers the buttons again")
    eng.settle()
    eng.click_choice(option["option_id"])
    check(eng.node() == target, "taking the same choice again reaches the same node")
    expect_vars(eng, after_vars, "the side effect applies once, not twice")
    check(len(eng.history()) == hist_after, "the history grows by the same single line, no duplicates",
          f"{len(eng.history())} rows vs {hist_after}; before the choice {hist_before}")
    cur["vars"] = after_vars
    cur["node"] = target
    cur["path"].append(target)
    cur["entered"].append(target)
    eng.shot("rewound", settle=True)
    # --- 11. the shortest way to an ending, achievement + notice queue ---------------------------
    plan, ending = model.plan_to_ending(cur["node"], cur["vars"])
    choice_ids = [opt for _, opt in plan if opt]
    note(f"shortest route to an ending: {len(plan)} nodes, {len(choice_ids)} choices -> node {ending}")
    goal = set(model.ending_unlock(ending))
    scripted_checked = False
    scripted_digit = False
    for node, option_id in plan:
        if model.is_choice(cur["node"]):
            eng.settle()
            # D-K3: the route's first choice is picked with its digit key, the rest by click.
            if not scripted_digit:
                scripted_digit = True
                options = model.selectable(cur["node"], cur["vars"])
                pick_with_keyboard(eng, options, next(o for o in options if o["option_id"] == option_id),
                                   digit=True)
            else:
                eng.click_choice(option_id)
            check(eng.node() == node, f"route: {option_id!r} -> node {node}", f"engine node {eng.node()}")
            cur["vars"] = model.enter(cur["vars"], node)
            cur["node"] = node
            cur["path"].append(node)
            cur["entered"].append(node)
            expect_vars(eng, cur["vars"], f"route node {node}")
        else:
            step_forward(eng, model, cur, expect_node=node)
        if list(model.comps(node, "script")) and not scripted_checked:
            scripted_checked = True
            check_scripted_node(eng, model, cur, node)
        if node == ending:
            break
        if model.is_choice(node):
            ids = [c["id"] for c in eng.choices()]
            allowed = [o["option_id"] for o in model.selectable(node, cur["vars"])]
            check(set(allowed) <= set(ids), f"node {node}: selectable options are on screen", f"{ids}")
    check(eng.node() == ending and goal, f"the route ends on node {ending}")
    achievement = next(iter(goal))
    extras = eng.extras()
    notice_seen = (extras["toast"] and extras["toast"]["id"] == achievement) or \
        any(n["id"] == achievement for n in extras["pending_notices"])
    check(notice_seen, "ending: the achievement notice is queued or on screen", json.dumps(extras["toast"]))
    eng.step(1 / 30, 3)
    toast = eng.extras()["toast"]
    check(toast is not None and toast["kind"] == "achievement" and toast["id"] == achievement and toast["visible"],
          "ending: the achievement toast is showing", str(toast))
    check(eng.extras()["pending_notices"] == [], "ending: the notice queue is drained once the toast shows")
    eng.shot("achievement_toast")
    eng.step(0.25, int(toast["remaining_seconds"] / 0.25) + 8)
    after = eng.extras()
    check(after["toast"] is None and after["pending_notices"] == [], "ending: the toast leaves the screen")
    entry = next(a for a in after["achievements"] if a["id"] == achievement)
    check(entry["unlocked"] and entry["at"], "ending: the achievement is recorded with its time", str(entry))
    # --- D07: seen backgrounds are CGs -------------------------------------------------------------
    want = model.unlocks_of(cur["entered"])
    prof = eng.profile()
    check(set(prof["unlocks"]["cg"]) == want["cg"], "every background seen on the way is an unlocked CG",
          f"{sorted(prof['unlocks']['cg'])} vs {sorted(want['cg'])}")
    check(set(prof["unlocks"]["music"]) == want["music"], "every track heard is unlocked in the music room")
    check(ids_of(prof["unlocks"]["achievements"]) == want["achievements"] == goal,
          "exactly the reached ending's achievement is unlocked")
    check(sha(slot_file) == saved["hash"], "the saved slot was never rewritten by playing on")
    state["entered"] = cur["entered"]
    state["ending"] = {"node": ending, "achievement": achievement, "unlocks": {k: sorted(v) for k, v in want.items()}}
    state["read_all"] = sorted({cid for nid in cur["path"] for cid in model.content_ids(nid)})
    eng.step(0.25, 12)  # let the profile write itself
    quit_through_menu(eng)
    eng.close()
    disk = disk_profile(eng)
    check(set(disk["unlocks"]["cg"]) == want["cg"] and ids_of(disk["unlocks"]["achievements"]) == goal,
          "quit flushed the unlocks to disk")
    check(disk["auto_enabled"] is False, "Auto was left off in the profile")
    save_state(ctx, state)


def check_scripted_node(eng, model, cur, node):
    """A node whose script changes a story variable: save and rewind must keep that change.

    Leaves the engine on `node` again, with `cur` describing it.
    """
    save_dir = pathlib.Path(eng.query("save_dir"))
    vars_at = dict(cur["vars"])
    slot_file = save_to_slot(eng, 2, save_dir)
    saved_vars = {k: number(v) for k, v in json.loads(slot_file.read_text(encoding="utf-8"))["variables"].items()}
    check(set(saved_vars) == set(vars_at) and all(abs(saved_vars[k] - v) < 1e-6 for k, v in vars_at.items()),
          f"a save made on the scripted node {node} holds the script's variable change",
          f"saved {saved_vars} expected {vars_at}")
    step_forward(eng, model, cur)
    presses = 0
    while eng.node() != node and presses < 6:
        eng.press("bksp")
        presses += 1
    check(eng.node() == node, f"Backspace rewinds onto the scripted node {node}", f"engine node {eng.node()}")
    expect_vars(eng, vars_at, f"after the rewind onto the scripted node {node} the script's change is still there")
    cur["node"], cur["vars"] = node, vars_at


def phase_extras(ctx, model, eng):
    eng.shot_tag = "p5_"
    state = load_state(ctx)
    eng.boot()
    save_dir = pathlib.Path(eng.query("save_dir"))
    slot_file = save_dir / "save_slot_1.json"
    want = {k: set(v) for k, v in state["ending"]["unlocks"].items()}
    prof = eng.profile()
    check(set(prof["unlocks"]["cg"]) == want["cg"] and set(prof["unlocks"]["music"]) == want["music"]
          and ids_of(prof["unlocks"]["achievements"]) == want["achievements"],
          "restart: every unlock survived the process")
    saves_before = {p.name: sha(p) for p in save_dir.iterdir()}
    read_before = sorted(prof["read_content_ids"])
    unlocks_before = json.dumps(prof["unlocks"], sort_keys=True)
    extras = eng.extras()
    counts = extras["counts"]
    check(counts["gallery"] == {"total": len(model.extras["gallery"]), "unlocked": len(want["cg"])}
          and counts["music"] == {"total": len(model.extras["music"]), "unlocked": len(want["music"])}
          and counts["achievements"] == {"total": len(model.extras["achievements"]),
                                         "unlocked": len(want["achievements"])},
          "restart: the extras counters match the unlocks", json.dumps(counts))
    # --- the Ekstralar tour -----------------------------------------------------------------------------
    eng.select_row(3)
    check(eng.menu()["rows"][3]["label"] == "Ekstralar", "title row 3 is Ekstralar")
    eng.press("enter")
    menu = eng.menu()
    check(menu["mode"] == 6, "Ekstralar opens its root page")
    check([r["label"] for r in menu["rows"]] == ["Galeri", "Müzik Odası", "Başarımlar", "Geri"],
          "Ekstralar root: Galeri / Müzik Odası / Başarımlar / Geri", str([r["label"] for r in menu["rows"]]))
    eng.shot("extras_root")
    root_values = {r["label"]: r["value"] for r in eng.menu()["rows"]}
    check(root_values == {"Galeri": f"{counts['gallery']['unlocked']}/{counts['gallery']['total']}",
                          "Müzik Odası": f"{counts['music']['unlocked']}/{counts['music']['total']}",
                          "Başarımlar": f"{counts['achievements']['unlocked']}/{counts['achievements']['total']}",
                          "Geri": ""},
          "Ekstralar root: each row shows its unlocked/total counter", str(root_values))
    pages = ((0, 7, "gallery", "Galeri"), (1, 8, "music", "Müzik Odası"), (2, 9, "achievements", "Başarımlar"))
    for row, mode, key, label in pages:
        eng.select_row(row)
        eng.press("enter")
        menu = eng.menu()
        check(menu["mode"] == mode, f"{label} page opens (mode {mode})")
        ex = menu["extras"]
        total, unlocked = counts[key]["total"], counts[key]["unlocked"]
        check(ex["total"] == total and ex["unlocked"] == unlocked and ex["counter"] == f"1 / {total}",
              f"{label}: {unlocked} of {total} unlocked, cursor on 1 / {total}", json.dumps(ex)[:160])
        if key == "gallery":
            cells = ex["cells"]
            check(len(cells) == total and sum(1 for c in cells if c["unlocked"]) == unlocked
                  and all("thumbnail" not in c and c["title"] == "???" for c in cells if not c["unlocked"]),
                  "Galeri: unlocked cells match, a locked cell exposes neither title nor image")
            check({c["thumbnail"] for c in cells if c["unlocked"]} ==
                  {g["image"] for g in model.extras["gallery"] if g["id"] in want["cg"]},
                  "Galeri: the unlocked cells are exactly the backgrounds seen")
        elif key == "music":
            check({i["id"] for i in ex["items"] if i["unlocked"]} == want["music"], "Müzik Odası: unlocked tracks match")
        else:
            got = {i["id"]: i for i in ex["items"]}
            check({i for i, v in got.items() if v["unlocked"]} == want["achievements"],
                  "Başarımlar: unlocked entries match")
            earned = got[state["ending"]["achievement"]]
            check(earned["title"] != "???" and earned["at"], "Başarımlar: the earned one shows its title and date")
            hidden = {a["id"] for a in model.extras["achievements"] if a.get("hidden")}
            check(all(got[i]["title"] == "???" for i in hidden if not got[i]["unlocked"]),
                  "Başarımlar: a locked hidden achievement stays masked")
        eng.press("down")
        moved = eng.menu()["extras"]["counter"]
        check(moved != f"1 / {total}" and moved.endswith(f" / {total}"),
              f"{label}: Down moves the cursor", moved)
        eng.shot(f"extras_{key}")
        eng.press("esc")
        check(eng.menu()["mode"] == 6, f"{label}: Geri returns to Ekstralar")
    eng.press("esc")
    check(eng.menu()["mode"] == 3 and eng.menu()["title_active"], "Ekstralar closes back to the title")
    # --- the tour changed nothing ---------------------------------------------------------------------------
    eng.step(0.25, 12)
    again = eng.profile()
    check(sorted(again["read_content_ids"]) == read_before and json.dumps(again["unlocks"], sort_keys=True) == unlocks_before,
          "the Extras tour changed no read mark and no unlock")
    check({p.name: sha(p) for p in save_dir.iterdir()} == saves_before, "the Extras tour left every save byte-identical")
    check(eng.node() == model.start, "the story position behind the title is untouched")
    quit_through_menu(eng, from_title=True)
    eng.close()
    check({p.name: sha(p) for p in save_dir.iterdir()} == saves_before, "the saves are unchanged after the last session")


PHASES = {"play": phase_play, "resume": phase_resume, "player": phase_player,
          "playon": phase_playon, "extras": phase_extras}


# ===========================================================================
# Child + parent
# ===========================================================================
def run_child(args):
    ctx = dict(zip(("phase", "release", "engine_lib", "sample", "xdg", "state", "shots"), args))
    ctx["shots"] = ctx["shots"] or None
    model = Model(ctx["sample"])
    eng = Engine(ctx["engine_lib"], ctx["release"], shots=ctx["shots"])
    try:
        PHASES[ctx["phase"]](ctx, model, eng)
    except Skip as skip:
        print(f"[SKIP] {skip}")
        return 77
    except Fail:
        return 1
    print(f"[Journey] phase {ctx['phase']} OK", flush=True)
    return 0


def build_release(base, player_bin, engine_lib, sample):
    release = base / "release"
    (release / "Assets" / "packages").mkdir(parents=True)
    (release / "mods").mkdir()
    package = release / "Assets" / "packages" / "game.rowlpkg"
    pack = subprocess.run([sys.executable, str(PACKAGER), str(sample / "Assets"), str(package),
                           "--game-manifest", str(sample / "project.rowlproj")],
                          capture_output=True, text=True, check=False)
    check(pack.returncode == 0, "the packager builds game.rowlpkg", pack.stdout + pack.stderr)
    name = "RowlGame.exe" if os.name == "nt" else "RowlGame"
    shutil.copy2(player_bin, release / name)
    shutil.copy2(engine_lib, release / engine_lib.name)
    shutil.copy2(sample / "project.rowlproj", release / "project.rowlproj")
    (release / "mods" / "README.md").write_text("# Rowl Engine mods\n", encoding="utf-8")
    (release / "README.txt").write_text("ROWL ENGINE - KAYIP TABLO RELEASE\n", encoding="utf-8")
    shutil.copy2(ROOT / "packaging" / "THIRD_PARTY_NOTICES.md", release / "THIRD_PARTY_NOTICES.md")
    launcher = subprocess.run([sys.executable, str(LAUNCHER_TOOL), str(release), "--platform",
                               "windows" if os.name == "nt" else "posix"], capture_output=True, text=True)
    check(launcher.returncode == 0, "launchers generated", launcher.stdout + launcher.stderr)
    verify = subprocess.run([sys.executable, str(VERIFIER), str(release)], capture_output=True, text=True)
    check(verify.returncode == 0, "the release passes tools/verify_release_package.py", verify.stdout + verify.stderr)
    # No loose game files: Assets holds the package and nothing else.
    leftovers = sorted(str(p.relative_to(release)) for p in (release / "Assets").rglob("*") if p.is_file())
    check(leftovers == ["Assets/packages/game.rowlpkg", "Assets/packages/game.rowlpkg.sha256"],
          "release: no loose game assets, only the package", str(leftovers))
    return release


def scan_logs(log_lines):
    errors, warns = [], collections.Counter()
    for raw in log_lines:
        line = ANSI.sub("", raw)
        if re.search(r"\[(ERROR|FATAL|CRITICAL)\]", line):
            errors.append(line.strip())
        elif "[WARN]" in line:
            body = re.sub(r"^\[[^\]]*\]\s*\[WARN\]\s*", "", line.strip())
            warns[re.sub(r"'[^']*'", "'*'", body)[:140]] += 1
    return errors, warns


def main():
    argv = sys.argv[1:]
    if argv and argv[0] == "--child":
        return run_child(argv[1:])
    shots = None
    if "--shots" in argv:
        index = argv.index("--shots")
        shots = argv[index + 1]
        del argv[index:index + 2]
    if len(argv) != 3:
        print(f"usage: {sys.argv[0]} <player-bin> <engine-lib> <sample-dir> [--shots DIR]", file=sys.stderr)
        return 2
    player_bin, engine_lib, sample = (pathlib.Path(a).resolve() for a in argv)
    for path in (player_bin, engine_lib, sample / "project.rowlproj"):
        if not path.is_file():
            print(f"missing input: {path}", file=sys.stderr)
            return 2
    started = time.time()
    all_log = []
    try:
        with tempfile.TemporaryDirectory(prefix="rowl-d-journey-") as directory:
            base = pathlib.Path(directory)
            release = build_release(base, player_bin, engine_lib, sample)
            env = dict(os.environ, XDG_DATA_HOME=str(base / "xdg"), LOCALAPPDATA=str(base / "xdg"),
                       SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="dummy")
            state = base / "journey_state.json"
            for phase in ("play", "resume", "player", "playon", "extras"):
                print(f"\n=== {phase} ===", flush=True)
                child = subprocess.run(
                    [sys.executable, "-u", str(pathlib.Path(__file__).resolve()), "--child", phase, str(release),
                     str(engine_lib), str(sample), str(base / "xdg"), str(state), shots or ""],
                    capture_output=True, text=True, env=env, timeout=240,
                    cwd=str(release))  # the launcher cds into the game folder; no stray Assets/ in the CWD
                # Tests' own lines first, engine log lines are kept for the log scan.
                for line in child.stdout.splitlines():
                    clean = ANSI.sub("", line)
                    if clean.startswith(("[PASS]", "[FAIL]", "[NOTE]", "[SHOT]", "[Journey]", "[SKIP]")):
                        print(clean)
                    else:
                        all_log.append(clean)
                all_log += [ANSI.sub("", l) for l in child.stderr.splitlines()]
                if child.returncode == 77:
                    print("[SKIP] SDL3 shared library not found; journey needs real SDL input events")
                    return 77
                if child.returncode != 0:
                    tail = "\n".join(all_log[-40:])
                    print(f"[FAIL] phase {phase} exited {child.returncode}\n{tail}", file=sys.stderr)
                    return 1
            errors, warns = scan_logs(all_log)
            unexpected = [e for e in errors if not any(k in e for k in KNOWN_ERRORS)]
            known = len(errors) - len(unexpected)
            check(not unexpected, "no unexpected ERROR line in any engine log", "\n".join(unexpected[:5]))
            note(f"expected start-up ERROR lines (engine boots before SetProjectDirectory): {known}")
            for text, count in warns.most_common():
                note(f"WARN x{count}: {text}")
            elapsed = time.time() - started
            check(elapsed < 120, f"the whole journey runs in under 120 s ({elapsed:.1f} s)")
    except Fail:
        print("[DAcceptanceJourney] FAILED", file=sys.stderr)
        return 1
    print("[DAcceptanceJourney] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
