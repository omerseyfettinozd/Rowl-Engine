# Linux real-screen acceptance — C01 / C02 (2026-10-21)

Run on commit `7974bc5` (Debug), re-checked after the fixes listed below.
Host: CachyOS, Hyprland 0.56.2 (Wayland) + XWayland, PipeWire 1.6.9, HiDPI eDP.
Tests ran on a temporary headless Hyprland output; input via pc-eye (updated the
same day: Lua dispatch, XWayland XTest keyboard, SDL3 evdev keys), screenshots
via grim, audio via the PipeWire sink monitor (RMS). Surfaces:
(a) standalone player, SDL Wayland-native; (b) standalone player, SDL x11 on
XWayland; (c) editor (Avalonia, XWayland); (d) pure X11 session.

There is no pure X11 session on this host, so column (d) is **awaiting
external evidence** for every row — it is not inferred from (b).

## C01 — Golden Project end to end (copy of `samples/second_signal`)

| Step | Result |
|---|---|
| Editor opens the project from a Unicode path with spaces | PASS |
| Turkish text entry ("Şişli'de ığdır"), Ctrl+S and menu Save write the graph | PASS |
| Edited text visible in the editor preview | FAIL → fixed in `3b58580` (source-locale catalog sync) |
| Package (`package_assets.py --game-manifest`, `export_game.py portable-zip`) | PASS once `project_uuid` exists; misleading error text fixed in `895a6cf` |
| Packaged game starts from a Unicode path, no tofu | PASS |
| Choice click advances (a, b) | PASS |
| Save → quit → relaunch → load returns to the same node | PASS |
| Rewind | PASS |
| tr ↔ en switch in the standalone player | N/A — no player language UI yet (D05) |

## C02 — matrix

| Row | a | b | c | d |
|---|---|---|---|---|
| Mouse click / advance | PASS | PASS | PASS (menus); game-panel choice click not verifiable via automation | awaiting |
| Keyboard (Space/Enter/F5/F9/Backspace; Ctrl+S + Turkish in editor) | PASS | PASS | PASS | awaiting |
| Esc pause menu | FAIL → fixed in `63db1b4` (verified on a) | FAIL → fixed in `63db1b4` (not re-checked on b) | N/A | awaiting |
| Resize / fullscreen | PASS | PASS | PARTIAL: Inspector clipped at ~1000 px width | awaiting |
| Focus change | PASS (game and audio keep running by design) | not tested | not tested | awaiting |
| Audio output (BGM on sink) | PASS (−54 dBFS vs −96 closed) | PASS | PASS | awaiting |
| Audio while paused | BGM keeps playing — matches the current contract (pause freezes story simulation only) | same | N/A | awaiting |
| Unicode / space path | PASS | PASS | PASS | awaiting |
| HiDPI scale 2 | PASS (sharp) | upscaled/blurry — expected XWayland behaviour | not tested | awaiting |

## Open findings

- Pause-menu labels are ASCII-only ("Duraklatildi", "Cikis") from the old
  debug-text era; with the TrueType GPU path they can be real Turkish — D05.
- Editor Inspector clips at ~1000 px window width (low).
- "Camera tween stalled: zero-dt steps" warning toast at editor start (low).
- Re-check (b) and a real (non-headless) output for the pause menu.
