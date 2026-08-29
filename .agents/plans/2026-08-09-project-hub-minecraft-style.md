# Rowl Engine — Çoklu Proje Hub Planı (Minecraft Tarzı)

**Tarih:** 2026-08-09
**Durum:** Onaylandı — implementasyon beklemede
**İstek:** Uygulama ilk açıldığında direkt editör yerine proje oluşturma / listeleme ekranı gelsin; Minecraft dünyaları gibi her proje ayrı klasörde, ayrı listelenen kart olsun; kart üzerinde çark → isim değiştir / kapak resmi ekle / sil (çift onay)

---

## Goal

Rowl Engine Editor'ü tek-proje modundan çıkarıp çoklu proje yönetimine geçirmek. İlk açılışta **Project Hub** penceresi gösterilecek, kullanıcı orada proje oluşturacak (isim + klasör seçimi), oluşturulan projeler kart listesinde görünecek, her kartın çark menüsünden isim değişikliği, kapak görseli ataması ve çift onaylı silme yapılabilecek. Seçilen proje çift tıklama / Aç ile `MainWindow` içinde `ProjectRoot` olarak açılacak.

## Success Criteria

- [ ] Editor `dotnet run` ile başladığında `MainWindow` doğrudan değil, `ProjectHubWindow` açılır.
- [ ] "Proje Oluştur" → isim + klasör seçici → `<seçilenKlasör>/<projeAdı>/` altında `Assets/{images,json,packages}`, `Assets/full_story_graph.json`, `Assets/json/full_story_graph.json`, `project.rowlproj` oluşturulur.
- [ ] Hub'da tüm projeler Minecraft tarzı grid kart listesi olarak görünür (isim, kapak, yol, son açılma tarihi).
- [ ] Kart üzerindeki ⚙️ çarka tıklayınca menü: `İsim Değiştir`, `Kapak Resmi Ekle/Değiştir`, `Projeyi Sil`.
- [ ] İsim değiştir → `project.rowlproj` içindeki `name` ve klasör adı atomik güncellenir, liste yenilenir.
- [ ] Kapak resmi → dosya seçici (png/jpg/webp) → proje köküne `cover.png` (veya `Assets/cover.png`) olarak kopyalanır, `project.rowlproj.coverImage` güncellenir.
- [ ] Sil → Dialog 1: "Emin misin?" (Evet/Hayır) → Evet ise Dialog 2: "Gerçekten emin misin? Bu işlem geri alınamaz." → Evet ise klasör `Directory.Delete(recursive:true)` + registry'den kaldırma.
- [ ] Mevcut tek proje (`/home/chaple/Belgeler/Rowl Engine/Assets/project.rowlproj`) registry boşken otomatik import edilir; mevcut dosyalar kaybolmaz.
- [ ] `ProjectRoot` artık static auto-resolve değil, Hub'dan seçilen projenin yolu olarak set edilir; `LoadFullStoryGraphFile()` / `SaveFullStoryGraphFile()` yeni root'a göre çalışır.
- [ ] `--headless-test` suite'i yeni akışla uyumlu kalır (gerekirse ProjectRoot override ile).

## Context And Current Facts

