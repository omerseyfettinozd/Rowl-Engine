# 📌 MASTER BLUEPRINT: THE ULTIMATE VISUAL NOVEL ENGINE & EDITOR

> **Core Philosophy:** *"Zero-Code Visual Creation, Blender-Grade Modularity, ComfyUI-Style Flow, Sandboxed Lua Extensibility, and Peak Performance at the Sweet Spot ('Tost Makinesi' Principle)."*

---

## 1. EXECUTIVE SUMMARY & PROJECT VISION

This document defines the absolute engineering blueprint for building the world's most flexible, high-performance, crash-proof, and completely visual Visual Novel Game Engine and Editor (**Rowl Engine**). 

The primary goal is to empower creators—ranging from solo writers to professional visual novel studios—to build deeply immersive, responsive, and complex games **without writing a single line of code**, while giving advanced developers and the modding community full freedom via secure scripting. Behind the scenes, the architecture is engineered to hit the **"Sweet Spot"** of optimization: maximum CPU/GPU efficiency, zero memory waste, lightning-fast execution, and cross-platform native stability across **PC (Windows, Linux, macOS) and Mobile (Android, iOS)**.

---

## 2. TECHNOLOGY STACK & ARCHITECTURE

To ensure uncompromising cross-platform performance and rock-solid stability, the engine and editor are decoupled and structured around high-performance native components communicating via modern IPC and serialization standards.

| Component | Technology Stack | Purpose & Benefits |
| :--- | :--- | :--- |
| **Engine Runtime** | C++20 + SDL3 / SFML 3.1 | Raw hardware performance, microsecond execution times, minimal memory footprint, multi-platform mobile/desktop compilation. |
| **Visual Editor** | C# .NET 8 + Avalonia UI | Modern, fully native cross-platform GUI framework running smoothly on Linux, Windows, and macOS. |
| **Data Bridge** | Local Named Pipes / TCP + FlatBuffers | Zero-copy / ultra-fast binary communication between editor and player for real-time live preview. |
| **Modularity & Scripting** | Sandboxed Lua Runtime + VFS | Secure, isolated extension layer allowing community mods and mini-games without risking engine crashes or breaking Android/iOS compatibility. |

---

## 3. COMFYUI-STYLE VISUAL NODE GRAPH SYSTEM

Instead of traditional linear scripts, creators build games visually using a node-based graph editor inspired by ComfyUI and modern node-compositors.

- **Node Modules (Frames / Scenes):** Every node represents a distinct state, frame, or dialogue block in the game.
- **Pin & Wire Connection:** Nodes feature input and output pins. Creators visually draw connection wires from choice buttons to subsequent node modules.
- **Node Types:**
  - *Dialogue/Scene Node:* Holds background, character sprites, speech bubbles, text, choice branches, and voice-over hooks.
  - *Logic/Condition Node:* Evaluates game variables (e.g., `has_key == true`, affection meters).
  - *Action/Event Node:* Modifies inventory, triggers screen shakes, plays SFX, sets flags, or executes custom Lua hooks.

---

## 4. NODE INSPECTOR & VISUAL COMPOSITION

Selecting any node in the graph opens a context-sensitive **Inspector Panel** on the right side of the editor, allowing precise visual composition:

1. **Background Layer:** Select background assets, set scaling/stretching rules, and assign transition animations (fade, slide, dissolve).
2. **Character Portraits / Sprites:** Place character assets across preset anchors (`Left`, `Center`, `Right`) or custom coordinates. Adjust expressions, tints, and focus states.
3. **Speech Bubble & Dialogue Box:** Configure 9-slice scalable dialogue bubbles, speaker name tags, rich text content, MSDF typography, and typewriter speeds.
4. **Audio Subsystem:** Assign background music (BGM) with streaming options, sound effects (SFX), voice-over clips, and environment DSP filters (e.g., Cave, Telephone, Underwater).
5. **Choice Branching:** Add, reorder, and link decision options to target node IDs.

---

## 5. RESPONSIVE LAYOUT & RESOLUTION INDEPENDENCE (PC & MOBILE)

Visual novels built with Rowl Engine must look pristine across 4K monitors, Steam Deck, and mobile aspect ratios (16:9, 16:10, 21:9, 9:16 portrait/landscape).

