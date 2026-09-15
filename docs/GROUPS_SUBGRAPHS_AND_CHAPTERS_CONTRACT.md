# Groups, Subgraphs & Chapters Contract (Faz 4 Dilim 3)

Canonical reference for visual canvas groups, subgraph navigation and
chapter file boundaries. Sources of truth are the implementations named
below; when this document and code disagree, code wins and this document
must be patched. Schema ground truth stays in
`docs/GRAPH_VNEXT_CONTRACT.md` (format v5); this slice adds the editor
session, canvas and file layers on top of it.

## 1. Rules

- A `group` is editor-only metadata (title, color, frame, members). It
  never changes runtime execution semantics: nodes keep their ids,
  positions, connections and chapter assignments whether grouped or not.
- A `subgraph` is a navigation scope. From outside it reads as one
  entry/exit-pinned unit; double-click (or `EnterSelectedSubgraph`)
  steps inside, the breadcrumb steps back out. Navigation never mutates
  nodes, connections or runtime state — it only changes which subset the
  canvas materializes.
- A `chapter` is a save/load boundary. Any chapter splits into its own
  file under `chapters/`; merging restores the exact full graph. A legacy
  single-file v4 graph keeps loading as one implicit default chapter.

## 2. Visual groups (`GroupService`, `CanvasGroupViewModel`)

- `editor/ViewModels/CanvasGroupViewModel.cs` — frame state
  (`GroupId/Title/Color/X/Y/Width/Height`, `MemberNodeIds`, min
  80×60). Pure math helpers: `Contains` (edges inside),
  `Intersects` (edge-touching visible), `FrameAroundBounds`
  (tight union + padding, default 48 px). `ToRecord/FromRecord`
  map to the v5 `CanvasGroup` persistence record.
- `editor/Services/GroupService.cs` — session owner
  (`ObservableCollection<CanvasGroupViewModel> Groups`, attached to the
  master node collection). `Create/CreateFromNodes/Delete`,
  `MoveGroup(id, dx, dy)` (frame + every member node share the delta;
  non-members never move; returns the translated member count),
  `ResizeGroup` (min-clamped), `FrameToMembers` (re-tighten around live
  members), `MembersOf/GroupsOf` (groups may overlap),
  `LoadFrom/ToRecords/Clear`.
- Canvas: `NodeGraphView` renders `NodeGraphViewModel.VisibleGroups`
  (diff-synced, master order) behind wires and cards. Header drag moves
  frame + members, double-click reframes, corner thumb resizes, `×`
  deletes the frame (nodes stay). Gestures run on tunnel so they never
  reach the marquee/pan bubble handlers.
- Culling (Dilim 1 integration): group rects live in their own
  `CanvasSpatialIndex` inside `NodeGraphViewModel` (own key space,
  same 512 px grid + 128 px margin). Under an active navigation scope a
  group stays only while at least one live member passes the scope
  (empty frames stay editable); unscoped canvases always show them.
- Minimap: `MinimapControl.GroupsSource` draws translucent backdrops
  (40/255 alpha of the frame color, slate fallback, cached brushes)
  behind the node dots, in the same letterbox projection.

## 3. Subgraph navigation (`SubgraphNavigationService`)

- `editor/Services/SubgraphNavigationService.cs` — definitions, depth
  stack, `ObservableCollection<NavCrumb> Crumbs`, chapter filter and the
  scope predicate. `MainWindowViewModel` only constructs it and forwards
  `ScopeChanged` to `NodeGraphViewModel.RefreshScope`.
- Scope rule (`IsNodeVisibleInScope`): chapter filter first (exact id
  match), then depth — at root every unsubgraphed node plus each
  subgraph's boundary (entry + exit) nodes pass while interior-only
  members hide; inside a subgraph only its member set passes. Wires need
  both endpoints in scope (unscoped canvases keep the Dilim 1
  either-endpoint rule bit-for-bit).
- Breadcrumbs: `Root › [Chapter] › Subgraph…`. `NavCrumb.Depth` is always
  a subgraph-stack depth for `GoToDepth` (root = 0 + clears the chapter
  filter; the chapter segment = 0 with the filter kept). `Enter/Exit/
  GoToRoot/GoToDepth/TryEnterForNode`; unknown ids are ignored (false),
  never throw. A definitions reload resets depth (stale depth is worse
  than root) but keeps the chapter filter.
- Boundary display: `IsBoundaryNode/GetBadge` (`◧ Title` entry,
  `Title ⏏` exit) refresh onto the display-only `NodeViewModel.
  IsSubgraphBoundary/SubgraphBadge` props (same pattern as Dilim 2
  `FilterOpacity`; listed in the search index visual-only set so badges
  never dirty the index). The card badge double-click enters the scope
  and pans to the entry node center.