- **Mevcut akış tek projelik:** `editor/App.axaml.cs:17-25` doğrudan `new MainWindow { DataContext = new MainWindowViewModel() }` yaratır; `MainWindowViewModel` ctor `LoadFullStoryGraphFile()` çağırır ve `ResolveProjectRoot()` (`MainWindowViewModel.cs:33-71`) ile `bin/Debug/net10.0`'dan yukarı yürüyerek `Assets/` + `editor/` veya `CMakeLists.txt` içeren kökü bulur. Yani proje konumu sabittir ve kullanıcı seçemez. Kaynak: `editor/ViewModels/MainWindowViewModel.cs:31`, `editor/App.axaml.cs:17`
- **Kaydet / Aç mevcut ama Hub değil:** `SaveProject()`, `SaveProjectAsAsync()`, `OpenProjectAsync()`, `SaveProjectToDirectory()` (`MainWindowViewModel.cs:1988-2186`) klasör seçici ile çalışır fakat launcher yok; kullanıcı menüden manuel açmak zorunda ve projeler listesi tutulmaz.
- **Proje manifesti minimal:** `Assets/project.rowlproj` şu an sadece `{ name, version, engineVersion, savedAt, nodeCount, startNodeId, virtualResolution }` tutar; `coverImage`, `path`, `createdAt`, `lastOpened` yok. Kaynak: `Assets/project.rowlproj:1`, `MainWindowViewModel.cs:2173-2185`
- **Varlık kökü:** Tüm asset I/O `MainWindowViewModel.AssetsPath` (`ProjectRoot/Assets`) ve alt klasörleri üzerinden. `ProjectRoot` static mutable (`public static string ProjectRoot { get; set; }`) olduğu için yeni proje seçiminde set edilebilir — API müsait.
- **UI stack:** Avalonia 11.3.11, Fluent theme, `CommunityToolkit.Mvvm` (`editor/RowlEngine.Editor.csproj:16-20`). Mevcut pencereler: `MainWindow.axaml`, `NodeGraphView`, `ProjectAssetsView`, `EnginePreviewControl` vb.
- **Build/test:** `build/` altında `libRowlEngineCore.so` üretiliyor, `editor` bunu `RowlNativeLib` olarak kopyalıyor. Headless testler `Program.cs:51-219` içinde; proje kaydet/aç/build akışını doğrular.
- **Git durumu:** `Assets/full_story_graph.json` ve türevleri modifiye ama henüz commit değil (`git status` - M).

## Constraints And Non-goals

**Constraints:**
- Mevcut projeler bozulmamalı; otomatik migrasyon şart.
- `ProjectRoot` static olduğu için thread-safe değil — sadece UI thread'den set edilmeli.
- Cover image boyutu büyük olabilir; 5MB üstü ve 4096x4096 üstü için uyarı/kırpma gerekir.
- Silme geri alınamaz — çift onay metni tam olarak istenen gibi olmalı: 1) "Emin misin?" 2) "Gerçekten emin misin?".

**Non-goals (bu planda YAPILMAYACAK):**
- Bulut senkron / proje paylaşım / versioning.
- Proje şablonları (boş vs. örnek sahne) — ilk fazda sadece boş + default 2 node'lu graph kopyalanacak.
- Proje içi arama / etiketleme / favori.
- Engine `mods/` desteğini etkilemek — `ProjectRoot/mods` mount point korunacak.

## Key Decisions

