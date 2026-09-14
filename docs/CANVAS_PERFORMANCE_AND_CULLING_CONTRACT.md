# Canvas Performance & Culling Contract (Faz 4 Dilim 1)

Canonical reference for editor canvas virtualization. Sources of truth
are the implementations named below; when this document and code
disagree, code wins and this document must be patched.

## 1. Rule

Only the visible rect materializes Avalonia controls. The canvas binds
`NodeGraphViewModel.VisibleNodes` / `VisibleConnections` (diff-synced);
`MainWindowViewModel.Nodes` / `Connections` remain the untouched masters.

## 2. Spatial index (`CanvasSpatialIndex`)

- Uniform grid, 512 px cells; node bounds `(X-8, Y, 308, NodeCardHeight)`.
- `Add / Move / Remove / Clear / Query`; degenerate rects are rejected,
  edge-touching counts as visible, unknown ids are no-ops. Pure value
  math, no Avalonia/VM types.
- Query cost is proportional to covered cells + candidates, never to
  the graph size. Viewport queries expand by `CullMargin` (128 px) so
  wire overshoot and pin handles never clip.

## 3. View ownership (`NodeGraphViewModel`)

- Observes `MainWindowViewModel` pan/zoom (no growth there) and the
  master collections; tracks per-node moves via `X/Y/NodeCardHeight`.
- Viewport rect (canvas coords): `(-PanX/z, -PanY/z, VW/z, VH/z)`;
  size is pushed by the view (`LayoutUpdated`), default 1280×800.
- `RefreshVisible` diff-syncs both collections in master order, so a
  pan reuses existing controls instead of re-materializing them.
- `BeginBulkUpdate` / `EndBulkUpdate` suspend per-item rebuilds across
  bulk loads (tracking stays O(1)); one rebuild runs at the end.
- Connections are visible when either endpoint is visible (incident
  map rebuilt on collection change).
- `FitToSelection` centers the selected card (F key, plain, no
  modifiers — Ctrl+F stays search); `ZoomToFit` fits `WorldBounds`
  clamped to zoom [0.15, 4.0]; both reuse the existing smooth-view
  animation targets. `PanTo` centers a canvas point immediately
  (minimap drag).

## 4. Minimap (`MinimapControl`)

- One-pass `DrawingContext` render: one dot per node + viewport rect;
  no per-node controls at any scale. Top-right, 220×150.
- Drag maps control points back to canvas coordinates
  (`CanvasFromControlPoint`, null-safe) and pans through the same
  `PanTo` path as keyboard navigation.

## 5. Budgets (reference host, `editor-scale-2000n-6000e-v1` shape)

- Index build over 2.000 nodes: < 2 s (measured ≪ 1 s).
- Six swept culled refreshes (pan + zoom ladder) with brute-force
  parity: < 2 s total.
- Zoomed-in 1920×1080 viewport materializes < 200 node controls.
- Real 60 FPS pan/zoom is a device measurement, not a unit assert;
  the suite proves the per-refresh work is sub-millisecond scale and
  control churn is limited to viewport crossings.

## 6. Test matrix

| Gate | Coverage |
|---|---|
| `editor/Tests/EditorCanvasCullingSlice1Tests.cs` (xUnit, 13) | Index intersect/touch/negative/move/remove/clear guards, 500-rect randomized brute-force parity, viewport pan/zoom math, visible-only culling, endpoint-rule wires, move updates, Fit/Zoom/Pan math, minimap mapping + degenerate nulls, 2.000n/6.000e parity + budgets. |
| `ctest --test-dir build` | Unchanged native suite + `rowl_editor_headless_tests` (runs the xUnit suite including slice 1). |

## 8. Test hygiene (learned the hard way)

`MainWindowViewModel` construction stamps the static project root that
the headless suite's save/load/validate flow reads, and parallel temp
projects must never leak into repo fixtures (`Assets/**` is
copy-source, never a scratch dir). Culling shells therefore restore the
static root compare-and-swap style, and the culling class shares the
`StaticRootSequential` xUnit collection with the headless suite so the
two never overlap. A poisoned `Assets/json/full_story_graph.json`
(nodes 9001/9003) once made this gate fail deterministically; repo
fixtures are asserted clean after every full-suite run.

## 7. Explicitly out of scope (later Faz 4 slices)

Search result lists, filters and color tags, group/subgraph/chapter
canvas semantics, autosave journal / crash recovery, and background
asset/thumbnail workers.
