# Localization Editor Contract — Translation Desk, Pseudo-Locale, CSV/JSON Exchange (Faz 3 Dilim 4)

Canonical reference for the editor Translation Desk. Sources of truth are
the implementations named below; when this document and code disagree, code
wins and this document must be patched.

## 1. Concepts

| Concept | Owner | Meaning |
|---|---|---|
| source inventory | `TranslationInventoryService` | Every `type == "dialogue"` component in `full_story_graph.json` (`Assets/json` first, legacy `Assets` copy as read fallback) keyed by canonical `content_id`. Components with a missing/invalid id are skipped; later duplicates of one id win. |
| source hash | `TranslationInventoryService.ComputeSourceHash` | SHA-256 hex of `speaker + "\0" + sourceText`. Stored per entry as optional `source_hash`. |
| desk row | `TranslationStatusService.TranslationRow` | Source snapshot (`SourceSpeaker`/`SourceText`, never edited) plus catalog values (`Speaker`/`TranslatedText`/`AltText`, editable). |
| row state | `TranslationStatusService` | `Missing` = absent entry or empty text; `Changed` = stored `source_hash` differs from the current source; `Translated` = otherwise. Entries that predate `source_hash` count as `Translated` while they carry text (`LegacyNoHash`) — the next desk save stamps the hash and arms Changed detection. |
| pseudo-locale | `PseudoLocaleGenerator` | Deterministic overflow test data, tag `qps-ploc`, file `Assets/locales/qps-ploc.json`. |
| exchange document | `TranslationExchangeService` | RFC 4180 CSV (`CRLF`, UTF-8): `content_id,speaker,source_text,translated_text,alt_text`. |

## 2. Catalog shape (additive to Dilim 1)

```json
{
  "schema_version": 1,
  "locale": "tr",
  "entries": {
    "<content_id>": {
      "speaker": "Margot",
      "text": "Röle cızırtıyla uyanıyor.",
      "alt_text": "Bir telsiz operatörü bekliyor.",
      "source_hash": "9f2c…64 hex…"
    }
  }
}
```

- `source_hash` is optional and ignored by the native runtime loader and by
  strict validation (both read only `speaker`/`text`/`alt_text`), so old
  catalogs keep loading untouched.
- The desk writes entries sorted by `content_id` with 2-space indentation
  plus a trailing newline, deterministically.

## 3. Desk behavior

- Language pickers: source/target locale; the available list comes from
  `project.rowlproj` (`supported_locales`), falling back to `["en"]`.
- Filters: `All`, `MissingOnly`, `ChangedOnly`. Search matches speaker,
  source text, translated text and `content_id`, case-insensitively.
- Row edit stages into memory; **Save** writes the whole catalog atomically
  (temp file + move via `ProjectFileSystem.WriteAllTextAtomically`) and
  re-validates the generated JSON before writing — a validation failure
  writes nothing. Catalog entries outside the inventory (orphans) are
  preserved untouched.
- The status line always shows `N satır: T çevrilmiş, M eksik, C değişmiş`
  plus any load/save warnings; I/O failures never throw out of the desk.

## 4. Pseudo-locale

- Every Latin letter maps to a wider accented lookalike
  (`"Play Game"` → `"[!!! Ƥľȧẏ Ɠȧṁē !!!]"`); digits, punctuation, CJK and
  emoji pass through; `<...>` markup spans pass through verbatim so pseudo
  catalogs never break the markup parser.
- Generation is deterministic (no randomness): byte-stable golden tests.
- One click writes `Assets/locales/qps-ploc.json` with current
  `source_hash` values, so the desk reports it as Translated until a
  source line changes. The desk reader prefers the literal file stem
  (`qps-ploc.json`) over the normalized code (`qps.json`); raw stems
  containing separators or `..` are never trusted.

## 5. CSV exchange (fail-closed, atomic)

- Export quotes fields containing `"`, `,` or line breaks (doubled quotes).
- Import stages the whole document first: a wrong header, a short/long
  row, a non-UUID `content_id`, a stray quote or an unterminated quoted
  field rejects the **entire** document and the rows stay untouched.
- Unknown `content_id` values are skipped with a warning count (never
  created); an empty `translated_text` marks the row Missing.
- JSON export reuses the catalog shape in §2 and always validates.

## 6. Architecture ownership

- `MainWindowViewModel` grows by one delegating command
  (`OpenLocalizationDeskCommand` → `LocalizationDeskCoordinator.OpenDesk`);
  `EngineHost` is untouched. All logic lives in
  `editor/Services/Localization/*`, `editor/ViewModels/Localization/*` and
  `editor/Views/Localization/*` (standalone modeless window, single-instance
  with focus-steal instead of duplicates).

## 7. Test matrix

| Gate | Coverage |
|---|---|
| `editor/Tests/EditorLocalizationSlice4Tests.cs` (xUnit) | Inventory scan/skip/duplicate rules, hash stability, Missing/Changed/Translated/Legacy mapping, filters + search, counts, atomic save roundtrip + orphan preservation + invalid-locale refusal, pseudo exact-string/tag-passthrough/determinism/catalog validity, CSV roundtrip with quoting + CRLF, all import rejection paths + atomicity + unknown-key skip + empty→Missing, JSON export validity, headless ViewModel refresh/filter/save/import/pseudo end to end in a temp project. |
| `ctest --test-dir build` (`rowl_editor_headless_tests`) | Runs the whole xUnit suite including slice 4. |

## 8. Explicitly out of scope

Machine-translation providers, per-node runtime dialogue resolution,
font-license/glyph coverage checks, and right-to-left layout preview.