| Karar | Seçilen | Neden | Reddedilen Alternatif |
|-------|---------|-------|----------------------|
| Registry konumu | `~/.config/RowlEngine/projects.json` (Linux) + `AppData/Roaming/RowlEngine/projects.json` (Win) + `~/Library/Application Support/RowlEngine/projects.json` (macOS) — fallback olarak `Environment.SpecialFolder.ApplicationData` | XDG uyumlu, proje klasöründen bağımsız, birden fazla repo klonunda çalışır | Repo köküne `.rowl_projects.json` yazmak — git ile çakışır, taşınabilir değil |
| Registry formatı | `List<ProjectInfo>` JSON: `{ id(guid), name, path(absolute), coverImage(relative), createdAt, lastOpenedAt }` | Basit, serialize kolay, mevcut `project.rowlproj` ile çakışmaz | SQLite — aşırı mühendislik |
| Project manifest genişletmesi | `project.rowlproj` içine `coverImage`, `createdAt` ekle; registry ana kaynak, manifest yedek | Manifest proje klasörüyle taşınır, registry global listeyi tutar — çift tutarlılık | Sadece registry — proje klasörü başka makineye kopyalanınca kapak kaybolur |
| Launcher mimarisi | `ProjectHubWindow` (ayrı Window) → seçim sonrası `MainWindow` yarat; `App.axaml.cs` startup'ta Hub'ı açar, Hub `OnProjectOpened` event'i ile MainWindow'u oluşturur | Mevcut `MainWindow`'u bozmaz, testler `MainWindowViewModel(projectRoot)` overload ile headless kalabilir | MainWindow içinde `IsHubVisible` toggle — layout karmaşık, 1600x900 Hub için uygun değil |
| Cover depolama | Proje kökünde `cover.png` (veya seçilen uzantı korunarak `cover{ext}`) + `Assets/cover.png` kopyası; `project.rowlproj.coverImage = "cover.png"` relative | Dosya seçicide önizleme kolay, silince tek dosya silinir | Sadece `Assets/images/cover.png` — kullanıcı projenin görsel kökünü karıştırabilir |
| İsim değiştirme implementasyonu | Registry `name` + `project.rowlproj.name` güncelle + **klasör rename** (`Directory.Move(oldPath, newPath)`) + registry `path` güncelle; isimde illegal char (`/ \ : * ? " < > |`) engelle | Kullanıcı Minecraft gibi isimle klasörü özdeş görür | Sadece manifest rename — klasör ismi eski kalır, kafa karıştırır |
| Silme akışı | İki ayrı `MessageBox` / `ConfirmDialog` penceresi ardışık: Dialog1 "Bu projeyi silmek istediğine emin misin? [Evet/Hayır]" → Dialog2 "Gerçekten emin misin? Tüm dosyalar kalıcı olarak silinecek. [Evet, Sil / İptal]" | İstek tam olarak bu; tek dialog yetmez | Tek dialog + checkbox — istek dışı |
| Yeni proje iskeleti | `Directory.CreateDirectory(target/Assets/{images,json,packages})` + `full_story_graph.json` için ya boş template ya da mevcut `Assets/full_story_graph.json`'dan kopya (hub'dan "Boş Proje" vs "Örnek Sahne ile" seçimi olmadan şimdilik boş+1 node) | En az sürpriz | Mevcut projenin tüm Assets'ini kopyalamak — yeni proje kirli başlar |

## Recommended Approach

1.  **Model + Servis katmanı önce:** `editor/Services/ProjectRegistryService.cs` ve `editor/Models/ProjectInfo.cs` (veya `Services/Models`) oluştur. Registry load/save, CRUD, cover kopyalama, silme, rename'i burada merkezileştir. `MainWindowViewModel.ProjectRoot`'u bu servisten besle; `ResolveProjectRoot()` fallback olarak kalır ama Hub varken kullanılmaz.
2.  **Hub UI sonra:** `editor/Views/ProjectHubWindow.axaml(+.cs)` ve `editor/ViewModels/ProjectHubViewModel.cs` ekle. Grid: `ItemsControl` / `ListBox` ile kartlar; her kart `Border` + `Image` (cover) + `TextBlock` (isim) + `TextBlock` (yol, küçük) + `Button` (⚙️) → `Flyout`/`ContextMenu`. Üstte "＋ Yeni Proje" (büyük, vurgulu) ve "📂 Mevcut Klasörü İçe Aktar" butonları.
3.  **Dialoglar:** `CreateProjectDialog.axaml` (isim TextBox + klasör seçici + Oluştur/İptal), `RenameProjectDialog.axaml`, cover için doğrudan `StorageProvider.OpenFilePickerAsync`, silme için iki `ConfirmDeleteDialog.axaml` (veya `MessageBox.Avalonia` tarzı basit Window).
4.  **Entegrasyon:** `App.axaml.cs:OnFrameworkInitializationCompleted` Hub'ı `MainWindow` olarak başlat; `ProjectHubViewModel.OnProjectOpened += (path) => { ProjectRegistryService.TouchLastOpened(path); MainWindowViewModel.ProjectRoot = path; var main = new MainWindow{ DataContext = new MainWindowViewModel(projectRoot) }; main.Show(); hub.Close(); }`. Headless mod (`--test`) Hub'ı atlayıp direkt `MainWindowViewModel` ile çalışmaya devam eder.
5.  **Migrasyon:** Hub ViewModel ctor'da `registry.Load(); if(empty && Directory.Exists(legacyRoot/Assets)) => autoAdd(legacyRoot)`.
6.  **Stil:** Mevcut dark/light temayı Hub da kullanır; Minecraft esintisi için kartlara `DropShadow`, köşe radius 8, cover yoksa placeholder gradient + 🎮 ikonu.

