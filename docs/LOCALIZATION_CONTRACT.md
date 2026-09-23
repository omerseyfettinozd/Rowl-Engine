# Localization Contract — Manifest Locales, Catalogs, Runtime Selection (Faz 3 Dilim 1)

Canonical reference for the Faz 3 Dilim 1 localization foundation. Sources of
truth are the implementations named below; when this document and code
disagree, code wins and this document must be patched.

## 1. Concepts

| Concept | Owner | Meaning |
|---|---|---|
| manifest locale declaration | `project.rowlproj` | Default + supported locale codes. `snake_case` (`default_locale` / `supported_locales`) and `camelCase` (`defaultLocale` / `supportedLocales`) spellings are equivalent. |
| locale catalog | `Assets/locales/<locale>.json` | `content_id → { speaker, text, alt_text }` table for one locale. |
| fallback chain | native `LocalizationManager` | Resolution order: active locale → default locale → node original text. Unknown keys never error. |
| capability | C ABI | `CAPABILITY_LOCALIZATION = (1 << 7) [128]`, additive. |

## 2. Manifest shape (keys optional; absent = legacy)

```json
{
  "name": "Second Signal",
  "default_locale": "tr",
  "supported_locales": ["tr", "en"]
}
```

- Missing keys (e.g. `first_light`) → default `"en"`, supported `["en"]`.
- Malformed JSON → same `"en"` fallback; the project still loads.
- A default outside the supported list is prepended, never dropped.
- Tags normalize (lowercase, `_` → `-`) but are preserved whole — region
  and script subtags are validated, never stripped. Resolution is
  chain-aware: `matchSupported` walks the BCP 47 fallback chain
  (`"tr-TR"` → `"tr-tr"` → `"tr"`), so `"tr-TR"` stays a distinct tag
  that resolves down its own chain instead of being truncated to `"tr"`.
  (Native `LocalizationManager` scope — see §5 for the managed-side difference.)

## 3. Catalog shape (`schema_version: 1|2`)

```json
{
  "schema_version": 1,
  "locale": "tr",
  "entries": {
    "<content_id>": {
      "speaker": "Margot",
      "text": "Röle cızırtıyla uyanıyor.",
      "alt_text": "Bir telsiz operatörü bekliyor."
    }
  }
}
```

- `locale` must match the file name; every entry needs string
  `speaker` / `text` / `alt_text` (empty strings allowed).
- `schema_version` 1 or 2 is accepted; anything else rejects the load.
  (Native runtime scope; the managed `LocalizationService.ValidateCatalog`
  in §5 currently accepts only 1 — see §5.)
  Malformed rows are skipped and counted (`skippedEntries`), so one
  translator slip no longer vetoes the whole locale — only a broken
  document (bad schema, locale mismatch, non-object `entries`) rejects
  the load with prior state untouched.
- A missing or malformed catalog stays unloaded; that locale resolves
  through the fallback chain instead of blocking the project mount.
- Reference fixture: `samples/second_signal/Assets/locales/{en,tr}.json`.

## 4. C ABI (additive; older entry points untouched)

| Entry point | Contract |
|---|---|
| `RowlEngine_SetLocale(handle, locale)` | `OK` on switch; `INVALID_HANDLE` for a dead handle; `INVALID_ARGUMENT` for null/empty/unsupported codes (state unchanged). Tags normalize. |
| `RowlEngine_GetLocale(handle, buffer, size, outRequired)` | Caller-buffer contract (null/0 = size query; undersized = `BUFFER_TOO_SMALL`). Fresh handles report `"en"`. |
| `RowlEngine_GetSupportedLocalesJson(...)` | Same contract; UTF-8 JSON array (e.g. `["en","tr"]`). |
| `RowlEngine_GetCapabilities` | Now includes `CAPABILITY_LOCALIZATION`; all older flags intact. |

Implementation ownership: `engine/include/rowl/i18n/` +
`engine/src/i18n/localization_manager.cpp` (rules) and
`engine/src/c_api_i18n.cpp` (ABI + project-mount bootstrap).
`engine.cpp`, `window.cpp` gain no logic; the `Engine` only hosts the
manager. Mount wiring is one call in `SetProjectDirectory`
(`engine/src/c_api_story.cpp`).

## 5. C# bridge and profile

- P/Invoke: `NativeBridge.RowlEngine_SetLocale / GetLocale /
  GetSupportedLocalesJson` (`editor/Src/Native/NativeBridge.cs`).
- Logic: `editor/Services/LocalizationService.cs` — manifest parsing,
  catalog validation, `EffectiveLocale` (profile preference ∩ supported,
  else manifest default, else `"en"`), and handle-seamed native
  selection. `EngineHost` and `MainWindowViewModel` are untouched.
- Managed-side divergence (native-parity gap, xUnit-locked):
  `LocalizationService.NormalizeLocale` truncates to the primary subtag
  (`"tr-TR"` → `"tr"`, locked by
  `ParseManifest_RegionTagsNormalizeToPrimarySubtag`), and
  `ValidateCatalog` accepts only `schema_version` 1 (locked by
  `ValidateCatalog_RejectsWrongSchemaAndLocaleMismatch`) and returns on
  the first malformed row instead of skip-and-count. Native behavior
  (§2/§3) is the forward contract; managed parity (or an explicit
  split-contract statement) belongs to a later Faz 3 slice and must update
  this section plus the xUnit locks together.
- `PlayerProfile.Language` (existing, `en` default, sanitized against
  known languages) is the preference source; no profile schema change.

## 6. Limits

- Catalog files larger than 16 MiB are skipped at mount time.
- Locale codes: 1–32 ASCII alphanumerics after normalization.
- Manifest documents larger than 1 MiB are ignored (fallback applies).

## 7. Test matrix

| Gate | Coverage |
|---|---|
| `tests/test_localization.cpp` (CTest) | Both manifest spellings, legacy/malformed fallback, default-outside-list, tag normalization, catalog accept/reject, full fallback chain, canonical `second_signal` catalogs, capability bits, null-handle guards, fresh-handle fallback, mount applies declaration. |
| `editor/Tests/EditorLocalizationSlice1Tests.cs` (xUnit) | Manifest parsing, catalog validation, `EffectiveLocale`, fail-closed native seams without the native library, capability value. |
| `tests/test_demo_packaged.py` + `test_golden_manifest.py` | Unchanged: `second_signal` locale fixtures still checksum-verify. |

## 8. Explicitly out of scope (later Faz 3 slices)

Rich-text markup, shaping/rendering (FreeType/HarfBuzz/FriBidi/
libunibreak), editor translation table, pseudo-locale, CSV/JSON exchange,
font-license/glyph coverage checks, and per-node localized dialogue
resolution through the story runtime.
