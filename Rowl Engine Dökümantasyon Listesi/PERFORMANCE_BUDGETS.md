# 📊 PERFORMANCE BUDGETS & TARGETS

> **Objective:** Define numerical engineering targets so every optimization decision can be measured against hard numbers. All targets enforce the *"Tost Makinesi"* (Zero Waste) principle.

> **Status (2026-09-12): Target catalog, not measured release evidence.** The
> normative current contract is `../docs/PLATFORM_SUPPORT.md`. Values below
> become release gates only after a compatible Release benchmark series exists
> for the named target device. Legacy FlatBuffers/IPC targets have been replaced
> by the current in-process P/Invoke preview boundary.

---

## 1. FRAME BUDGET & RENDERING TARGETS

| Target Device | Time Budget per Frame | Minimum FPS | Notes |
| :--- | :--- | :--- | :--- |
| **High-End Desktop** | **16.0 ms** | 60 FPS | 4K monitors, 144 Hz displays supported via VSync off. |
| **Mid-Range Desktop** | **16.0 ms** | 60 FPS | 1080p / 1440p with SDL3 renderer. |
| **Steam Deck / Handheld** | **16.0 ms** | 60 FPS | 800p native display, touched optimizations. |
| **Low-End Mobile (Android / iOS)** | **33.3 ms** | 30 FPS | Battery preservation, thermal throttling safe. |

### Render Thread Sub-Budgets (60 FPS Target):
- **CPU Logic + Lua Script Execution:** ≤ 2 ms
- **VFS Asset Lookup + Decompression:** ≤ 1 ms (non-blocking)
- **GPU Render Submit (Draw Calls):** ≤ 4 ms
- **Frame Idle / Wait for VSync:** Remainder (system fills gap)

---

## 2. MEMORY BUDGETS (RAM USAGE)

| Memory Category | Budget (Desktop) | Budget (Mobile) | Notes |
| :--- | :--- | :--- | :--- |
| **Engine Core + Lua VM** | 64 MB | 48 MB | Fixed overhead on startup. |
| **Texture Cache (LRU)** | 64 MiB default | Host-selected profile, minimum 1 MiB | Current runtime default and `RowlEngine_SetTextureCacheBudgetBytes`; device value requires measurement. |
| **Audio Buffer Pool (SFX)** | 64 MB | 32 MB | Pre-decoded WAV / short Ogg. |
| **BGM Streaming Buffer** | 2 MB | 1 MB | Ring buffer for ongoing playback. |
| **State History (Rewind Stack)** | 5 MB | 2 MB | Persistent structures; scales with gameplay length. |
| **Package Index (VFS)** | 8 MB | 4 MB | Loaded once at startup from `.rowlpkg` index table. |
| **Total Peak Application** | **< 1.5 GB** | **< 512 MB** | Must pass headless / offline mode. |

---

## 3. ASSET MEMORY CALCULATION FORMULAS

### Texture Cache (`total_texture_ram`):
```
total_texture_ram = Σ (width_i × height_i × 4 bytes RGBA) / LRU_EVICTION_WINDOW
```

### BGM Streaming RAM:
```
bgm_ram_per_track = STREAMING_RING_BUFFER_SIZE = 2 MB (constant)
```

---

## 4. STARTUP TIME TARGETS

| Phase | Desktop Target | Mobile Target | Notes |
| :--- | :--- | :--- | :--- |
| **Engine Splash → First Frame** | **≤ 1.5 seconds** | **≤ 2.5 seconds** | Includes VFS index load + font atlas init. |
| **Editor Cold Start** | **≤ 3.0 seconds** | N/A | Avalonia shell + in-process native host initialization. |
| **Level / Node Switch** | **≤ 200 ms** | **≤ 300 ms** | From node script execution to first render frame. |

---

## 5. EDITOR/NATIVE LIVE PREVIEW TARGETS

| Operation | Target Latency | Measurement Point |
| :--- | :--- | :--- |
| **Editor → Runtime Preview Push** | **< 1 ms target** | End-to-end from Avalonia change delivery through the in-process P/Invoke boundary. |
| **Runtime State Snapshot (Rewind)** | **< 0.5 ms** | Pointer swap + audio/visual diff apply. |
| **Preview JSON Serialization** | Baseline required | Single node preview payload using the current versioned benchmark fixture. |

---

## 6. BATTERY & THERMAL TARGETS (MOBILE)

- **Battery Drain target:** ≤ 3% per 15 minutes on the selected reference Android device; not yet validated.
- **Thermal target:** Establish from physical-device profiling before adopting a release threshold.
- **Background Behavior:** When app is backgrounded, pause all audio, Lua timers, and render loop within 100 ms to comply with OS power policies.

---

## 7. PERFORMANCE TESTING CHECKLIST (QA GATE)

To pass any Phase delivery, automated QA must verify:
- [ ] `ctsan` / `valgrind` reports zero memory leaks after 10,000 state transitions.
- [ ] Frame time graph stays under budget for 5-minute runtime stress test on test target hardware.
- [ ] Mobile battery drain test passes on physical mid-range device.
- [ ] VFS LRU cache never exceeds budgeted RAM during extended gameplay session.
