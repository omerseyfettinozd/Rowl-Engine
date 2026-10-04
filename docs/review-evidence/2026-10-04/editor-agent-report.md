# Rowl Engine editor audit — 2026-10-04

Scope: 42 files reviewed selectively, not all editor files read end-to-end. Inventory: /tmp/rowl-audit-editor-files.txt. Root: /home/chaple/.codex/worktrees/435f/Rowl Engine. No repository source changed. Static evidence unless explicitly marked reproduced. General xUnit/headless runs owned by root agent; GUI display/input/audio/device interaction not verified here.

## Assessment

Editor authoring foundation: 7.0/10 provisional. Engineering/services: 8.0; actual author workflow reliability: 5.5; graph/search/inspector capabilities: 8.0; UI/product proof: 6.0; test design: 7.5. This is a usable, substantial editor with a large regression investment, but save/discard/build boundaries prevent claiming release-ready author safety.

## Existing strengths verified in source

- Graph culling, minimap, groups/subgraphs/chapters and result-list search EXIST now. NodeGraphViewModel.cs:14-18,66-81; Views/Panels/NodeGraphView.axaml:32,76,93,159-166; MainWindow.axaml:300-301,357. PRODUCTIZATION_BASELINE.md still says these do not exist: dated inventory understates current code.
- Translation desk EXISTS: missing/changed/translated status, filters, CSV round trip, atomic catalog saving, pseudo locale. LocalizationDeskViewModel.cs:94-105,133-185. A polished multirow editing/unsaved translation workflow was not verified.
- MP3/FLAC/WebP conversion pipeline EXISTS with accepted source conversion, sidecar hashes and freshness. EditorAssetImportService.cs:78-97; MediaConverterService.cs:333-350,375-415. Baseline partial import claim is older than implementation.
- Build staging, cancellation and release verifier EXIST. ProjectBuildService.cs:292-334,336-359. Existing output is preserved (273-275). This is meaningful protection against partial published builds.
- Snapshot capture/background save, recovery journal and loader rollback EXIST: MainWindowViewModel.cs:2150-2243; StoryGraphSaveService.cs:426-455; StoryGraphLifecycleCoordinator.cs:83-103,115-124. Remaining issues are workflow integration and concurrency, not absence of persistence.
- xUnit coverage is broad (90+ test source files, approximately 22.5K test-source lines). Culling has randomized index correctness tests; undo tests round-trip choice targets, not just counts (EditorCanvasCullingSlice1Tests.cs:150-176; EditorUndoIntegrityFacts.cs:78-119). Test quantity is not GUI/device proof.

## Findings

### E1 — P1 / Kritik: Discard is followed by a shutdown save

Reproduced using actual MainWindowViewModel and EditorProjectLifecycleCoordinator in a temporary headless project. AutoSave disabled; SaveProjectNow baseline → edit → ScheduleSave → discard resolver → Dispose. Output: DIRTY_MARKER_BEFORE_DISCARD=True; DISCARD_CLOSE_ALLOWED=True; VM_DIRTY_AFTER_DISCARD=False; DIRTY_MARKER_AFTER_DISCARD=True; DISK_CHANGED_AFTER_DISCARD_DISPOSE=True; DISCARDED_EDIT_ON_DISK=True. Probe: /tmp/rowl-editor-discard-probe/Program.cs. Native/real window interaction not required or run. MainWindow.axaml.cs:27-38 routes Closing through ResolveUnsavedChanges, then Closed through vm.Dispose. EditorProjectLifecycleCoordinator.cs:31-34 handles discard only by setting IsProjectDirty false. MainWindowViewModel.cs:1772-1784 sets a recovery dirty.flag and starts debounce; neither is cancelled/cleared by discard. Dispose at 2610-2625 sees debounce or recovery flag and writes active/full graphs anyway. No OnIsProjectDirtyChanged cleanup exists. User selects “Kaydetmeden çık” but edited data is still persisted. Existing lifecycle test (EditorProjectLifecycleTests.cs:40-48) only checks delegate booleans, never Dispose or files. Recommendation: explicit close decision with discard cancelling outstanding save/debounce/recovery work; integration test comparing disk bytes after close/discard. Status: unsaved dialog exists, safe discard product behavior partial.

### E2 — P1 / Kritik: Save failure result is discarded before Save As and build

SaveAs reproduced with actual unchanged ProjectSaveAsCoordinator.cs + ProjectFileSystem.cs compiled into /tmp/rowl-editor-audit-probe; output SAVE_FAILED_BUT_COPY_SUCCEEDED=True, COPIED_GRAPH=old-disk-document. MainWindowViewModel.cs:2166-2191 returns bool from SaveProjectNow; SaveProjectToDirectory:2383-2389 and BuildGameAsync:2425-2433 adapt it to Action. ProjectSaveAsCoordinator.cs:22,33 proceeds regardless; EditorBuildCoordinator.cs:69,76 likewise. If graph save fails (disk permission/full/capture error), stale disk graph can be copied/exported with success. Recommendation: Func<bool>/typed save result and abort before target staging; exact failing-save tests. Status: save/build exists, fail-closed handoff partial.

### E3 — P1 / Önemli: Build bypasses full linter and structure validation

