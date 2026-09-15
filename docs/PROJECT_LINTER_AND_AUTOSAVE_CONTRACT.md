# Project Linter & Autosave Contract (Faz 4 · Dilim 5)

Birleşik batch lint (`ProjectLintService`), build kapısı, deep-nav issue
odağı, crash-recovery journal ve arka-plan tarama kuralları.

## 1. Birleşik lint (`editor/Services/ProjectLintService.cs`)

`Lint(nodes, connections, assetsPath, startNodeId?, structure?, lintOptions?)`
önce `ProjectValidationService.Validate(...)` sonucunu alır (`AddRange`
eder — aynı `ProjectValidationIssue` tipi, dönüşüm yok), sonra yalnızca
lint'e özel kuralları ekler. Devralınan kurallar tekrar yazılmaz:
reachability / terminal / cycle, `CheckContentIds` duplicate + format,
disk-index + case-collision + asset existence / case / format,
`GraphStructureValidator.Validate`.

- **Tek tarama bütçesi (2000-node p95):** `BuildDiskIndex` bir kez çalışır;
  `Validate` çekirdek overload'ı (`Validate(..., DiskIndex)`) aynı dizini
  kullanır. Karmaşıklık O(N+E+dosya). Ölçüm, culling sonrası
  `RefreshVisible` içindeki mevcut sayaçlardadır; lint kare döngüsüne
  girmez (yalnızca `AnalyzeStoryGraph` ve build öncesi).
