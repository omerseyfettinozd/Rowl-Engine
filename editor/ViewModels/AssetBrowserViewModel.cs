using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Views.Dialogs;

namespace RowlEngine.Editor.ViewModels
{
    public partial class AssetNodeViewModel : ViewModelBase
    {
        public string Name { get; private set; }
        public string RelativePath { get; private set; }
        public string FullPath { get; private set; }
        public bool IsDirectory { get; }
        /// <summary>Faz 4: tür etiketi (Klasör/Görsel/Ses/Betik/Paket/Dosya).</summary>
        public string TypeLabel { get; }
        /// <summary>Faz 4: "__missing__" grup başlığı (seçilemez bilgi satırı).</summary>
        public bool IsMissingGroup => !IsMissing && RelativePath == "__missing__";
        /// <summary>
        /// Faz 4: görsel küçük resmi (AssetBitmapCache; dizin/kayıp/görsel-dışı
        /// türde null → şablon tip etiketine düşer). Uzantı kapısı decode
        /// denemesinden ÖNCE çalışır (bozuk/görsel-dışı dosya Bitmap üretmemeli).
        /// </summary>
        public Avalonia.Media.Imaging.Bitmap? Thumbnail =>
            IsDirectory || IsMissing ||
            !Services.MediaFormatCatalog.IsSupportedImageExtension(System.IO.Path.GetExtension(Name))
                ? null
                : Services.AssetBitmapCache.GetOrLoad(FullPath);
        public bool HasThumbnail => Thumbnail != null;
        public ObservableCollection<AssetNodeViewModel> Children { get; } = new();

        [ObservableProperty]
        private bool _isEditing;

        [ObservableProperty]
        private string _editingName = string.Empty;

        /// <summary>
        /// Faz 4: ağaçta genişletme durumu ViewModel'de tutulur (yalnızca
        /// kök açık + filtre aktifken tümü; IsExpanded=True stili kalktı).
        /// </summary>
        [ObservableProperty]
        private bool _isExpanded;

        /// <summary>
        /// MS-5: true when the file was previously known but is now absent on
        /// disk (deleted/renamed externally). Shown with a "kayıp dosya" badge.
        /// </summary>
        [ObservableProperty]
        private bool _isMissing;

        private readonly Action? _onRenamed;

        public AssetNodeViewModel(string name, string relativePath, string fullPath, bool isDirectory, Action? onRenamed = null)
        {
            Name = name;
            RelativePath = relativePath;
            FullPath = fullPath;
            IsDirectory = isDirectory;
            _onRenamed = onRenamed;
            TypeLabel = GetTypeLabel(isDirectory, name);
        }

        /// <summary>Faz 4: uzantıdan tür etiketi (filtre kategorileriyle aynı küme).</summary>
        internal static string GetTypeLabel(bool isDirectory, string name)
        {
            if (isDirectory) return "Klasör";
            string ext = System.IO.Path.GetExtension(name).ToLowerInvariant();
            if (Services.MediaFormatCatalog.IsSupportedImageExtension(ext)) return "Görsel";
            if (Services.MediaFormatCatalog.IsSupportedAudioExtension(ext)) return "Ses";
            if (ext == ".json" || ext == ".txt" || ext == ".lua") return "Betik";
            if (ext == ".rowlpkg") return "Paket";
            return "Dosya";
        }

        public void StartRename()
        {
            EditingName = Name;
            IsEditing = true;
        }

        [RelayCommand]
        public void CommitRename()
        {
            if (!IsEditing) return;
            IsEditing = false;

            // Faz 4: sessiz-return dalları kullanıcıya bildirilir.
            if (string.IsNullOrWhiteSpace(EditingName))
            {
                try { ToastService.Instance.Show("Varlık adı boş olamaz.", ToastType.Warning, 4000); } catch { }
                return;
            }
            if (EditingName.Trim() == Name)
            {
                return;
            }

            try
            {
                string? parentDir = System.IO.Path.GetDirectoryName(FullPath);
                if (string.IsNullOrEmpty(parentDir)) return;

                string newName = EditingName.Trim();
                if (newName.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) >= 0)
                {
                    try { ToastService.Instance.Show($"Geçersiz ad: '{newName}'", ToastType.Warning, 4000); } catch { }
                    return;
                }
                string newFullPath = System.IO.Path.Combine(parentDir, newName);

                // Faz 4: hedef mevcutsa sessizce atlama; bildir.
                if (System.IO.Directory.Exists(newFullPath) || System.IO.File.Exists(newFullPath))
                {
                    try { ToastService.Instance.Show($"'{newName}' zaten mevcut.", ToastType.Warning, 4000); } catch { }
                    return;
                }

                if (IsDirectory)
                {
                    if (System.IO.Directory.Exists(FullPath) && !System.IO.Directory.Exists(newFullPath))
                    {
                        System.IO.Directory.Move(FullPath, newFullPath);
                    }
                }
                else
                {
                    if (System.IO.File.Exists(FullPath) && !System.IO.File.Exists(newFullPath))
                    {
                        System.IO.File.Move(FullPath, newFullPath);
                    }
                }

                _onRenamed?.Invoke();
            }
            catch (Exception ex)
            {
                // MS-5: surface instead of swallowing silently.
                string renameMsg = $"Varlık yeniden adlandırılamadı: {ex.Message}";
                System.Diagnostics.Debug.WriteLine(renameMsg);
                try { ToastService.Instance.Show(renameMsg, ToastType.Error, 5000); } catch { }
            }
        }

