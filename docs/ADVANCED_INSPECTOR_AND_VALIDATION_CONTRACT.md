# Advanced Inspector & Validation Contract (Faz 4 Dilim 4)

Canonical reference for the Inspector schema, dynamic field widgets,
chapter/group assignment and reactive inline validation. Sources of truth
are the implementations named below; when this document and code disagree,
code wins and this document must be patched.

## 1. Rules

- Every inspectable field is described once (`InspectorFieldDescriptor`):
  widget kind, range limits, enum options, asset family. Values stay on
  the view models; descriptors carry no state.
- Field problems surface where the field lives (inline badge under the
  editor), the moment the value changes — and as `ProjectValidationIssue`
  records, the same type the Issues panel and the Dilim 5 linter consume.
- Severity mirrors `ProjectValidationService`: malformed/duplicate
  content ids, dangling option targets and unresolvable asset paths are
  errors; empty speaker/text/title and isolated nodes are warnings.

## 2. Schema (`editor/Services/Inspector/`)

- `InspectorField.cs` — `InspectorFieldKind`
  (Text, MultilineText, Range, Toggle, Enum, Color, Asset, Chapter),
  `InspectorAssetKind` (Image, Audio, Script, Font),
  `InspectorFieldOption`, and the record `InspectorFieldDescriptor`
  (Key, Label, Kind, ComponentType, Min/Max/Step, Hint, AssetKind,
  Options). `Keys` holds the stable validation field keys
  (`node.title`, `dialogue.speaker`, `background.texture`, …).
- `InspectorSchemaProvider.cs` — static. `NodeFields` (title, chapter)
  plus per-component sections for `dialogue`, `background`, `character`,
  `audio`, `choice` and `script`. Ranges mirror the limits already
  hardcoded in the component views (e.g. speaker font 10–64 step 2, box
  opacity 0–1 step 0.05), so the schema documents rather than changes
  accepted values. Unknown component types yield an empty schema (their
  bespoke editors keep working; progressive coverage).

## 3. Dynamic widgets (`editor/Controls/`)

- `RangedNumberControl` — slider + numeric box on one TwoWay double
  (Minimum/Maximum/Step). Used for the dialogue font sizes.
- `ColorPickerControl` — `Palette` mode assigns a node color tag
  (preset swatches + clear + hex readout via `NodeColorTags.ToHex`);
  `Hex` mode edits a raw `#RRGGBB` string (preview chip, commit on
  Enter/focus-loss, invalid input reddens the box and never propagates).
  Used for the node tag and the dialogue hex colors.
- `AssetPickerControl` — path field plus an optional Browse button
  (bound to the existing MainVM picker commands; hidden when no command
  is bound). Existence/format problems surface through a badge under the
  control, never inside it.
- `FieldValidationBadge` — binds `Validation` +
  `NodeId` + `FieldKey`, shows the first error (red ⛔) else the first
  warning (amber ⚠) with the full message as tooltip, collapsed when
  clean. Refresh is event-driven (`Validation.Changed`); subscriptions
  are rehook-safe.

## 4. Chapter & group assignment (`InspectorViewModel`)

- Chapter dropdown (`ChapterAssignmentOptions`, `SelectedChapterOption`;
  `"Yok"` = unassigned) writes `SelectedNode.ChapterId` and rebuilds the
  canvas chapter-filter options (the implicit default chapter appears and
  vanishes with unassigned nodes).
- Group chips (`MemberGroups` with ×) and join dropdown
  (`JoinableGroups` + `SelectedJoinGroup` + Ekle) mutate frame membership
  through `GroupService`; frames themselves still live on the canvas.
- `RegenerateSelectedContentIdCommand` assigns a fresh UUID to the first
  enabled dialogue; the content_id badge clears reactively.
- `MainWindowViewModel` needed only a construction-order move (structure
  services before the Inspector) — no new logic there by design.

## 5. Inline validation (`InspectorValidationService`)

- Attached once to the live node/connection collections (+ an Assets-path
  provider). Property changes revalidate the owner node only (O(local));
  connection edits rebuild degrees and revalidate all (rare path);
  content_id duplicates keep an incremental normalized→owners index.
- Rules: empty title (warning), isolated node (warning, multi-node
  graphs), empty dialogue text / empty speaker-with-text (warnings),
  malformed content_id (error), duplicate content_id on later owners
  (error, first use stays clean — same as the batch validator), enabled
  choice option with `TargetNodeId == 0` (error), empty option text
  (warning), asset references via the same enabled-component /
  `AssetKeys` / blank-is-optional enumeration as the batch validator
  with identical severities (`InspectorAssetRules`, plus a direct-hit
  fallback for files imported after the last index refresh).
- Unlike the batch validator, inline checks ignore start-node
  reachability (authoring strictness — unreachable work still gets
  badges). Disabled components and their fields are always skipped.
- Subscriptions are leak-safe: node/frame/component collections unhook
  `OldItems` on every change; `Detach` and collection `Reset` rebuild
  the hook set before revalidating.

## 6. Test matrix

| Gate | Coverage |
|---|---|
| `editor/Tests/EditorAdvancedInspectorSlice4Tests.cs` (xUnit, 14) | Node schema fields, dialogue range limits, unknown-component empty schema, empty speaker/text warnings, invalid + duplicate content_id errors (+ disabled skip), isolated-node warning + option-target error, asset missing/existing/outside/unsupported/pending severities + blank silence, keystroke reactivity, chapter assignment round-trip, group join/leave, content_id regen clears badge, `IssuesFor` filtering, linter-foundation record shape, hex parse/normalize. |
| `ctest --test-dir build` | Unchanged native suite + `rowl_editor_headless_tests` (runs the xUnit suite including slice 4). |

Test hygiene from Dilim 1–3 still applies: the `StaticRootSequential`
collection, compare-and-swap static-root restore, OS-temp projects/files
only (file-IO tests use explicit temp dirs, never the static root).

## 7. Explicitly out of scope (Dilim 5 — Linter)

Batch project linter (topology, Lua/condition, translation, glyph and
unused-resource audits), error-blocks-build gating, issue→node/component
deep navigation, autosave journal / crash recovery and background
asset/thumbnail workers. The inline `Issues` collection is the merge
source the linter will consume.
