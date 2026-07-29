# 🔬 SUB-SPEC 03: HYBRID VIRTUAL FILE SYSTEM (VFS) & ZSTD STORAGE ENGINE

> **Subsystem Target:** Hybrid File Resolution (Loose Files + Packed Archives), Zstd Compression, Async Threaded Asset Pipeline, and RAM Caching.

---

## 1. ARCHITECTURAL OVERVIEW

Rowl Engine uses a **Hybrid Virtual File System (VFS)** that abstracts physical disk paths away from engine systems. The VFS transparently resolves asset requests regardless of whether the file exists as a raw loose file on disk, inside a third-party mod folder, or compressed within an encrypted `.rowlpkg` game package.

```
                  +-----------------------------------+
                  |  Engine Asset Request ("bg01.png") |
                  +-----------------------------------+
                                    │
                                    ▼
                  +-----------------------------------+
                  |        VFS Resolver Pipeline      |
                  +-----------------------------------+
                                    │
          ┌─────────────────────────┼─────────────────────────┐
          │ (Priority 1)            │ (Priority 2)            │ (Priority 3)
          ▼                         ▼                         ▼
  [ Mods Directory ]      [ Loose Dev Assets ]       [ `.rowlpkg` Archive ]
  `mods/custom_bg/`       `assets/backgrounds/`      `data/game_data.rowlpkg`
  (Uncompressed)          (Uncompressed)             (Zstd Compressed)
```

---

## 2. FILE RESOLUTION PRIORITY & OVERRIDE MECHANICS

When any subsystem requests a path (e.g., `vfs://textures/characters/hero.png`), the VFS checks sources in strict order:

1. **User Mod Overrides (`/mods/<mod_name>/...`):** Highest priority. Allows community modders to replace backgrounds, audio, or scripts by dropping files in the `mods` folder without touching original game files.
2. **Development Loose Assets (`/assets/...`):** Active during editor/testing mode. Enables rapid iteration without packing archives.
3. **Primary Archive Packages (`/data/*.rowlpkg`):** Production release packages. Fast, compressed, and encrypted.

---

## 3. `.rowlpkg` PACKAGE CONTAINER FORMAT SPECIFICATION

All production game assets are bundled into `.rowlpkg` files using Facebook's **Zstandard (Zstd)** dictionary compression.

### Binary Layout Structure:
```
[ 4 Bytes: Magic Cookie "ROWL" ]
[ 2 Bytes: Spec Version (UInt16) ]
[ 4 Bytes: File Count N (UInt32) ]
[ Index Table Offset (UInt64) ]
--------------------------------------------------
[ Payload Block 0: Compressed File Data Chunk 0 ]
[ Payload Block 1: Compressed File Data Chunk 1 ]
...
[ Payload Block N: Compressed File Data Chunk N ]
--------------------------------------------------
[ INDEX TABLE (Zstd Compressed) ]
  ├── File Path Hash (64-bit MurmurHash3)
  ├── Relative Path String (e.g., "audio/bgm/theme.ogg")
  ├── Offset in Archive (UInt64)
  ├── Compressed Size (UInt64)
  ├── Uncompressed Size (UInt64)
  └── Compression Flags (0 = Raw, 1 = Zstd)
--------------------------------------------------
[ 32 Bytes: SHA-256 Package Integrity Checksum ]
```

---

## 4. ASYNCHRONOUS THREADED IO & CACHE MANAGEMENT

To eliminate frame stutters during asset loading (especially on low-end hardware and mobile storage), all file I/O runs asynchronously off the main thread.

```
Main Render Thread                Worker Thread Pool               GPU / RAM
──────────────────                ──────────────────               ─────────
VFS::RequestAsync("bg.png") ───> Push Read Job to Queue
                                         │
                                   Disk Read / Zstd Decompress
                                         │
                                  Texture Memory Prepare
                                         │
Frame Callback Received <──────── Signal Job Complete ──────────> Texture Handle Ready
```

### RAM LRU Cache Eviction:
- **Max Memory Budget:** Default **256 MB** for desktop, **128 MB** for low-end mobile.
- **LRU (Least Recently Used) Eviction:** When memory budget is reached, textures and audio buffers that are no longer referenced by active nodes are automatically purged from RAM (`unloadTexture`).
- **Predictive Lookahead:** The VFS pre-fetches assets required by adjacent branching choice nodes asynchronously before the player makes a choice.

---

## 5. MOBILE VFS ADAPTATION (ANDROID & IOS)

- **Android (`.apk` / `.aab` Assets):** Integrates with Android's `AAssetManager` API. Loose file checks redirect to internal app storage (`/data/data/<package>/files/mods/`), while `.rowlpkg` archives are read directly from the APK asset bundle via zero-copy memory mapping (`mmap`).
- **iOS (App Bundle):** Resolves loose mod paths to `Documents/mods/` and package archives to `MainBundle/`.
