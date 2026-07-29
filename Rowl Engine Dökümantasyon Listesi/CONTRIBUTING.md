# 📐 NAMING CONVENTIONS & CONTRIBUTION RULES

> **Objective:** Ensure every contributor writes code that reads like one unified codebase, regardless of author, language, or subsystem.

---

## 1. C++ NAMING CONVENTIONS (Engine Core)

Follow these rules strictly for all C++20 code under `engine/include/` and `engine/src/`:

| Element | Convention | Example |
| :--- | :--- | :--- |
| Namespace | PascalCase snake-case hybrid | `Rowl::Audio::DspFilters` |
| Class / Struct | PascalCase | `class AudioEngine` |
| Method | camelCase | `play_audio(...)` |
| Variable | snake_case | `m_buffer_pool_size` |
| Constant / enum | SNAKE_CASE | `AUDIO_TYPE_STREAMING` |
| File name | snake_case.hpp / .cpp | `audio_engine.hpp` |
| Private members | Prefix with `m_` | `m_active_sources` |
| Static members | Prefix with `s_` | `s_instance_count` |

### Memory & Smart Pointer Rules:
- Use `std::unique_ptr` for exclusive ownership.
- Use `std::shared_ptr` only for shared ownership (e.g., cross-subsystem references, persistent state trees).
- Use `std::weak_ptr` to break reference cycles (especially in persistent/immutable state systems).

---

## 2. C# NAMING CONVENTIONS (Editor)

All C# code under `editor/Src/` follows standard Microsoft .NET naming rules:

| Element | Convention | Example |
| :--- | :--- | :--- |
| Class | PascalCase | `IpcClientViewModel` |
| Interface | `I` prefix + PascalCase | `IDataSource` |
| Method | PascalCase | `ConnectAsync()` |
| Local variable | camelCase | `pipeClientStream` |
| Private field | `_` underscore prefix | `_pipeName` |
| File name | PascalCase | `IpcClient.cs` |

---

## 3. FLATBUFFERS NAMING CONVENTIONS

Schema files (`.fbs`) must follow these rules for cross-language consistency:

- **Table / Struct:** PascalCase (`NodeData`, `HandshakeReq`)
- **Enum Value:** SCREAMING_SNAKE_CASE (`HandshakeResp`)
- **Field Name:** snake_case (`editor_version`, `node_id`)
- **Root Type:** Always ends with `Envelope` (`MessageEnvelope`)

---

## 4. LUA NAMING CONVENTIONS (Scripts & Mods)

Community mods run inside the Sandbox and should follow Lua best-practices:

- **Global Variables:** None allowed. Use `local` only.
- **Module Tables:** Prefix with `rowl_mod_` to avoid collision (`local rowl_mod_inventory = {}`).
- **Engine API Calls:** Always use `rowl.*` namespace (`rowl.var.get_number(...)`).
- **File Naming:** snake_case with `.lua` extension (`minigame_puzzle.lua`).

---

## 5. COMMIT MESSAGE CONVENTIONS

All git commits must follow the Angular-style Conventional Commits:

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Type:** `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`, `revert`
**Scope:** `core`, `vfs`, `ipc`, `editor`, `audio`, `lua`, `state`, `mobile`
**Example:**
```
feat(audio): add telephone DSP filter preset

Implements band-pass filter for phone-call scenes.
Fixes editor dropdown exposure in node inspector.
```

---

## 6. CODE REVIEW & CONTRIBUTION WORKFLOW

1. Fork the repository.
2. Create a feature branch: `feat/<short-description>`.
3. Ensure all new code compiles on **Linux GCC, Windows MSVC, and Android Clang**.
4. Add GoogleTest unit tests for any new engine component.
5. Update the relevant `sub-specs/` or `phases/` document if changing architecture.
6. Open Pull Request with a summary referencing the affected spec file.

---

## 7. SECURITY & MODDING RULES

- Never submit mods that attempt to bypass Lua sandbox restrictions.
- Third-party `modules/` directories require **SHA-256 manifest signatures** to load in production (to prevent malicious asset replacement).
- Do not store secrets (keys, tokens) in documentation or asset files.
