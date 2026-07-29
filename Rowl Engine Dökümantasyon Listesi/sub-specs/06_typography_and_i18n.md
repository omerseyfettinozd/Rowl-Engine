# 🔬 SUB-SPEC 06: MSDF TYPOGRAPHY, LOCALIZATION (i18n) & GPU TEXT SHADERS

> **Subsystem Target:** MSDF Font Rendering Engine, HarfBuzz/FriBidi Complex Text Shaping, Binary i18n String Tables, Rich Text Tags, and GPU-Driven Text Shaders.

---

## 1. ARCHITECTURAL OVERVIEW

Text is the primary medium of Visual Novels. Rowl Engine's typography engine guarantees razor-sharp text rendering across any screen resolution (from 720p phones to 4K displays), supports complex international scripts (Turkish, CJK, RTL Arabic), and offloads text animation processing to GPU shaders.

```
+-------------------------------------------------------------------+
|                     TEXT RENDERING PIPELINE                       |
|                                                                   |
| [ Raw String Key / UTF-8 ] ---> [ Binary i18n Hash Lookup O(1) ]  |
|                                                  │                |
|                                                  ▼                |
| [ GPU Text Vertex Shader ] <--- [ HarfBuzz / FriBidi Shaping ]    |
|   - Typewriter Offset             (Ligatures, BiDi, CJK)          |
|   - Wave / Shake Effect                          │                |
|   - MSDF Multi-Channel Texture                   ▼                |
|                                       [ MSDF Glyph Cache ]        |
+-------------------------------------------------------------------+
```

---

## 2. MSDF (MULTI-CHANNEL SIGNED DISTANCE FIELD) FONT ENGINE

Standard raster fonts pixelate when scaled, and single-channel SDF fonts blur sharp corners. Rowl Engine implements **MSDF (Multi-channel Signed Distance Field)** rendering.

- **How MSDF Works:** Glyphs are encoded into RGB channels representing distance vectors to glyph edges.
- **Benefits:**
  - **Infinite Crisp Scaling:** Fonts remain razor-sharp at 4K resolution or when zooming into dialogue text.
  - **Zero Memory Bloat:** A single 512x512 MSDF texture atlas holds an entire font family.
  - **Dynamic Outlines & Shadows:** Text borders, drop shadows, and glows are calculated dynamically in the fragment shader without generating extra textures.

---

## 3. COMPLEX SCRIPT SHAPING & i18n LOCALIZATION

Visual Novels must support global languages flawlessly.

### Text Shaping Layer (HarfBuzz + FriBidi):
- **Full UTF-8 / UTF-16 Support:** Native rendering for Turkish (`ç, ş, ğ, ü, ö, ı, İ`), CJK (Japanese Kanji/Kana, Chinese Hanzi), and European accents.
- **Right-To-Left (RTL) & BiDi:** Integrated **FriBidi** algorithm automatically handles RTL scripts (Arabic, Hebrew) mixed with LTR numbers or English words.
- **Complex Ligatures:** **HarfBuzz** ensures correct glyph contextual joining for Arabic and Devanagari scripts.

### Binary Compiled i18n String Tables (`.rowlstr`):
- **Development Mode:** Translators edit human-readable `strings_tr.json`, `strings_ja.json`, `strings_en.json`.
- **Production Export:** JSON files are compiled into **Binary 64-bit Hash Tables (`.rowlstr`)**.
- **O(1) Instant Language Swapping:** Switching languages in the options menu resolves string keys in constant $O(1)$ time with zero string parsing delay.

---

## 4. RICH TEXT TAGS & PARSER

Dialogue text supports inline markup tags parsed at node load time:

```xml
<speaker="Evelyn">
"Watch out! That <color=#FF0000><b<shake>>ANCIENT DRAGON</shake></b></color> is waking up!"
</speaker>
```

### Supported Tags:
- `<color=#HEX>` / `<color=red>`: Text color override.
- `<b>`, `<i>`, `<u>`: Bold, Italic, Underline styling.
- `<size=32>`: Local font size adjustment.
- `<speed=0.05>`: Temporary typewriter speed change.
- `<shake intensity=2.0>`: Per-character GPU shaking animation.
- `<wave speed=3.0 amplitude=5.0>`: Per-character sinusoidal waving animation.
- `<pause=1.0>`: Inserts a 1-second delay in typewriter output.

---

## 5. GPU-DRIVEN TEXT SHADERS ("TOST MAKİNESİ" OPTIMIZATION)

Traditional engines animate typewriter text or shaking letters by manipulating string arrays or vertex buffers on the CPU every frame, causing CPU spikes.

- **GPU Text Vertex Shader:** 
  - Each character glyph vertex carries an attribute index and timestamp.
  - **Typewriter Effect:** The vertex shader checks `if (char_index > u_typewriter_progress) alpha = 0.0;` on the GPU.
  - **Text Shake / Wave:** Sinusoidal offsets ($y = A \cdot \sin(\omega t + \phi)$) are calculated entirely inside the GPU vertex shader.
- **CPU Load:** **0% CPU usage** for complex text animations.