- **Kısıtlar:** `NodeViewModel` / `ConnectionViewModel` public API +
  `Serialize()` dışında reflection yok; dosya IO yalnızca `assetsPath`
  altındadır; kural hatası throw değil issue üretir
  (`RunRule` → tek warning'e indirgenir); `IsEnabled == false` bileşenler
  ve seçenekler atlanır.

### Yeni kurallar (yalnızca lint'te; inline'a eklenmez)

| # | Kural | Severity |
|---|-------|----------|
| 1a | `Scripts/` altı `.lua` ön-tarama: boş dosya / `MaxLuaBytes` (256 KB) üstü | warning |
| 1b | `require` / `dofile` / `loadfile` literal'i Assets dışına çıkıyor (`..` kaçışı, absolute, sürücü kökü) | error |
| 1c | choice option `TargetNodeId == 0` veya silinmiş node; condition `fail_target_node_id` bozuk / `0` / silinmiş | error |
| 2a | `DialogueText` / `Speaker` boş (inline ile aynı severity) | warning |
| 2b | `Assets/locales/*.json` kapsama: content_id eksik (missing) veya `source_hash` bayat (changed); bozuk katalog tek warning | warning |
| 2c | Şekillenemez glif özeti: kontrol karakteri (TAB/LF/CR hariç), U+FFFD, noncharacter | warning |
| 3 | Kullanılmayan kaynak: `ExactPaths` MINUS referans aday kümesi (normalized + `images/`/`audio/`/`fonts/`/`scripts/`); `NodeId = null`, `AssetPath` dolu | warning |

Kural 3 kapsamı: `.recovery/` defteri ve yük-taşıyan JSON'lar
(`scripts/` altı hariç) elenir; yalnızca başvurulabilir dosyalar
(`images|audio|fonts|scripts/` altı veya bilinen medya/`.lua` uzantısı)
adaydır. Kural başına `MaxIssuesPerRule` (200) tavanı vardır.

## 2. Build kapısı (değişmedi — kilitli)

Yeni kapı açılmadı. İki mevcut kapı korunur:

- `EditorBuildCoordinator.BuildStandaloneGameAsync` (:72-90)
- `ProjectBuildService.ExecuteBuildPipeline` (:100-118)

`Validate` + `Any(IsError)` → `Succeeded = false`, `Cancelled = false` +
`Diagnostic(Code = ValidationFailed, Severity = Error,
Operation = build_validation)`; output dizini oluşmaz.
Error bloklar (içerik id bozuk/duplicate, hedef 0, proje-dışı path,
converter-pending, unsupported ext, missing asset, case-mismatch,
case-collision, kayıp bağlantı, start yok, boş graf); warning geçirir
(unreachable, terminal, cycle, boş title/speaker/text, isolated inline,
disk scan incomplete). Warning tek başına asla bloklamaz.

## 3. Deep-nav issue odağı

`MainWindowViewModel.FocusIssueNode(ulong?)` (~15 satır, yalnızca
delegasyon): `null` veya bulunamayan id'de return → zorunlu sıra
`Subgraphs.TryEnterForNode(id)` (breadcrumb + scope senkronu) →
`Search.JumpTo(node)` (`SelectNodeQuiet` + hedef-merkez `PanTo` +
`IsSearchHighlighted` #FACC15, 1500 ms generation-guard). `DepthStack`
direkt kurcalanmaz; `PanTo` scope'tan sonra gelir (aksi halde hedef hâlâ
culled olur). `ProjectIssuesViewModel.Focus` tek satırdır;
`NodeId = null` (global/asset issue) sessiz no-op'tur.

## 4. Crash recovery (`editor/Services/CrashRecoveryService.cs`)

UI-free, `StoryGraphSaveSnapshot` alır, asla throw etmez:

- `ScheduleSave` → `Assets/json/.recovery/dirty.flag` koyar; başarılı
  `TryWriteSnapshotWithRecovery` kaldırır.
- Journal: `.recovery/journal-YYYYMMDD.jsonl` — satır başına
  `{seq, utc, sha256, format_version, snapshot}` + `Flush(true)` (fsync);
  rotate: son 50 dosya veya 10 MB toplam.
- Her başarılı yazımda kanonik `full_story_graph.json` →
  `.recovery/last-good.json` + `.sha256` sidecar (atomik).
- Startup `CheckAtStartup`: parse + nodes array + `TryParse` structure;
  bozuk/eksikte otomatik overwrite **yok** — `TryBuildRestoreOffer`
  (önce en yeni geçerli journal girdisi, yedek hash-doğrulanmış
  last-good) kullanıcı onayına sunulur; `RestoreOfferToCanonical`
  yalnızca `RestoreFromRecovery` komutundan çağrılır.
- Dispose: debounce **veya** dirty.flag varsa senkron flush; çift yazım
  başarılı olursa flag temizlenir.
- Regresyon kilitleri: `ApplyLoadedStructure` hatası artık yutulmuyor —
  `LoadGraphWithRollback` tüm yükü geri alıyor (v5→v4 sessiz downgrade
  yok); v4 load→save byte-stable (boş `chapter_id`/`metadata` omit);
  split/merge `format_version` kuralını korur.

## 5. Arka-plan tarama (`editor/Services/AssetScanWorker.cs`)

`OffscreenRuntimeWorker` deseninin yönetilen kopyası: ayrı owner-thread +
`BlockingCollection<Action>` + `Invoke<T>` (TCS + bloklayan; validation
için) + `TryPost` (fire-and-forget; thumbnail için, hata worker'ı
öldürmez) + `Dispose` (`CompleteAdding` + `Join`). Standalone SDL host UI
thread'de kalır; ağır build mevcut `Task.Run + CancellationToken +
staging-dir` desenindedir. `AnalyzeStoryGraph`, canlı koleksiyonları UI
thread'de anlık kopyalar (`ToList`), disk+lint'i worker'da koşar,
sonucu dispatcher ile geri post'lar; dirty bayrağına dokunmaz.

## 6. Testler

`editor/Tests/EditorProjectLinterSlice5Tests.cs` (xUnit, ≥12 fact): her
kural + error-blocks/warning-passes + deep-nav + journal-recovery +
round-trip regresyonları + worker semantiği + 2000-node bütçesi.
