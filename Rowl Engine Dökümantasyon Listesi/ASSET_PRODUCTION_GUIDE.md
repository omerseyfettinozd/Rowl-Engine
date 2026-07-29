# 🎨 ASSET PRODUCTION GUIDE

> **Objective:** Define exact file formats, naming conventions, color spaces, and export settings so artists, musicians, and translators produce assets that plug directly into the engine without rework.

---

## 1. IMAGE ASSETS (BACKGROUNDS, SPRITES, UI)

| Asset Type | Required Format | Resolution | Notes |
| :--- | :--- | :--- | :--- |
| **Backgrounds / CGs** | PNG (lossless) | 1920×1080 minimum | sRGB color space. 4K assets up to 4096×4096 allowed. |
| **Character Sprites** | PNG with transparency | 2048×2048 max | PSD exports must flatten layers and preserve alpha. |
| **UI / Dialogue Frames** | PNG (9-slice required) | 256×256 recommended | Use `.9.png` naming suffix to mark slice guides. |
| **Font Atlas (MSDF)** | PNG (generate via `msdf-atlas-gen`) | 512×512 or 1024×1024 | Must include metadata `.json` for glyph ranges. |
| **Thumbnails / Icons** | PNG (lossless) | 512×512 | Used in editor asset browser. |

### Texture Naming Convention:
```
backgrounds/   -> bg_[chapter]_[scene]_[variant].png         (e.g. bg_ch2_beach_sunset.png)
characters/    -> spr_[character]_[expression]_[layer].png   (e.g. spr_evelin_happy_face.png)
ui/            -> ui_[element]_[state].9.png                 (e.g. ui_dialogue_bubble_active.9.png)
```

### Prohibited Formats:
- **Do not use JPG** for any asset with transparency requirements.
- **Do not use BMP / TGA** (no compression wastes disk and RAM).
- **Do not embed ICC profiles** (sRGB assumed universally).

---

## 2. AUDIO ASSETS (BGM, SFX, VOICE-OVER)

| Asset Type | Required Format | Sample Rate | Bitrate / Notes |
| :--- | :--- | :--- | :--- |
| **BGM (Music)** | Ogg Vorbis or Opus | 44.1 kHz | CBR ~192 kbps. Long tracks MUST be streaming-only. |
| **Voice-Over (VO)** | Ogg Vorbis or Opus | 48 kHz | VBR ~96 kbps. Monophonic audio preferred. |
| **SFX / UI Clicks** | Uncompressed WAV (PCM 16-bit) | 48 kHz | < 2 seconds length. Stored in RAM buffer pool. |
| **Ambient Loops** | Ogg Vorbis (looped metadata) | 44.1 kHz | Seamless loop points required. |

### Audio Naming Convention:
```
audio/
├── bgm/     -> bgm_[chapter]_[mood]_[loop].ogg       (e.g. bgm_ch2_tense_loop.ogg)
├── sfx/     -> sfx_[context]_[action].wav            (e.g. sfx_ui_button_click.wav)
└── voice/   -> vo_[node_id]_[character]_[line].ogg   (e.g. vo_101_evelin_greeting.ogg)
```

---

## 3. SCRIPT & DATA ASSETS (LUA, JSON, STRINGS)

| Asset Type | Required Format | Notes |
| :--- | :--- | :--- |
| **Node Graph Data** | `oyun_verisi.json` (UTF-8) | Editor-generated. Do not hand-edit unless necessary. |
| **Lua Scripts** | UTF-8 `.lua` | Must run inside Lua sandbox (no `io`, `os`). |
| **Game Variables** | JSON or `.rowlstr` binary | Start via JSON for translation; final export to `.rowlstr`. |
| **Translation Files** | UTF-8 JSON | `strings_[lang_code].json` (e.g. `strings_tr.json`, `strings_en.json`). |

### Naming Convention:
```
data/
├── scripts/  -> script_[unique_id]_[type].lua
└── strings/  -> strings_[ISO639-1].json               (e.g. strings_ja.json)
```

---

## 4. VIDEO ASSETS (CUTSCENES)

- **Format:** H.264 MP4 or WebM (VP9).
- **Resolution:** 1920×1080 at 30 fps minimum.
- **Audio:** Embedded AAC or Opus track.
- **Storage:** Streaming-only. Must not be preloaded into RAM.

---

## 5. PACKAGE EXPORT WORKFLOW

1. **Development:** Editor and engine run in **Loose Files Mode**. Assets are raw on disk.
2. **Final Build:** Run `tools/package_assets.py --input ./data --output ./build/game_data.rowlpkg`.
3. **Integrity:** The resulting archive includes a **SHA-256 checksum** per file and a master package checksum.
4. **Distribution:** Ship `.rowlpkg` alongside the runtime executable or bundle inside `.apk` / `.ipa`.

---

## 6. DPI & COLOR SPACE RULES

- **All raster source art must be authored at 2x scale** (e.g., 3840×2160 for 1920×1080 display).
- **Engine scales down via bilinear filtering**; never scale up source art beyond 1.0x to prevent blur.
- **sRGB is the enforced color space** for all monitors and mobile displays. HDR / wide-gamut is deferred to post-v1.0.