- Port discipline stays in `GraphStructureValidator` (Faz 1 Dilim 5);
  the service exposes `TryGetEntry/GetExits/OwnerOf` read helpers.

## 4. Chapters (`ChapterStorageService`)

- `editor/Services/ChapterStorageService.cs` — definitions
  (`LoadDefinitions/ToRecords/Clear`), `EffectiveChapters` (defined
  chapters plus the implicit `"default"` / `"Ana Bölüm"` chapter when
  unassigned nodes exist), `NodeIdsOfChapter`.
- File layout under `chapters/`:
  - `chapter_index.json`: `format_version`, `start_node_id`,
    `node_order` (global id list), `chapters`, `groups`, `subgraphs`.
  - `<chapter_id>.json`: `format_version: 5`, `chapter_id`, `nodes`
    (full node payloads, document order preserved per chapter).
- `Split(fullGraphJson, chaptersDir)`: buckets nodes by `chapter_id`
  (missing → `default`, auto-created in the manifest), writes files
  atomically, returns chapter ids in manifest order. Splitting a legacy
  v4 graph promotes it to an explicit `default` chapter (one-way,
  documented; loading v4 stays untouched).
- `Merge(chaptersDir)`: joins files, restores the exact global node
  order via `node_order`, dedupes structure from the manifest, writes
  v5 only when structure/chapter assignments exist (else v4).
  Duplicate node ids across files are a hard error; manifest-unknown
  nodes are appended by ascending id instead of dropped.
  `Merge(Split(x))` round-trips parsed-equal (compare parsed JSON, not
  raw bytes: whitespace/key order are not significant).
- Cross-chapter edges stay legal: `next_nodes` reference global node
  ids, which merge preserves. `LoadChapterFile` reads one chapter file
  (`chapter_id` + node payloads) for partial flows.
- Editor wiring: `MainWindowViewModel.ApplyLoadedStructure` (load path)
  and `CurrentStructure` (every save path: snapshot `Capture`, legacy
  `SaveFullGraph`, split source). `SplitChaptersToFiles/
  MergeChaptersFromFiles` commands round-trip through
  `full_story_graph.json` + `LoadFullStoryGraphFile`, reusing the
  transactional load.

## 5. Save-path repair (this slice)

The pre-slice snapshot saver (`StoryGraphSaveService`) wrote a hardcoded
`format_version: 4` and dropped `chapter_id`, `metadata` (color tags)
and all vNext structure on every toolbar save. It now captures
`ChapterId/ColorTag/Tags` per node plus the composed `Structure`, and
emits v5 under the same omission rules as `StoryGraphSerializer`
(absent = omitted, never null). Untagged/unassigned graphs stay
byte-stable v4 — the MS-2 parity test still passes unchanged.

## 6. Architectural boundaries

- `engine.cpp`, `window.cpp`, `EngineHost.cs`: 0 diff (no native or
  bridge changes in this slice).
- `MainWindowViewModel.cs`: construction + ~10 thin delegations
  (structure apply/compose, group/subgraph/chapter commands). All frame,
  stack and file logic lives in the three services.
- `NodeGraphViewModel.cs`: culling owner extended (group index,
  `VisibleGroups`, `ScopePredicate`); no pan/zoom/selection growth.

## 7. Test matrix

| Gate | Coverage |
|---|---|
| `editor/Tests/EditorGroupsSubgraphsChaptersSlice3Tests.cs` (xUnit, 16) | Group frame math, header-drag member translation, resize clamp + reframe, metadata-only save (v4 byte-stability + v5 round-trip), group culling parity + scope gating, depth stack + breadcrumb trail, root boundary rule + interior hiding + wire gating, port/entry-exit helpers, badge display props, chapter split/merge round-trip + duplicate-id error, v4 backward compat (implicit default), snapshot v5 persistence, search-index immunity to badge props, MainVM end-to-end delegation. |
| `ctest --test-dir build` | Unchanged native suite + `rowl_editor_headless_tests` (runs the xUnit suite including slice 3). |

Test hygiene from Dilim 1/2 still applies: `MainWindowViewModel`
construction stamps the static project root, so the slice-3 shell shares
the `StaticRootSequential` collection and restores the root
compare-and-swap style; temp projects/files live under the OS temp dir,
never under repo fixtures.

## 8. Explicitly out of scope (later Faz 4 slices)

Inspector chapter/group editors, group multi-select + bulk ops,
subgraph collapsed-proxy rendering (this slice uses boundary-pin
scoping), chapter lazy-loading into the runtime (Faz 5 chapter index),
linter issue navigation, autosave journal / crash recovery, background
asset/thumbnail workers.