## Work Plan

### Faz 0 — Keşif ve Sözleşme (0.5 gün)
- [ ] Mevcut `ResolveProjectRoot` tüm call-site'ları tara (`grep -rn ProjectRoot` — zaten yapıldı).
- [ ] `project.rowlproj` tüm okuma/yazma yerlerini listele.
- [ ] Hub pencere boyutu kararı: 1100x700, `WindowStartupLocation.CenterScreen`, `CanResize=false` veya `SizeToContent`.

### Faz 1 — Model & Registry Servisi (1 gün)
- **Yeni dosyalar:**
  - `editor/Models/ProjectInfo.cs` — `public record ProjectInfo { Guid Id; string Name; string Path; string? CoverImage; DateTime CreatedAt; DateTime LastOpenedAt; }`
  - `editor/Services/ProjectRegistryService.cs` — `Load()`, `Save()`, `Add(ProjectInfo)`, `Update(ProjectInfo)`, `Remove(Guid)`, `Rename(Guid,string)`, `SetCover(Guid,string sourceImagePath)`, `GetAll()`, `FindByPath()`, `Touch(Guid)`. Dosya konumu: `GetRegistryPath()` → `SpecialFolder.ApplicationData` + `RowlEngine/projects.json`. `JsonSerializer` ile atomic write (`temp + move`).
  - `editor/Services/ProjectFactory.cs` (veya Registry içinde) — `CreateNewProject(string name, string parentFolder)` → klasör oluştur, `Assets/` iskeleti, `project.rowlproj` yaz, `full_story_graph.json` template, `cover` yok.
- **Değişen dosyalar:**
  - `Assets/project.rowlproj` şeması genişletme (opsiyonel alanlar).
  - `MainWindowViewModel.cs` — `ProjectRoot` set edildikten sonra `LoadFullStoryGraphFile()` çağrısı için `public MainWindowViewModel(string? overrideRoot = null)` ctor overload ekle; mevcut parametresiz ctor legacy fallback'i korur.
- **Bağımlılık:** Yok.

### Faz 2 — Hub UI (1.5–2 gün)
- **Yeni dosyalar:**
  - `editor/Views/ProjectHubWindow.axaml` + `.cs` — Window, `DataContext=ProjectHubViewModel`.
  - `editor/ViewModels/ProjectHubViewModel.cs` — `ObservableCollection<ProjectCardViewModel> Projects`, `RelayCommand CreateProject`, `OpenProject(ProjectCard)`, `ImportExistingProject`, `ShowGearMenu`.
  - `editor/ViewModels/ProjectCardViewModel.cs` — `ProjectInfo` wrapper + `IAsyncRelayCommand Rename`, `SetCover`, `Delete` (çift onay).
  - `editor/Views/Dialogs/CreateProjectDialog.axaml` — `TextBox Name`, `TextBox ParentFolder` (readonly) + `Browse` butonu + `Create/Cancel`.
  - `editor/Views/Dialogs/RenameProjectDialog.axaml`
  - `editor/Views/Dialogs/ConfirmDialog.axaml` (generic, başlık/mesaj parametreli) — iki kez kullanılacak.
