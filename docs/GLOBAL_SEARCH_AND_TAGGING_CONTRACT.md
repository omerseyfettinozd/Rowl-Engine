# Global Search & Tagging Contract (Faz 4 Dilim 2)

Canonical reference for project-wide node search, result focus, color
tags and the filter bar. Sources of truth are the implementations named
below; when this document and code disagree, code wins and this document
must be patched.

## 1. Rule

The search box (Ctrl+F) lists ranked results with a field badge and a
text excerpt; picking a result (click or Enter) quiet-selects the node,
centers the canvas on the card through the Dilim 1 `PanTo` path and
flashes a temporary amber highlight. Filters (node kind, speaker, color
tag) narrow the result list and dim filtered-out cards on the canvas and
the minimap. `MainWindowViewModel` keeps only the box text and the
toggle; `EngineHost` is untouched.

## 2. Search stack (`editor/Services/Search/`)

- `NodeSearchField` — taxonomy + Turkish badge labels + rank weights
  (title/id 0, speaker/content/tag/kind 1, dialogue/choice 2,
  variable/condition 3, asset/script 4).
- `NodeSearchQuery` — parser. Bare tokens match any field; `field:value`
  restricts one field (`title, id, speaker, text` (= dialogue + choice),
  `dialogue|dialog, choice|option, content_id|contentid|cid,
  variable|var, condition|cond, asset|bg|portrait|sound|bgm|sfx,
  script|lua|code, tag|color|colour, type|kind`). Tokens combine with
  AND. Unknown prefixes degrade to literal text; `field:` with an empty
  value is ignored. Empty queries return zero hits, never throw.
- `NodeSearchDocument` — per-node snapshot (title, id, speakers, dialogue
  texts, choice texts, content ids, variables, conditions incl. option
  conditions, asset paths, script path/code, color tags, kinds). Original
  casing is kept; matching is ordinal-ignore-case.
- `NodeSearchIndex` — pure document store + ranked search (weight, then
  node id), `MaxResults = 200` display cap with uncapped `TotalCount`
  and full `MatchingNodeIds`. `Snippet` windows ±30 chars, single line,
  max ~120 chars.
- `NodeSearchService` — owns the index for one graph. Collection
  add/remove is incremental (O(1) per node); content edits mark that
  node dirty via node + component + tags subscriptions and refresh
  lazily on the next query. Position/selection/highlight/dimming
  properties never dirty the index. Connection changes recompute the
  outgoing map lazily (jump kinds refresh then).
- `NodeColorTags` — 8-name palette
  (`red, orange, yellow, green, blue, purple, pink, gray`), lowercase
  normalization, slate fallback for unknown values, cached Avalonia
  brushes (normal + dimmed).

## 3. Semantic node kinds (derived, never stored)

`dialogue` (enabled dialogue component), `choice` (≥1 option),
`condition`, `variable`, `script`, `transition`, `camera`,
`audio` (non-empty BGM/SFX track), `jump` (≥1 outgoing edge with no
dialogue text and no choices — a pure routing node). A node carries a
set of kinds; `type:` matches one exactly (case-insensitive).

## 4. View ownership (`SearchViewModel`)

- Constructed by `MainWindowViewModel` (single delegation line) and
  attached to `Nodes`/`Connections`. Exposes `Results`,
  `SelectedResult` (change jumps), kind/speaker/color-tag selections
  with `Available*` option lists, `StatusText` (`"N sonuç · M ms"`),
  `LastQueryMs`, `TotalCount`.
- `JumpTo` = `SelectNodeQuiet` (no save storm) + `NodeGraphViewModel.PanTo`
  on the card center + 1.5 s amber highlight (`IsSearchHighlighted`,
  generation-guarded, dispatcher-safe clear).
- Active query or filters dim the rest of the canvas to
  `FilteredOutOpacity` (0.25) via `NodeViewModel.FilterOpacity`; clearing
  restores 1.0. Dimming never affects culling, save or the index.
- Keyboard: Down/Up in the box moves the selection, Enter jumps to it
  (skipped while playing standalone), Esc closes and clears.

## 5. Color tags (JSON, backward compatible)

- Node-level `metadata` object: `{ "color_tag": "red",
  "tags": ["act1", "finale"] }`. Read fail-soft in
  `StoryGraphNodeHydrator` (non-string/blank/overlong entries skipped,
  cap 32 tags × 64 chars).
- Written by `StoryGraphSerializer` only when a tag or list tag exists;
  untagged nodes stay byte-stable v4 and the native parser (which reads
  known keys with defaults) keeps loading them.
- Unknown `color_tag` values round-trip untouched and render with the
  slate fallback brush.
- Assignment UI: inspector "🏷 Etiket" dropdown (`ColorTagAssignmentOptions`);
  display: card top strip (`NodeControl`, layout-neutral overlay) +
  per-tag minimap dots (dimmed variant for filtered-out nodes, live
  repaint on tag/dim changes).

## 6. Budgets (reference host, 2.000-node mixed-content graph)

- Single query over 2.000 indexed nodes: **< 50 ms** (measured single
  milliseconds; suite asserts worst and average over 12 representative
  queries × 3 repeats).
- Index attach/rebuild over 2.000 nodes stays in bulk-load territory
  (same order as the Dilim 1 < 2 s gate); per-edit refresh is O(1) per
  dirty node.

## 7. Test matrix

| Gate | Coverage |
|---|---|
| `editor/Tests/EditorGlobalSearchSlice2Tests.cs` (xUnit, 16) | Case-insensitive title/speaker/text match, per-field filters, AND tokens, empty/literal edge cases, field badge + snippet, semantic kinds incl. jump, 2.000-node <50 ms budget + spot correctness, incremental add/edit/remove freshness, tag round-trip, legacy omission + untagged load, unknown-tag preservation + fallback, palette/normalization, filter dimming + restore, jump select/pan/highlight/clear, main-box end-to-end, result-select jumps. |
| `ctest --test-dir build` | Unchanged native suite + `rowl_editor_headless_tests` (runs the xUnit suite including slice 2). |

Test hygiene from Dilim 1 still applies: `MainWindowViewModel`
construction stamps the static project root, so shell tests share the
`StaticRootSequential` collection and restore the root
compare-and-swap style; temp projects live under the OS temp dir, never
under repo fixtures.

## 8. Explicitly out of scope (later Faz 4 slices)

Group/subgraph/chapter canvas semantics, linter issue navigation,
autosave journal / crash recovery, background asset/thumbnail workers,
regex/fuzzy matching, search-result multi-select and bulk tag editing.
