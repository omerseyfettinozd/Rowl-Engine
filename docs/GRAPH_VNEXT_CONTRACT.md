# Graph vNext Contract — Group / Subgraph / Chapter (Format v5)

Canonical reference for the Faz 1 Dilim 5 story-graph extension. Sources of
truth are the implementations named below; when this document and code
disagree, code wins and this document must be patched.

## 1. Concepts

| Concept | Owner | Meaning |
|---|---|---|
| `group` | editor canvas only | Visual frame: title, color, rect, member node list. No runtime semantics; the native loader keeps the data but never acts on it. |
| `subgraph` | editor + native contract | Modular sub-flow: flow may **enter** the member set only through `entry_node_id` and **leave** it only through an `exit_node_ids` member. |
| `chapter` | runtime section marker | Save/load boundaries, backlog clustering and profile progress resolve through the `chapter_id` assigned to each node. |

Ids live in per-kind namespaces: a group, a subgraph and a chapter may share
an id string without colliding.

## 2. JSON shape (all vNext keys optional; absent = v4)

```json
{
  "format_version": 5,
  "start_node_id": 101,
  "nodes": [ { "id": 101, "chapter_id": "ch1", "...": "..." } ],
  "groups": [
    { "id": "g1", "title": "Act", "color": "#3B82F6",
      "x": 0, "y": 0, "width": 9, "height": 9, "node_ids": [101] }
  ],
  "subgraphs": [
    { "id": "sg1", "title": "Trial", "entry_node_id": 101,
      "exit_node_ids": [102], "node_ids": [101, 102] }
  ],
  "chapters": [
    { "id": "ch1", "title": "Arrivals", "order": 0,
      "summary": "Meet.", "start_node_id": 101 }
  ]
}
```

- Node `chapter_id`: optional string (≤128 chars). Empty/absent = unassigned.
- Chapter `start_node_id`: optional entry hint, no auto-jump semantics (flow
  ownership stays with Faz 2).
- The writer emits **v5 only when the document carries structure or chapter
  assignments**; otherwise output stays byte-stable **v4**. Absent sections
  are omitted, never written as `null` (explicit nulls are rejected).
- v4 documents (golden projects `first_light`, `second_signal`,
  `rowl-golden-project-v2`) load unchanged: no groups, one implicit chapter.

## 3. Validation rules (errors, identical in C# and native)

1. Duplicate group / subgraph / chapter ids.
2. Group / subgraph / chapter entries referencing missing nodes.
3. Subgraph entry or exit that is not a member of its own member set.
4. One node in two subgraphs (membership is disjoint; groups may overlap).
5. Port discipline: an edge leaving a member set must start at an exit node;
   an edge entering a member set must land on the entry node.
6. Entry-to-exit reachability: every exit must be reachable from the entry
   through member-internal edges.
7. Node `chapter_id` naming an undefined chapter; chapter `start_node_id`
   naming a missing node.
8. Subgraph call-graph cycles: direct member-to-member boundary crossings are
   calls (`A → B`); a cycle has no call-stack semantics at runtime and is
   rejected with the cycle path. Story loops stay legal **inside** one
   subgraph or **outside** all subgraphs — plain node cycles are unaffected.

Warnings (editor linter only): groups/subgraphs with no members, chapters
with no assigned nodes.

## 4. Runtime & C API surface (additive)

- `StoryRuntime::currentChapterId()` — chapter of the current node, empty
  when unassigned. `Engine::getCurrentChapterId()` /
  `getStoryGraphDocument()` are header-inline; `engine.cpp` is untouched.
- `ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT (8)`; result-coded caller-buffer
  queries `RowlEngine_GetCurrentChapterIdUtf8`,
  `RowlEngine_GetChapterCount`, `RowlEngine_GetChapterIdAtUtf8`
  (order-sorted: `order`, then `id`; out-of-range index is
  `ROWL_RESULT_INVALID_ARGUMENT`). v4 graphs report count 0 and an empty
  current chapter with `ROWL_RESULT_OK`.
- C# mirror: `GraphStructureModels` (records + limits),
  `GraphStructureHydrator` (shape parsing),
  `GraphStructureValidator` (rules), `NodeViewModel.ChapterId`,
  `ProjectValidationService.Validate(..., structure)` overload,
  `NativeBridge` chapter P/Invoke declarations. `MainWindowViewModel`,
  `EngineHost`, `engine.cpp` and `window.cpp` are untouched by design;
  canvas UI for groups/subgraphs/chapters belongs to Faz 4.

## 5. Limits

Groups ≤4096, subgraphs ≤1024, chapters ≤1024, members per entry ≤10000,
exits per subgraph ≤1024; ids ≤128 chars, titles ≤512, summaries ≤4096.
Overruns are validation failures, matching the existing story-parser style.

## 6. Tests

- xUnit `EditorGraphVNextTests` (10): v4 stability, v5 round-trip, golden
  v4 samples, orphan/duplicate/port/cycle/exit rules, warning-only empty
  group, malformed-section load failure.
- Headless Test 34: v5 round-trip + orphan-chapter gate.
- Native `test_graph_vnext`: parse/runtime/golden/matrix; `test_c_api_contract`
  chapter surface (null/invalid/small-buffer/order); `test_c_api_header.c`
  capability static assert.