- **UI detayları:**
  - Kart: `<Border CornerRadius="8" BoxShadow="..."><Grid RowDefs="140,Auto,Auto"><Image Source={Binding CoverBitmap}/><TextBlock Text={Binding Name}/><StackPanel><TextBlock Text={Binding Path} Opacity=0.6 FontSize=11/><TextBlock Text={Binding LastOpenedAt} /></StackPanel><Button Content="⚙️" Flyout><MenuFlyout><MenuItem Header="✏️ İsim Değiştir"/><MenuItem Header="🖼️ Kapak Resmi Ekle"/><MenuItem Header="🗑️ Sil" Foreground="#DC2626"/></MenuFlyout></Button></Grid></Border>`
  - Boş state: "Henüz proje yok — İlk projeni oluştur!" + büyük CTA.
  - Hub üst bar: `🎮 ROWL ENGINE` + `+ Yeni Proje` + `📂 İçe Aktar` + tema toggle (opsiyonel).
- **Bağımlılık:** Faz 1.

### Faz 3 — Entegrasyon & Akış (0.5–1 gün)
- **Değişen dosyalar:**
  - `editor/App.axaml.cs` — `desktop.MainWindow = new ProjectHubWindow { DataContext = new ProjectHubViewModel(...) }` ; headless `--test` ve `Design.DataContext` korunmalı.
  - `editor/Views/MainWindow.axaml(.cs)` — gerekirse `ProjectHub`'a dön butonu (Dosya menüsüne "🏠 Hub'a Dön" ekle → `MainWindow` kapat, Hub yeniden aç).
  - `editor/Program.cs` — `RunHeadlessTests()` içinde `MainWindowViewModel` ctor overload kullan, Hub bypass.
- **Dosya picker entegrasyonu:** `StorageProvider.OpenFolderPickerAsync` ve `OpenFilePickerAsync` (cover) kullanımı.
- **Bağımlılık:** Faz 2.

### Faz 4 — Cilalama, Validasyon, Test (0.5 gün)
- [ ] Cover image validasyonu: boyut, format (png/jpg/jpeg/webp), max 5MB uyarısı.
- [ ] İsim validasyonu: boş değil, 3–32 karakter, illegal char yok, aynı parent'ta aynı isimde klasör varsa "Bu isimde proje zaten var" hatası.
- [ ] Silme: iki dialog'un metnini tam istek gibi yap, ikinci dialog'da buton rengi kırmızı, "Evet, Kalıcı Olarak Sil".
- [ ] Registry atomic write + corrupt JSON recovery (bozuksa yedekle, boş liste ile başla).
- [ ] Mevcut `ImportAssetAsync` / `AssetBrowserViewModel.RefreshAssets()` yeni `ProjectRoot` ile uyumlu olduğunu doğrula.

## Validation Plan

| Faz | Komut / Manuel Kontrol | Beklenen Kanıt |
|-----|------------------------|----------------|
| 1 | `dotnet build editor/RowlEngine.Editor.csproj` | Derleme hatasız |
| 1 | `dotnet run --project editor/RowlEngine.Editor.csproj -- --headless-test` | 8 test hala yeşil (Faz1 sonrası) |
| 2 | `dotnet run --project editor` → Hub görünümü | Kart grid, "Yeni Proje" butonu, boş state |
| 2 | Hub → Yeni Proje → isim "TestProjesi" + klasör `/tmp/RowlTestHub` → Oluştur | `/tmp/RowlTestHub/TestProjesi/Assets/`, `project.rowlproj` name=TestProjesi, `coverImage=null` |
| 2 | Hub listede TestProjesi kartı | Kartta isim, yol, placeholder kapak |
| 3 | Kart ⚙️ → Kapak Ekle → `/tmp/sample.jpg` seç | `TestProjesi/cover.jpg` oluştu, `project.rowlproj.coverImage` güncellendi, kartta önizleme |
| 3 | Kart ⚙️ → İsim Değiştir → "YeniAd" | Klasör `TestProjesi` → `YeniAd`, registry path güncellendi, liste yenilendi |
| 3 | Kart ⚙️ → Sil → Dialog1 "Emin misin?" Evet → Dialog2 "Gerçekten emin misin?" Evet | Klasör silindi, registry'den düştü, liste güncellendi; Hayır/İptal yollarında silinmedi |
| 3 | Hub → kart çift tık / Aç | `MainWindow` açıldı, `ProjectRoot` seçilen yol, `full_story_graph.json` oradan yüklendi |
| 4 | Mevcut repo kökünü (`/home/chaple/Belgeler/Rowl Engine`) Hub'da İçe Aktar ile seç | Otomatik kart oluştu, mevcut 3 node'lu graph bozulmadan açıldı |
| 4 | Edge: isim boş, illegal char, var olan isim | Hata mesajı, oluşturma engellendi |
| 4 | Edge: registry JSON'u boz (`echo "xxx" > ~/.config/RowlEngine/projects.json`) sonra başlat | Uygulama çökmez, bozuk dosya `.bak` yapılır, boş Hub açılır |