        /// <summary>MS-5: ghost entry for a file missing on disk; never renamed.</summary>
        internal static AssetNodeViewModel CreateMissingGhost(string name, string relativePath, string fullPath)
        {
            return new AssetNodeViewModel(name, relativePath, fullPath, false)
            {
                IsMissing = true
            };
        }

        [RelayCommand]
        public void CancelRename()
        {
            IsEditing = false;
        }

        public override string ToString() => RelativePath;

        public override bool Equals(object? obj)
        {
            if (obj is AssetNodeViewModel other) return RelativePath == other.RelativePath;
            if (obj is string str) return RelativePath == str;
            return false;
        }

        public override int GetHashCode() => RelativePath.GetHashCode();
    }

    public partial class AssetBrowserViewModel : ViewModelBase, IDisposable
    {
        public MainWindowViewModel MainViewModel { get; }

        [ObservableProperty]
        private AssetNodeViewModel? _selectedNode;

        /// <summary>
        /// Faz 4: "__missing__" grup başlığı seçilemez bilgi satırıdır;
        /// seçim denenirse geri bırakılır (hayalet çocuklar seçilebilir).
        /// </summary>
        partial void OnSelectedNodeChanged(AssetNodeViewModel? value)
        {
            if (value is not null && value.IsMissingGroup)
                SelectedNode = null;
        }

        /// <summary>Faz 4: arama kutusu (ad/uzantı içerir). Her tuş RefreshAssets'i tetikler.</summary>
        [ObservableProperty]
        private string _searchText = string.Empty;

        /// <summary>Faz 4: tür filtresi seçenekleri (Türkçe, sabit).</summary>
        public IReadOnlyList<string> AssetTypeFilters { get; } =
            new[] { "Tümü", "Görsel", "Ses", "Betik", "Paket" };

        /// <summary>Faz 4: seçili tür filtresi. Değişim RefreshAssets'i tetikler.</summary>
        [ObservableProperty]
        private string _selectedTypeFilter = "Tümü";

        /// <summary>Faz 4: durum satırı ("12 öğe · 2 sistem dosyası gizli" / "5 sonuç").</summary>
        [ObservableProperty]
        private string _statusText = string.Empty;

        /// <summary>
        /// Faz 5: gerçek dosya/klasör yok (kayıp grubu sayılmaz; filtre
        /// kapalıyken anlamlı). Boş-durum panelini açar.
        /// </summary>
        [ObservableProperty]
        private bool _isTreeEmpty;

        /// <summary>Faz 5: filtre aktif ama görünür dosya yok. Filtre-temizleme panelini açar.</summary>
        [ObservableProperty]
        private bool _isFilterNoMatch;

        partial void OnSearchTextChanged(string value)
        {
            ResetGridRenderLimit();
            RefreshAssets();
        }

        partial void OnSelectedTypeFilterChanged(string value)
        {
            ResetGridRenderLimit();
            RefreshAssets();
        }

        /// <summary>Faz 4: arama veya tür filtresi aktif mi?</summary>
        private bool FilterActive =>
            !string.IsNullOrWhiteSpace(SearchText) || SelectedTypeFilter != "Tümü";

        private int _visibleFileCount;
        private int _hiddenFileCount;

        /// <summary>Faz 4: dosya adı + tür filtresi eşleşmesi.</summary>
        private bool MatchesFilter(string fileName)
        {
            if (!string.IsNullOrWhiteSpace(SearchText) &&
                !fileName.Contains(SearchText, StringComparison.OrdinalIgnoreCase))
                return false;
            string ext = System.IO.Path.GetExtension(fileName).ToLowerInvariant();
            return SelectedTypeFilter switch
            {
                "Görsel" => Services.MediaFormatCatalog.IsSupportedImageExtension(ext),
                "Ses" => Services.MediaFormatCatalog.IsSupportedAudioExtension(ext),
                "Betik" => ext == ".json" || ext == ".txt" || ext == ".lua",
                "Paket" => ext == ".rowlpkg",
                _ => true,
            };
        }

        public ObservableCollection<AssetNodeViewModel> AssetTree { get; } = new();

        // MS-5: asset watcher + missing-asset tracking
        private readonly List<FileSystemWatcher> _watchers = new();
        private System.Threading.Timer? _watchDebounce;
        private readonly object _watchLock = new();
        private bool _disposed;

        /// <summary>
        /// MS-5: every asset file path ever observed to exist. Entries absent
        /// from the latest scan are surfaced as missing-asset ghosts.
        /// </summary>
        private readonly HashSet<string> _knownAssetFiles = new(StringComparer.OrdinalIgnoreCase);

        private HashSet<string> _lastMissingFiles = new(StringComparer.OrdinalIgnoreCase);

        /// <summary>MS-5: full paths currently shown as missing-asset ghosts.</summary>
        public IReadOnlyCollection<string> MissingAssetPaths => _lastMissingFiles;

        /// <summary>MS-5: number of missing-asset ghosts in the tree.</summary>
        public int MissingAssetCount => _lastMissingFiles.Count;

        /// <summary>MS-5: debounce between a disk event and the tree refresh.</summary>
        internal static TimeSpan WatchDebounceInterval { get; set; } = TimeSpan.FromMilliseconds(400);

        /// <summary>
        /// Faz 4: silme-onay enjeksiyonu (başlık, mesaj → onay?). Headless
        /// testler buradan mock'lar; null ise gerçek ConfirmDialog açılır.
        /// </summary>
        public Func<string, string, Task<bool>>? ConfirmDeleteAsync { get; set; }

        /// <summary>
        /// Faz 4: klasör-adı enjeksiyonu (önerilen ad → girilen ad/null).
        /// Headless testler buradan mock'lar; null ise RenameProjectDialog
        /// varyantı açılır (penceresiz ortamda önerilen ad kullanılır).
        /// </summary>
        public Func<string, Task<string?>>? PromptForFolderNameAsync { get; set; }

        /// <summary>MS-5: true while at least one project directory is watched.</summary>
        public bool IsWatching => _watchers.Count > 0;

        public AssetBrowserViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            // Simge boyutu kalıcılığı: MainVM ctor'u LoadEditorSettings'i
            // bizden önce çalıştırdığı için Settings'teki değer diskten
            // gelen değerdir; ızgara açılışta onu devralır.
            GridItemSize = MainViewModel.Settings.AssetGridItemSize;
            RefreshAssets();
            StartWatching();
        }

        public void RefreshAssets()
        {
            try
            {
                RefreshAssetsCore();
            }
            catch (Exception ex)
            {
                ReportAssetError("yenileme", ex);
            }
        }

        /// <summary>
        /// Tur-8: true when <paramref name="path"/> lives under one of the
        /// current project mounts (prefix comparison, case-insensitive).
        /// Existence is NOT required — a deleted file under the current
        /// root is still "ours" (a genuine ghost), while a path from a
        /// previous project root is stale knowledge to evict.
        /// </summary>
        private static bool IsUnderAnyMount(string path, List<(string displayName, string path)> mountPoints)
        {
            string full;
            try { full = Path.GetFullPath(path); }
            catch { return false; }
            foreach (var (_, mountPath) in mountPoints)
            {
                string mountFull;
                try { mountFull = Path.GetFullPath(mountPath); }
                catch { continue; }
                if (!mountFull.EndsWith(Path.DirectorySeparatorChar))
                    mountFull += Path.DirectorySeparatorChar;
                if (full.StartsWith(mountFull, StringComparison.OrdinalIgnoreCase))
                    return true;
            }
            return false;
        }

        private void RefreshAssetsCore()
        {
            AssetTree.Clear();
            _visibleFileCount = 0;
            _hiddenFileCount = 0;
            bool filterActive = FilterActive;

            // Define VFS mount point: only Assets/ is the canonical asset root.
            var mountPoints = new List<(string displayName, string path)>
            {
                ("Assets", MainWindowViewModel.AssetsPath),
                ("Mods", Path.Combine(MainWindowViewModel.ProjectRoot, "mods"))
            };
            var currentFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (var mountPoint in mountPoints)
            {
                string displayName = mountPoint.displayName;
                string mountPath = mountPoint.path;

                if (System.IO.Directory.Exists(mountPath))
                {
                    var rootDir = new System.IO.DirectoryInfo(mountPath);
                    var rootNode = new AssetNodeViewModel(displayName, displayName, mountPath, true, RefreshAssets)
                    {
                        // Faz 4: yalnızca kök açık başlar (IsExpanded=True stili kalktı).
                        IsExpanded = true
                    };

                    PopulateDirectoryNode(rootDir, mountPath, rootNode.Children, currentFiles);
                    AssetTree.Add(rootNode);
                }
            }

            // MS-5: previously seen files that vanished from disk become ghosts.
            // Tur-8: knowledge is scoped to the current project mounts. The
            // suite reuses one browser across project roots (E2E temp root,
            // then the MS5 temp root); entries from a previous root are
            // stale — not missing. Without this, a deleted foreign root
            // badges the current project (Windows CI: 3 E2E ghosts + the
            // real probe = count 4).
            _knownAssetFiles.RemoveWhere(known => !IsUnderAnyMount(known, mountPoints));
            var missing = _knownAssetFiles.Where(p => !currentFiles.Contains(p)).OrderBy(p => p).ToList();
            _knownAssetFiles.UnionWith(currentFiles);

            var missingSet = new HashSet<string>(missing, StringComparer.OrdinalIgnoreCase);
            if (missing.Count > 0)
            {
                var group = new AssetNodeViewModel("Kayıp Dosyalar", "__missing__", string.Empty, true)
                {
                    IsExpanded = filterActive
                };
                foreach (string fullPath in missing)
                {
                    string name = System.IO.Path.GetFileName(fullPath);
                    string rel = TryRelativize(fullPath) ?? name;
                    group.Children.Add(AssetNodeViewModel.CreateMissingGhost(name, rel, fullPath));
                }
                AssetTree.Add(group);
            }

            var newlyMissing = missingSet.Where(p => !_lastMissingFiles.Contains(p)).ToList();
            _lastMissingFiles = missingSet;
            OnPropertyChanged(nameof(MissingAssetPaths));
            OnPropertyChanged(nameof(MissingAssetCount));

            if (newlyMissing.Count > 0)
            {
                string msg = $"{newlyMissing.Count} varlık diskte bulunamadı: {string.Join(", ", newlyMissing.Select(System.IO.Path.GetFileName))}";
                try
                {
                    MainViewModel.AppendLog(msg);
                    MainViewModel.NotificationService.ShowWarning(msg, "Kayıp Varlık");
                    ToastService.Instance.Show(msg, ToastType.Warning, 5000);
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine($"Failed to report missing assets: {ex.Message}");
                }
            }

            // Faz 4: durum satırı (budama sonrası görünür sayı).
            // Faz 5: boş-durum rozetleri — gerçek düğüm sayımı budamadan
            // ÖNCE yapılır (filtre görünümü boşaltabilir ama dosyalar durur).
            IsTreeEmpty = !filterActive && !HasRealNodes(AssetTree);
            int shownFiles = filterActive ? PruneFilteredNodes(AssetTree) : _visibleFileCount;
            IsFilterNoMatch = filterActive && shownFiles == 0;
            StatusText = filterActive
                ? $"{shownFiles} sonuç"
                : $"{_visibleFileCount} öğe" +
                  (_hiddenFileCount > 0 ? $" · {_hiddenFileCount} sistem dosyası gizli" : string.Empty);
            // Faz 6 Dilim 3: budama sonrası gezinme durumunu canlı ağaca bağla.
            ResolveFolderNavigation();
        }

        /// <summary>
        /// Faz 5: ağaçta kayıp-grubu/hayalet dışı gerçek düğüm var mı?
        /// (Boş klasörler de içerik sayılır — panel yalnızca tamamen
        /// boşken görünür.)
        /// </summary>
        private static bool HasRealNodes(ObservableCollection<AssetNodeViewModel> nodes)
        {
            foreach (var node in nodes)
            {
                if (!node.IsMissing && !node.IsMissingGroup)
                    return true;
                if (HasRealNodes(node.Children))
                    return true;
            }
            return false;
        }

        /// <summary>Faz 5: arama + tür filtresini temizler (boş-durum eylemi).</summary>
        [RelayCommand]
        private void ClearFilter()
        {
            SearchText = string.Empty;
            SelectedTypeFilter = "Tümü";
        }

        // ── Faz 6 Dilim 3: klasör ağacı + simge ızgarası gezinmesi ──────────
        /// <summary>
        /// Faz 6 Dilim 3: ızgarada gezilen klasör. Yalnızca gezinme
        /// durumudur — seçim otoritesi <see cref="SelectedNode"/> olarak kalır.
        /// </summary>
        [ObservableProperty]
        private AssetNodeViewModel? _selectedFolder;

        /// <summary>
        /// Faz 6 Dilim 3: seçili klasörün ızgara içeriği. Aynı düğüm
        /// referanslarının ayrı koleksiyonudur (klon yok, IsExpanded
        /// aynen korunur); kayıp hayaletler ve kayıp-grup ızgaraya girmez.
        /// </summary>
        public ObservableCollection<AssetNodeViewModel> FolderContents { get; } = new();

        /// <summary>Faz 6 Dilim 3: ızgara hücre genişliği, piksel (40–128).</summary>
        [ObservableProperty]
        private double _gridItemSize = 72;

        /// <summary>Faz 6 Dilim 3: hücredeki küçük-resim karesi kenarı.</summary>
        public double GridItemImageSize => Math.Max(24, GridItemSize - 32);

        partial void OnGridItemSizeChanged(double value)
        {
            double clamped = Math.Clamp(value, 40, 128);
            if (!clamped.Equals(value))
                GridItemSize = clamped;
            OnPropertyChanged(nameof(GridItemImageSize));
            // Kalıcılık (tek yön: ızgara → Settings → makine profili).
            // Settings'ten ızgaraya geri yazma yoktur, döngü oluşmaz;
            // Settings.PropertyChanged ana VM'de profili diske yazar.
            if (!MainViewModel.Settings.AssetGridItemSize.Equals(GridItemSize))
                MainViewModel.Settings.AssetGridItemSize = GridItemSize;
        }

        partial void OnSelectedFolderChanged(AssetNodeViewModel? value) => RebuildFolderContents();

        /// <summary>
        /// Unity kromu Dilim G1: sağ bölme görünümü. True = simge ızgarası,
        /// false = kompakt liste. Her iki görünüm de aynı
        /// <see cref="VisibleGridItems"/> penceresine ve aynı seçim
        /// otoritesine bağlıdır; anahtar yalnızca sunumu değiştirir.
        /// </summary>
        [ObservableProperty]
        private bool _isGridView = true;

        /// <summary>Dilim G1: anahtar butonunun eylem metni (ızgaradayken "Liste").</summary>
        public string AssetViewToggleText => IsGridView ? "Liste" : "Izgara";

        partial void OnIsGridViewChanged(bool value) => OnPropertyChanged(nameof(AssetViewToggleText));

        /// <summary>Dilim G1: ızgara/liste görünümü arasında geç.</summary>
        [RelayCommand]
        private void ToggleAssetView()
        {
            IsGridView = !IsGridView;
        }

        /// <summary>Faz 6: ızgara tek seferde en fazla bu kadar hücre çizer.</summary>
        public const int DefaultGridRenderLimit = 200;

        /// <summary>Faz 6: "daha fazla göster" adım büyüklüğü.</summary>
        public const int GridRenderStep = 200;

        /// <summary>
        /// Faz 6: ızgaranın o an çizdiği üst sınır. WrapPanel sanallaştırma
        /// yapmadığı için veri-penceresi uygulanır: ListBox
        /// <see cref="VisibleGridItems"/> üzerine bağlıdır, tamamı
        /// <see cref="FolderContents"/> içinde durur.
        /// </summary>
        [ObservableProperty]
        private int _gridRenderLimit = DefaultGridRenderLimit;

        /// <summary>Faz 6: ızgaranın o an çizdiği pencere (en fazla limit).</summary>
        public ObservableCollection<AssetNodeViewModel> VisibleGridItems { get; } = new();

        /// <summary>Faz 6: pencerede gösterilmeyen öğe kaldı mı?</summary>
        [ObservableProperty]
        private bool _hasMoreGridItems;

        /// <summary>Faz 6: "gösterilen/toplam" sayacı ("200/1500 gösteriliyor").</summary>
        [ObservableProperty]
        private string _gridItemsStatus = string.Empty;

        partial void OnGridRenderLimitChanged(int value) => UpdateVisibleGridItems();

        /// <summary>Faz 6: 200 öğe daha çiz (kalan azsa tümünü açar).</summary>
        [RelayCommand]
        private void ShowMoreGridItems()
        {
            GridRenderLimit = Math.Min(FolderContents.Count, GridRenderLimit + GridRenderStep);
        }

        /// <summary>Faz 6: yeni klasör/filtrede pencereyi başa sarar.</summary>
        private void ResetGridRenderLimit()
        {
            GridRenderLimit = DefaultGridRenderLimit;
        }

        private void UpdateVisibleGridItems()
        {
            int total = FolderContents.Count;
            int shown = Math.Clamp(GridRenderLimit, 0, total);
            VisibleGridItems.Clear();
            for (int i = 0; i < shown; i++)
                VisibleGridItems.Add(FolderContents[i]);
            HasMoreGridItems = shown < total;
            GridItemsStatus = total == 0
                ? string.Empty
                : shown < total
                    ? $"{shown}/{total} gösteriliyor"
                    : $"{total} öğe";
        }

        /// <summary>
        /// Faz 6 Dilim 3: klasöre gir (ızgara çift-tık / Enter). Yalnızca
        /// gerçek klasörler; hayalet ve kayıp-grup reddedilir.
        /// </summary>
        public void EnterFolder(AssetNodeViewModel? node)
        {
            if (node == null || !node.IsDirectory || node.IsMissing || node.IsMissingGroup)
                return;
            AssetNodeViewModel live = FindNodeByRelativePath(node.RelativePath) ?? node;
            ResetGridRenderLimit();
            SelectedFolder = live;
            SelectedNode = live;
        }

        /// <summary>Faz 6 Dilim 3: seçili öğe klasörse içine gir (Enter tuşu).</summary>
        [RelayCommand]
        private void EnterSelectedFolder() => EnterFolder(SelectedNode);

        /// <summary>Faz 6 Dilim 3: üst klasöre çık (kökte kalır).</summary>
        [RelayCommand]
        private void GoToParentFolder()
        {
            AssetNodeViewModel? root = AssetTree.FirstOrDefault(n => n.IsDirectory && !n.IsMissingGroup);
            AssetNodeViewModel? folder = SelectedFolder;
            if (folder == null || string.IsNullOrEmpty(folder.FullPath))
            {
                SelectedFolder = root;
                return;
            }
            string? parentFull = null;
            try
            {
                parentFull = Path.GetDirectoryName(
                    folder.FullPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
            }
            catch { }
            AssetNodeViewModel? parent = string.IsNullOrEmpty(parentFull)
                ? null
                : FindDirectoryByFullPath(parentFull);
            ResetGridRenderLimit();
            SelectedFolder = parent ?? root;
        }

        private AssetNodeViewModel? FindNodeByRelativePath(string relativePath)
        {
            var stack = new Stack<AssetNodeViewModel>(AssetTree);
            while (stack.Count > 0)
            {
                var node = stack.Pop();
                if (node.RelativePath.Equals(relativePath, StringComparison.Ordinal))
                    return node;
                foreach (var child in node.Children) stack.Push(child);
            }
            return null;
        }

        private AssetNodeViewModel? FindDirectoryByFullPath(string fullPath)
        {
            var stack = new Stack<AssetNodeViewModel>(AssetTree);
            while (stack.Count > 0)
            {
                var node = stack.Pop();
                if (node.IsDirectory && !node.IsMissingGroup &&
                    node.FullPath.Equals(fullPath, StringComparison.OrdinalIgnoreCase))
                    return node;
                foreach (var child in node.Children) stack.Push(child);
            }
            return null;
        }

        /// <summary>
        /// Faz 6 Dilim 3: tarama sonunda gezinme durumunu canlı ağaca
        /// bağlar. SelectedNode tek seçim otoritesidir — ölü referanslar
        /// RelativePath ile yeniden çözülür (TwoWay ping-pong'a karşı
        /// yalnızca içerik gerçekten değişince yazılır).
        /// </summary>
        private void ResolveFolderNavigation()
        {
            if (SelectedNode != null)
            {
                AssetNodeViewModel? live = FindNodeByRelativePath(SelectedNode.RelativePath);
                if (live != null && !ReferenceEquals(live, SelectedNode))
                {
                    // Equals yol-tabanlı olduğundan üretilen setter bu
                    // yazmayı yutardı; bayat seçim ağaçta vurguyu
                    // kaybettirir ve rename ölü düğümde açılırdı.
                    _selectedNode = live;
                    OnPropertyChanged(nameof(SelectedNode));
                }
            }
            AssetNodeViewModel? folder = SelectedFolder != null
                ? FindNodeByRelativePath(SelectedFolder.RelativePath)
                : null;
            if (folder != null && (folder.IsMissing || folder.IsMissingGroup || !folder.IsDirectory))
                folder = null;
            folder ??= AssetTree.FirstOrDefault(n => n.IsDirectory && !n.IsMissingGroup);
            bool same = folder == null
                ? SelectedFolder == null
                : SelectedFolder != null &&
                  SelectedFolder.RelativePath.Equals(folder.RelativePath, StringComparison.Ordinal);
            if (!same)
            {
                SelectedFolder = folder; // yol değişti → olay ızgarayı kurar
            }
            else
            {
                if (folder != null && !ReferenceEquals(folder, SelectedFolder))
                {
                    // Aynı klasör, yeni ağaç örneği: setter yutmasın diye
                    // alanı doğrudan benimse (yoksa ızgara ilk taramanın
                    // anlık görüntüsünde donar).
                    _selectedFolder = folder;
                    OnPropertyChanged(nameof(SelectedFolder));
                }
                RebuildFolderContents();
            }
        }

        private void RebuildFolderContents()
        {
            FolderContents.Clear();
            foreach (var child in SelectedFolder?.Children ?? Enumerable.Empty<AssetNodeViewModel>())
            {
                if (child.IsMissing || child.IsMissingGroup)
                    continue;
                FolderContents.Add(child);
            }
            // Faz 6: limit korunur (izleyici/tuş yenilemesi kaydırmayı
            // geri sarmaz), pencere yeni içeriğe göre dilimlenir.
            UpdateVisibleGridItems();
        }

        private static string? TryRelativize(string fullPath)
        {
            try
            {
                string root = MainWindowViewModel.AssetsPath;
                if (fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                    return System.IO.Path.GetRelativePath(root, fullPath);
                string mods = Path.Combine(MainWindowViewModel.ProjectRoot, "mods");
                if (fullPath.StartsWith(mods, StringComparison.OrdinalIgnoreCase))
                    return System.IO.Path.GetRelativePath(mods, fullPath);
            }
            catch { }
            return null;
        }

        /// <summary>
        /// MS-5: routes a swallowed asset error to the user (toast + inline
        /// notification + log) instead of only Debug.WriteLine.
        /// </summary>
        private void ReportAssetError(string operation, Exception ex)
        {
            string msg = $"Varlık işlemi başarısız ({operation}): {ex.Message}";
            System.Diagnostics.Debug.WriteLine(msg);
            try
            {
                MainViewModel.AppendLog($"{msg}");
                MainViewModel.NotifyError(msg, "Varlık Tarayıcı");
                ToastService.Instance.Show(msg, ToastType.Error, 5000);
            }
            catch (Exception reportEx)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to report asset error: {reportEx.Message}");
            }
        }

        // MS-5: FileSystemWatcher (debounced)
        private void StartWatching()
        {
            StopWatchingCore();
            var dirs = new[]
            {
                MainWindowViewModel.AssetsPath,
                Path.Combine(MainWindowViewModel.ProjectRoot, "mods")
            };
            foreach (string dir in dirs)
            {
                try
                {
                    if (!System.IO.Directory.Exists(dir)) continue;
                    var watcher = new FileSystemWatcher(dir)
                    {
                        IncludeSubdirectories = true,
                        EnableRaisingEvents = true,
                        NotifyFilter = NotifyFilters.FileName | NotifyFilters.DirectoryName
                            | NotifyFilters.LastWrite | NotifyFilters.Size
                    };
                    watcher.Created += OnWatchedChanged;
                    watcher.Deleted += OnWatchedChanged;
                    watcher.Renamed += OnWatchedRenamed;
                    watcher.Changed += OnWatchedChanged;
                    watcher.Error += OnWatcherError;
                    _watchers.Add(watcher);
                }
                catch (Exception ex)
                {
                    ReportAssetError($"izleyici başlatma ({dir})", ex);
                }
            }
        }

        private void OnWatchedChanged(object? sender, FileSystemEventArgs e) => ScheduleWatchedRefresh();

        private void OnWatchedRenamed(object? sender, RenamedEventArgs e) => ScheduleWatchedRefresh();

        private void OnWatcherError(object? sender, ErrorEventArgs e)
        {
            ReportAssetError("izleyici hatası", e.GetException() ?? new IOException("FileSystemWatcher error"));
            try { StartWatching(); } catch { }
        }

        private void ScheduleWatchedRefresh()
        {
            lock (_watchLock)
            {
                if (_disposed) return;
                _watchDebounce ??= new System.Threading.Timer(_ => OnWatchDebounceElapsed(), null, System.Threading.Timeout.Infinite, System.Threading.Timeout.Infinite);
                try
                {
                    _watchDebounce.Change(WatchDebounceInterval, System.Threading.Timeout.InfiniteTimeSpan);
                }
                catch (ObjectDisposedException) { }
            }
        }

        private void OnWatchDebounceElapsed()
        {
            try
            {
                var dispatcher = Avalonia.Threading.Dispatcher.UIThread;
                if (dispatcher.CheckAccess())
                    RefreshAssets();
                else
                    dispatcher.Post(RefreshAssets);
            }
            catch
            {
                try { RefreshAssets(); } catch { }
            }
        }

        private void StopWatchingCore()
        {
            foreach (var watcher in _watchers)
            {
                try
                {
                    watcher.EnableRaisingEvents = false;
                    watcher.Dispose();
                }
                catch { }
            }
            _watchers.Clear();
        }

        public void Dispose()
        {
            if (_disposed) return;
            _disposed = true;
            lock (_watchLock)
            {
                try { _watchDebounce?.Dispose(); } catch { }
                _watchDebounce = null;
            }
            StopWatchingCore();
        }

        private static readonly HashSet<string> IgnoredDirectoryNames = new(StringComparer.OrdinalIgnoreCase)
        {
            "json", "packages", "test_assets", "bin", "obj", ".git", ".vs"
        };

        private static readonly HashSet<string> IgnoredFileExtensions = new(StringComparer.OrdinalIgnoreCase)
        {
            ".json", ".rowlproj", ".rowlpkg", ".gitkeep", ".tmp", ".log"
        };

        private void PopulateDirectoryNode(System.IO.DirectoryInfo dirInfo, string rootPath, ObservableCollection<AssetNodeViewModel> targetCollection, HashSet<string>? currentFiles = null)
        {
            foreach (var subDir in dirInfo.GetDirectories().OrderBy(d => d.Name))
            {
                if (subDir.Name.StartsWith(".") || IgnoredDirectoryNames.Contains(subDir.Name)) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, subDir.FullName);
                var dirNode = new AssetNodeViewModel(subDir.Name, relPath, subDir.FullName, true, RefreshAssets);

                PopulateDirectoryNode(subDir, rootPath, dirNode.Children, currentFiles);

                targetCollection.Add(dirNode);
            }

            foreach (var file in dirInfo.GetFiles().OrderBy(f => f.Name))
            {
                if (file.Name.StartsWith(".") ||
                    IgnoredFileExtensions.Contains(file.Extension) ||
                    file.Name.Equals(".gitkeep", StringComparison.OrdinalIgnoreCase))
                {
                    _hiddenFileCount++;
                    continue;
                }

                string relPath = System.IO.Path.GetRelativePath(rootPath, file.FullName);
                var fileNode = new AssetNodeViewModel(file.Name, relPath, file.FullName, false, RefreshAssets);
                currentFiles?.Add(file.FullName);
                _visibleFileCount++;

                targetCollection.Add(fileNode);
            }
        }

        /// <summary>
        /// Faz 4 (düzeltme): filtre budaması SADECE görünümü kırpar; tarama
        /// her zaman tam yapılır. Aksi halde filtre-dışı dosyalar o taramada
        /// currentFiles'ta yer almaz ve kayıp-hayalet diye damgalanırdı.
        /// Kayıp grubu budanmaz (uyarılar arama sırasında da görünür).
        /// </summary>
        /// <returns>Budama sonrası görünür gerçek dosya sayısı.</returns>
        private int PruneFilteredNodes(ObservableCollection<AssetNodeViewModel> nodes)
        {
            int keptFiles = 0;
            for (int i = nodes.Count - 1; i >= 0; i--)
            {
                var node = nodes[i];
                if (node.IsMissingGroup) continue;
                if (node.IsDirectory)
                {
                    int kept = PruneFilteredNodes(node.Children);
                    if (kept == 0) nodes.RemoveAt(i);
                    else
                    {
                        node.IsExpanded = true;
                        keptFiles += kept;
                    }
                }
                else if (MatchesFilter(node.Name))
                {
                    keptFiles++;
                }
                else
                {
                    nodes.RemoveAt(i);
                }
            }
            return keptFiles;
        }

        /// <summary>
        /// Faz 4: klasör oluşturma isim sorar (RenameProjectDialog varyantı;
        /// vazgeçilirse hiçbir şey yapılmaz). Komut adı CreateFolderCommand
        /// olarak korunur (Async soneki atılır).
        /// </summary>
        [RelayCommand]
        public async Task CreateFolderAsync()
        {
            try
            {
                string rootPath = MainWindowViewModel.AssetsPath;
                string targetDir = rootPath;

                // MS-5: ghosts carry no usable directory; fall back to the root.
                if (SelectedNode != null && !SelectedNode.IsMissing && !string.IsNullOrEmpty(SelectedNode.FullPath))
                {
                    if (SelectedNode.IsDirectory)
                    {
                        targetDir = SelectedNode.FullPath;
                    }
                    else
                    {
                        string? parent = System.IO.Path.GetDirectoryName(SelectedNode.FullPath);
                        if (!string.IsNullOrEmpty(parent)) targetDir = parent;
                    }
                }

                string suggested = "YeniKlasor";
                string fullSuggested = System.IO.Path.Combine(targetDir, suggested);
                int counter = 1;
                while (System.IO.Directory.Exists(fullSuggested))
                {
                    suggested = $"YeniKlasor_{counter++}";
                    fullSuggested = System.IO.Path.Combine(targetDir, suggested);
                }

                string? name = PromptForFolderNameAsync != null
                    ? await PromptForFolderNameAsync(suggested)
                    : await PromptFolderNameAsync(suggested);
                if (string.IsNullOrWhiteSpace(name)) return;
                name = name.Trim();
                if (name.IndexOfAny(System.IO.Path.GetInvalidFileNameChars()) >= 0)
                {
                    ToastService.Instance.Show($"Geçersiz klasör adı: '{name}'", ToastType.Warning, 4000);
                    return;
                }

                string fullNewFolderPath = System.IO.Path.Combine(targetDir, name);
                if (System.IO.Directory.Exists(fullNewFolderPath))
                {
                    ToastService.Instance.Show($"'{name}' zaten mevcut.", ToastType.Warning, 4000);
                    return;
                }

                System.IO.Directory.CreateDirectory(fullNewFolderPath);
                System.IO.File.WriteAllText(System.IO.Path.Combine(fullNewFolderPath, ".gitkeep"), "");

                RefreshAssets();
                ToastService.Instance.Show($"'{name}' oluşturuldu.", ToastType.Info, 3000);
            }
            catch (Exception ex)
            {
                ReportAssetError("klasör oluşturma", ex);
            }
        }

        /// <summary>Faz 4: pencere varsa prompt dialogu, yoksa önerilen ad.</summary>
        private async Task<string?> PromptFolderNameAsync(string suggested)
        {
            try
            {
                var window = EditorDialogService.GetMainWindow();
                if (window == null) return suggested;
                var dlg = new RenameProjectDialog(suggested, "Yeni Klasör", "Klasör Adı", "Klasör adı boş olamaz.");
                return await dlg.ShowDialog<string?>(window);
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Faz 4: silmeden önce ConfirmDialog sorar (klasörde öğe sayısı
        /// mesaja yazılır). Komut adı DeleteAssetCommand olarak korunur.
        /// </summary>
        [RelayCommand]
        public async Task DeleteAssetAsync()
        {
            if (SelectedNode == null) return;
            // MS-5: ghosts are already gone from disk; just drop the selection.
            if (SelectedNode.IsMissing || string.IsNullOrEmpty(SelectedNode.FullPath))
            {
                SelectedNode = null;
                RefreshAssets();
                return;
            }
            var node = SelectedNode;
            int childCount = 0;
            if (node.IsDirectory)
            {
                try
                {
                    if (System.IO.Directory.Exists(node.FullPath))
                        childCount = System.IO.Directory.EnumerateFileSystemEntries(node.FullPath, "*", SearchOption.AllDirectories).Count();
                }
                catch { }
            }
            string title = node.IsDirectory ? "Klasörü Sil" : "Dosyayı Sil";
            string message = node.IsDirectory
                ? $"'{node.Name}' klasörünü ve içindeki {childCount} öğeyi silmek istediğinize emin misiniz?"
                : $"'{node.Name}' dosyasını silmek istediğinize emin misiniz?";
            bool confirmed = ConfirmDeleteAsync != null
                ? await ConfirmDeleteAsync(title, message)
                : await ShowDeleteConfirmAsync(title, message);
            if (!confirmed) return;
            try
            {
                if (node.IsDirectory)
                {
                    if (System.IO.Directory.Exists(node.FullPath))
                    {
                        System.IO.Directory.Delete(node.FullPath, true);
                    }
                }
                else
                {
                    if (System.IO.File.Exists(node.FullPath))
                    {
                        System.IO.File.Delete(node.FullPath);
                    }
                }
                SelectedNode = null;
                RefreshAssets();
                ToastService.Instance.Show($"'{node.Name}' silindi.", ToastType.Info, 3000);
            }
            catch (Exception ex)
            {
                ReportAssetError("varlık silme", ex);
            }
        }

        /// <summary>Faz 4: pencere yoksa güvenli varsayılan VAZGEÇ'tir (false).</summary>
        private async Task<bool> ShowDeleteConfirmAsync(string title, string message)
        {
            try
            {
                var window = EditorDialogService.GetMainWindow();
                if (window == null) return false;
                var dlg = new ConfirmDialog(title, message, "Sil", true);
                return await dlg.ShowDialog<bool?>(window) == true;
            }
            catch
            {
                return false;
            }
        }

        [RelayCommand]
        public void OpenInExplorer()
        {
            try
            {
                string targetPath = SelectedNode?.IsDirectory == true
                    ? SelectedNode.FullPath
                    : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? MainWindowViewModel.AssetsPath);

                if (string.IsNullOrEmpty(targetPath) || !System.IO.Directory.Exists(targetPath))
                {
                    targetPath = MainWindowViewModel.AssetsPath;
                }

                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
                {
                    FileName = targetPath,
                    UseShellExecute = true
                });
            }
            catch
            {
                try
                {
                    string targetPath = SelectedNode?.IsDirectory == true
                        ? SelectedNode.FullPath
                        : (System.IO.Path.GetDirectoryName(SelectedNode?.FullPath) ?? MainWindowViewModel.AssetsPath);
                    System.Diagnostics.Process.Start("xdg-open", targetPath);
                }
                catch { }
            }
        }

        [RelayCommand]
        public void StartRename()
        {
            // MS-5: missing ghosts have no disk target to rename.
            if (SelectedNode != null && !SelectedNode.IsMissing)
            {
                SelectedNode.StartRename();
            }
        }

        [RelayCommand]
        public void RefreshAssetsCommand()
        {
            RefreshAssets();
        }
    }
}
