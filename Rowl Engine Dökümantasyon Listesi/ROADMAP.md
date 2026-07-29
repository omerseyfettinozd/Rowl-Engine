# 🗺️ ROWL ENGINE: MASTER ROADMAP & PHENOMENAL PHASES

> **Strategy:** *"PC-First, Mobile-Ready, Zero Compromise, Rock-Solid Stability."*

This document outlines the macro roadmap for developing **Rowl Engine**. To ensure maximum quality and zero architectural debt, development is split into 5 sequential phases. No phase begins until the previous phase's deliverables are fully functional and verified.

---

## 📅 PHASE 1: C++ CORE RUNTIME & VFS FOUNDATION
*Focus: Raw performance, windowing, file systems, and cross-platform compilation.*

- **1.1 C++20 Core & SDL3/SFML Setup:** Initialize project repository, CMake configuration, and multi-platform build scripts (Linux, Windows, Android, iOS).
- **1.2 Hybrid Virtual File System (VFS):** Implement loose files directory lookup for development and modding.
- **1.3 Zstd Archive Engine & Threaded Reader:** Build `.rowlpkg` container format and background worker thread for compressed asset loading.
- **1.4 Basic Window & Render Loop:** Establish the hardware-accelerated rendering pipeline (dirty-flag rendering pattern).

---

## 📅 PHASE 2: C# AVALONIA EDITOR & FLATBUFFERS IPC
*Focus: Native cross-platform GUI and lightning-fast live preview bridge.*

- **2.1 Avalonia UI Editor Shell:** Setup C# .NET 8 project structure with dockable panels, dark theme, and project management.
- **2.2 FlatBuffers Schema Definition:** Define serialization structures for nodes, variables, and assets.
- **2.3 Local Named Pipes / TCP Bridge:** Implement reliable, low-latency IPC between the C# Editor and the C++ Runtime.
- **2.4 Live Preview (Hot-Reload):** Enable instant state pushing from editor to player without disk I/O overhead.

---

## 📅 PHASE 3: COMFYUI-STYLE NODE GRAPH & MSDF TYPOGRAPHY
*Focus: Visual authoring experience and pristine text rendering.*

- **3.1 Node Graph Canvas:** Implement interactive node dragging, zooming, pin-and-wire connections, and graph zooming.
- **3.2 Contextual Inspector Panel:** Build dynamic property editors for Backgrounds, Characters, Dialogues, and Audio.
- **3.3 MSDF Font Pipeline:** Integrate Multi-channel Signed Distance Field text rendering for 4K crispness.
- **3.4 Responsive Layout Engine:** Implement 9-slice scaling, virtual canvas sizing, and anchor/percentage positioning.

---

## 📅 PHASE 4: SANDBOXED LUA SCRIPTING & TIME-TRAVEL REWIND
*Focus: Extensibility, modding freedom, and AAA state management.*

- **4.1 Sandboxed Lua Integration:** Embed lightweight Lua runtime with strict security boundaries for custom mini-games and event scripts.
- **4.2 Persistent Data Structures (Structural Sharing):** Implement immutable state history for infinite, zero-RAM-bloat Rewind and Backlog.
- **4.3 Dual-Path Audio Subsystem:** Deploy disk streaming for BGM/Voice-overs and memory pools for SFX, plus automatic Ducking.
- **4.4 DSP Environment Filters:** Add real-time audio effects (Reverb, Low-Pass, Telephone) controllable via node inspector.

---

## 📅 PHASE 5: MULTI-PLATFORM MOBILE EXPORT (ANDROID & IOS)
*Focus: Touch optimization, mobile packaging, and global deployment.*

- **5.1 Touch Input Abstraction:** Map mobile touch gestures (taps, swipes) to standard interaction events.
- **5.2 Screen Orientation & Aspect Guardian:** Ensure automatic pillarboxing/letterboxing across 9:16 portrait and 16:9 landscape devices.
- **5.3 Android Studio & Xcode Toolchain Integration:** Automate `.apk` and `.ipa` bundling pipelines.
- **5.4 Final QA & Stress Testing:** Run performance benchmarks on low-end hardware ("Tost Makinesi" test).