## Risks / Rollback

- **Risk: Registry bozulması → tüm proje listesi kaybolur.** Mitigasyon: atomic write + bozuk JSON'da dosyayı `projects.json.bak.<timestamp>` olarak yedekle ve boş liste ile devam et; log'a yaz.
- **Risk: Klasör rename sırasında crash → yarım taşınma.** Mitigasyon: `Directory.Move` atomik (aynı volume'da); önce registry'yi değil, önce move'u yap, başarılı olunca registry güncelle; hata ise rollback mesajı.
- **Risk: Silme geri alınamaz, kullanıcı yanlışlıkla siler.** Mitigasyon: çift onay + ikinci dialog kırmızı uyarı + proje adını diyalogda göster.
- **Risk: Mevcut `Assets/` repo içinde git tarafından takip ediliyor; Hub yeni projeleri repo dışına taşıyınca git status temiz kalmalı.** `.gitignore`'a `projects.json` zaten dışarıda olduğu için sorun yok; repo kökündeki `Assets/` legacy proje olarak kalır.
- **Rollback:** Faz 1–3 commit'leri ayrı tutulursa, `git revert` ile Hub kaldırılıp `App.axaml.cs` eski haline döndürülebilir; `ProjectRoot` static fallback'i koruduğu için eski tek-proje akışı anında geri gelir. Registry dosyası silinse bile uygulama legacy `ResolveProjectRoot()` ile çalışmaya devam eder.

## Open Questions

- [ ] **Soru:** Yeni proje template'i boş mu olsun, mevcut örnek sahne (3 node) ile mi başlasın? **Varsayım:** Boş + 1 default Dialogue Node (şu anki fallback gibi). Onay gerektirir.
- [ ] **Soru:** Hub → MainWindow → Hub'a Dön akışında MainWindow kapatılınca Hub yeniden mi açılsın yoksa uygulama kapansın mı? **Öneri:** Dosya menüsüne "Hub'a Dön" ekle, kapatınca Hub show, Hub kapatılınca uygulama exit.
- [ ] **Soru:** Cover image proje klasöründe mi (`<root>/cover.png`) yoksa `Assets/images/cover.png` içinde mi dursun? **Öneri:** `<root>/cover.png` (proje meta verisi, asset değil).
- [ ] **Soru:** Proje ismi değiştirince klasör adı da değişsin mi, sadece manifest mi? **Öneri:** İkisi de (Minecraft gibi). Kullanıcı klasörü manuel rename ederse Hub "Kayıp proje" olarak gri kart gösterip "Yolu düzelt" sunsun mu? V1'de sadece uyarı logu yeterli.

---

**Sonraki adım:** Bu plan onaylandığına göre, implementasyon için `Faz 1`'den başlayarak kod değişimine geçilebilir. Devam etmek için "implemente et / başla" demen yeterli — plandaki commit/PR split korunarak ilerlenecek.