EditorBuildCoordinator.cs:81 calls ProjectValidationService.Validate without CurrentStructure. ProjectBuildService.cs:100 does same. Overload ProjectValidationService.cs:16-17 passes null structure, so structural checks at 134-142 are skipped. ProjectLintService.cs:125-139 contains lint-only rules; choice target 0/deleted nodes are ERROR at 200-211; condition and unsafe Lua errors are also linter-owned. Full lint is only user-triggered AnalyzeStoryGraph (MainWindowViewModel.cs:2494-2522); build does not consume its result. Therefore error can appear in Analysis yet Build runs a different smaller gate. Recommendation: use same detached full-graph lint snapshot in build, keeping warning/error policy explicit. Status: linter exists, complete build-blocking linter product integration missing.

### E4 — P2 / Önemli: Save As drops SourceAssets

Reproduced with actual SaveAs code: SOURCEASSETS_COPIED=False. ProjectSaveAsCoordinator.cs:35-38 copies only Assets; 40-64 writes manifest; never SourceAssets. New build pipeline depends on projectRoot/SourceAssets (EditorBuildCoordinator.cs:77-80) and conversion uses source-path provenance. Copy works at runtime with converted outputs but is incomplete as an editable project archive: raw originals and automatic reconversion capability disappear. Recommendation: define project-copy inventory including SourceAssets, use staged clone, test reconversion from copied project. Status: Save As exists, full author project clone partial.

### E5 — P2 / Önemli: Import conversion blocks UI despite async entry point

MainWindowViewModel.ImportAssetAsync:1705 awaits picker but 1710 calls synchronous ImportAssetFiles. EditorAssetImportService.cs:48-49 blocks GetAwaiter().GetResult around external conversion. Single image/audio pickers likewise block at 139-140 / 184-185. MP3 decode/OGG encode/large batches therefore keep UI blocked until completion or converter timeout; async service already exists but not used. Recommendation: await async import, progress/cancel and marshal UI collection updates. Status: conversion exists, responsive importer partial.

### E6 — P2 / Önemli: Recovery exists as code but lacks a user action

MainWindowViewModel.cs:668-672 stages PendingRecoveryOffer and logs literal developer instruction “RestoreFromRecovery ile geri yükleyin”; command exists at 678-688. Search across editor/Views finds no PendingRecoveryOffer or RestoreFromRecovery binding/menu. Normal author cannot activate staged recovery from the UI. Recommendation: actionable recovery dialog/banner with source/date, restore/inspect/dismiss; full startup-to-restore UI integration test. Status: recovery storage exists, discoverable restore product surface missing.

### E7 — P2 / Önemli: Imports overwrite same-name assets without collision workflow

EditorAssetImportService.cs:105-109,153-158,198-203 flattens filenames into images/audio and File.Copy(overwrite:true). Two external files named hero.png replace the first globally; referencing scenes immediately show new content and import is not undoable. Recommendation: preserve-relative-path or rename/replace/cancel choice; explicit replacement confirmation and rollback. Status: file import exists, collision-safe author workflow partial.

### E8 — P2 / Önemli: Build conversion resumes on worker and uses live UI graph

EditorBuildCoordinator.cs:79-82 awaits SourceAssets conversion with ConfigureAwait(false), then enumerates live nodes/connections and invokes reportIssues directly. MainWindowViewModel.cs:2429-2440 supplies live ObservableCollections and updates ProjectIssuesViewModel; SetIssues directly Clear/Add (ProjectIssuesViewModel.cs:26-27). After real asynchronous conversion, callbacks can run outside UI thread; edits during await can also make disk snapshot and validation graph differ. Contrast: normal AnalyzeStoryGraph uses detached CaptureGraph and UIThread.Post (2499,2522). Recommendation: capture immutable build input before await, run validation off-thread, dispatch UI progress/issues explicitly. Status: background export exists, consistent thread-safe build boundary partial. Static finding; GUI failure not reproduced.

### E9 — P2 / İkincil: Recovery journal's 10 MiB total bound is soft

CrashRecoveryService.cs:43-44 declares 50 files / 10 MiB. Append at 148-152 writes full snapshots into one daily journal. Rotate at 175-180 only deletes while journals.Count>1; a single current-day file can grow arbitrarily beyond 10 MiB. Large graph + many autosaves means sizable disk growth. Recommendation: chunk/rotate within day or bounded snapshot ring, preserving newest valid record; test single-day oversized journal. Status: recovery exists, bounded disk contract partial.

### E10 — P2 / İkincil: “End-to-end” gate is programmatic and accepts invalid media fixtures

EditorEndToEndFlowTests.cs:37 opens ViewModel with connectEngine:false; author actions occur through methods/properties rather than pointer/key UI. PNG and WAV fixtures at 72-74 are only four-byte headers, not decodable assets. Preview tests at 85-98 check scene push/change coalescing; export at 127-142 checks artifacts. Valuable integration coverage but cannot prove rendered image, audio playback, actual picker/drag/keyboard, exported player UX or valid media. MediaConverterSliceTests.cs:18-22 explicitly uses fake tool runner/native delegates. Recommendation: preserve these fast tests, add real Golden Project editor interaction and exported-player proof separately. Status: test foundation exists, complete user journey visual/media proof external.

## Validation performed

- Static source call-chain and UI-binding inspection, 42 targeted files.
- Actual headless ViewModel discard/Dispose probe reproduced unintended persistence with autosave disabled.
- Actual SaveAs-source temporary C# probe succeeded after MSBuildEnableWorkloadResolver=false.
- Initial ordinary dotnet run failed due host SDK workload resolver MSB4242: workload set version 10.0.111.1 has missing manifests. No system repair/install made.
- No general xUnit suite or GUI/device run by this agent. Root reported its general managed gate passed 510/510 with MSBuildEnableWorkloadResolver=false; this report does not independently certify that run.
