# 🔬 SUB-SPEC 07: AVALONIA EDITOR & COMFYUI NODE GRAPH ARCHITECTURE

> **Subsystem Target:** C# .NET 8 Avalonia UI Editor Shell, ComfyUI-Style Interactive Canvas, Dockable Workspaces, Contextual Inspector, and Module Manager.

---

## 1. ARCHITECTURAL OVERVIEW

The **Rowl Engine Editor** is a modern, cross-platform visual authoring suite built with **C# .NET 8** and **Avalonia UI**. It provides a zero-code environment where creators construct complex branching visual novels using a node-based graph editor inspired by ComfyUI and modern node compositors.

```
+-----------------------------------------------------------------------------------+
|                                 ROWL ENGINE EDITOR                                |
|                                                                                   |
|  [ TOP TOOLBAR: Project | Module Manager | IPC Connect | Play / Live Preview ]    |
|  +-----------------------+----------------------------------+------------------+  |
|  | PROJECT / ASSET TREE  | COMFYUI NODE GRAPH CANVAS        | NODE INSPECTOR   |  |
|  |                       |                                  |                  |  |
|  |  ├── assets/          |  [ Node #101 ]──(Choice 1)───────>| - Background     |  |
|  |  │   ├── bg/          |    (Dialogue)  ──(Choice 2)──┐   | - Sprites        |  |
|  |  │   └── audio/       |                              │   | - Audio & DSP    |  |
|  |  ├── scripts/         |                       [ Node #102 ]  | - Lua Hooks      |  |
|  |  └── modules/         |                         (Action) |                  |  |
|  |                       |                                  |                  |  |
|  +-----------------------+----------------------------------+------------------+  |
|  [ BOTTOM DOCK: IPC Output Log / Console | Backlog Debugger | Variable Monitor ]  |
+-----------------------------------------------------------------------------------+
```

---

## 2. AVALONIA UI SHELL & DOCKING SYSTEM

- **MVVM Architecture:** Uses `CommunityToolkit.Mvvm` for clean separation between UI XAML views and underlying graph ViewModels.
- **Dockable Panels (`Dock.Avalonia`):** Creators can drag, float, dock, or pin any panel (Node Canvas, Asset Tree, Inspector, Live Preview Window, Console) to customize their authoring workspace.
- **Theme Engine:** Dark-themed high-contrast UI tailored for long authoring sessions.

---

## 3. COMFYUI-STYLE INTERACTIVE NODE CANVAS

The node graph canvas is the core workspace where games are designed visually:

- **Canvas Operations:** Infinite canvas with hardware-accelerated pan ($1:1$ smooth dragging) and zoom ($10\% - 400\%$).
- **Node Anatomy:**
  - **Header:** Node ID, Custom Label, Node Type Icon, and Color Coding.
  - **Input Pins (Left):** Execution Inflow from previous choices or logic nodes.
  - **Output Pins (Right):** Choice Branch 1, Choice Branch 2, Fallback Output.
- **Bezier Wire Connection:** Connection wires are rendered as smooth Bezier curves with color-coded data types (e.g., Green = Execution Flow, Yellow = Logic Condition, Blue = Audio Event).
- **Live Execution Glow:** During IPC Live Preview testing, active wires and nodes glow in real-time as the C++ runtime traverses them.
- **Grouping Frames (Blender-Style Boxes):** Creators can draw colored comment boxes around node clusters (e.g., "Chapter 1 - Beach Scene") to keep large stories organized.

---

## 4. CONTEXTUAL INSPECTOR PANEL

Selecting any node on the canvas instantly populates the **Inspector Panel** on the right:

1. **Background & Visual Layer:** Asset picker for background textures, transition animation selection (Fade, Slide, Zoom), and pan/zoom speed.
2. **Character Placement:** Multi-character slot grid. Configure sprite expressions, anchor positioning (`Left`, `Center`, `Right` or custom XY percentages), and tint/focus state.
3. **Speech Bubble & Typography:** Speaker Name Tag, Rich Text Editor with live markup syntax highlighting, MSDF font picker, and typewriter speed slider.
4. **Audio & DSP Filter Picker:** BGM/SFX asset selectors, Voice-Over clip sync, and DSP Filter Dropdown (`Normal`, `Cave`, `Telephone`, `Underwater`).
5. **Branching & Choice Manager:** Add/Remove choice buttons, reorder choices via drag-and-drop, and assign conditional visibility flags (e.g., `Show choice only if gold >= 50`).

---

## 5. MODULE MANAGER & FEATURE TOGGLE WINDOW

In alignment with our Blender-style modularity:

- **Module Manager Window:** Displays installed add-ons and feature packs found in the `modules/` directory.
- **Checkbox Feature Toggle (`[x]` / `[ ]`):** Unchecking a module (e.g., `[ ] v1.0.2 Particle VFX`) instantly removes its corresponding node types from the editor palette and excludes its code from exported build manifests.