- **Virtual Canvas:** The engine renders at a fixed virtual design resolution (e.g., 1920x1080).
- **Anchor & Percentage Positioning:** UI elements, speech bubbles, and character sprites are positioned using anchor points (`TopLeft`, `BottomCenter`, etc.) and relative percentage offsets rather than hardcoded pixel values.
- **9-Slice Scaling:** UI boxes and speech bubbles stretch gracefully without distorting border corners.
- **Aspect Ratio Guardian:** Automatic letterboxing/pillarboxing ensures the artistic layout never breaks regardless of physical screen ratios on Android, iOS, or PC.
- **Unified Input Handling:** Mouse clicks on desktop and touch taps on mobile are abstracted into unified interaction events by the SDL3 input layer.

---

## 6. EDITOR-RUNTIME IPC & LIVE PREVIEW

- **Problem Solved:** Traditional disk file I/O (`oyun_verisi.json` reload) during editing introduces latency and friction.
- **Architecture:** The C# Avalonia Editor and C++ Runtime communicate via **Local Named Pipes / TCP** coupled with **FlatBuffers** serialization. 
- **Benefit:** When the creator modifies a node or presses "Play" in the editor, changes are pushed instantly to the running engine instance with near-zero latency, enabling fluid hot-reload testing.

---

## 7. STATE MANAGEMENT & TIME-TRAVEL REWIND (PERSISTENT DATA STRUCTURES)

- **Architecture:** Powered by **Persistent Data Structures (Structural Sharing)**.
- **Benefit:** Instead of brute-force deep cloning of the entire game state on every step, only modified variables and node states are duplicated while historical branches share immutable references. 
- **Result:** Implements infinite **Rewind / Backlog** capabilities with near-zero RAM footprint and absolute protection against memory corruption.

---

## 8. HYBRID VFS (VIRTUAL FILE SYSTEM) & ASSET MANAGEMENT

- **Development & Modding (Loose Files Mode):** During creation and community modding, assets (`assets/`, `mods/`) reside in open plaintext/raw formats on disk, allowing creators and translators to drop in files freely.
- **Deployment & Performance (Packed Mode):** Upon export, assets are packed into optimized, encrypted `.rowlpkg` archives compressed with **Zstandard (Zstd)**.
- **Threaded VFS Reader:** A dedicated background thread reads and decompresses assets asynchronously, ensuring buttery-smooth transitions and full mobile compatibility without stuttering.

---

## 9. SANDBOXED LUA SCRIPTING & COMMUNITY MODDING

- **Extensibility:** Advanced users and modders can write custom mini-games, complex inventory systems, or scripted events using lightweight **Lua** scripts.
- **Crash-Proof Sandbox:** Lua code runs inside a strict C++ sandbox. If a mod contains infinite loops or runtime errors, the engine catches the exception safely, displays a non-fatal warning, and keeps the core game running without crashing.
- **Cross-Platform:** Works identically on Windows, Linux, Android, and iOS without requiring native compilation (.dll/.so restrictions bypassed).

---

## 10. AUDIO SUBSYSTEM & DSP ENVIRONMENT FILTERS

- **Dual-Path Audio Architecture:**
  - *Streaming Mode:* Large BGM tracks and voice-over files stream directly from disk/VFS to prevent RAM bloat.
  - *Memory Buffer Pool:* Short UI clicks and SFX load directly into RAM for zero-latency playback.
- **Automatic Ducking:** Background music automatically dips in volume when character voice-overs or important dialogue sounds play.
- **DSP Environment Filters:** Real-time audio filters (Reverb, Low-Pass, Telephone, Underwater) can be toggled per node/scene via simple dropdowns in the inspector.

---

## 11. TYPOGRAPHY, MSDF FONT RENDERING & i18n

- **MSDF (Multi-channel Signed Distance Field):** Ultra-crisp font rendering at any scale (from mobile screens to 4K displays) with zero pixelation.
- **Localization (i18n):** Complete decoupling of strings into external dictionaries supporting UTF-8/UTF-16 (Turkish, CJK scripts) and BiDi text shaping via HarfBuzz/FriBidi.
- **GPU Text Shaders:** Typewriter effects, text shaking, and fades handled entirely on the GPU via shaders, keeping CPU usage close to zero ("Tost Makinesi" principle).

---

## 12. PHASING & EXECUTION ROADMAP SUMMARY

The engine will be built iteratively, starting with a PC-first core and expanding to mobile:
- **Phase 1:** C++20 Core, SDL3 Windowing, Hybrid VFS, and Zstd Asset Manager.
- **Phase 2:** C# Avalonia Editor & Local IPC (FlatBuffers) Live Preview.
- **Phase 3:** ComfyUI-Style Visual Node Graph, Inspector, and MSDF Typography.
- **Phase 4:** Sandboxed Lua Scripting, Immutable Rewind State, and Dual-Path Audio.
- **Phase 5:** Mobile Export (Android & iOS) with Responsive Touch UI and Touch Optimization.
