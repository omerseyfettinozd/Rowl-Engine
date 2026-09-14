using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace RowlEngine.Editor.ViewModels
{
    public partial class AssetNodeViewModel : ViewModelBase
    {
        public string Name { get; private set; }
        public string RelativePath { get; private set; }
        public string FullPath { get; private set; }
        public bool IsDirectory { get; }
        public string Icon { get; }
        public string IconColor { get; }
        public ObservableCollection<AssetNodeViewModel> Children { get; } = new();

        [ObservableProperty]
        private bool _isEditing;

        [ObservableProperty]
        private string _editingName = string.Empty;

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

            if (isDirectory)
            {
                Icon = "📁";
                IconColor = "#FBBF24";
            }
            else
            {
                string ext = System.IO.Path.GetExtension(name).ToLowerInvariant();
                if (Services.MediaFormatCatalog.IsSupportedImageExtension(ext))
                {
                    Icon = "🖼️";
                    IconColor = "#38BDF8";
                }
                else if (Services.MediaFormatCatalog.IsSupportedAudioExtension(ext))
                {
                    Icon = "🎵";
                    IconColor = "#A855F7";
                }
                else if (ext == ".json" || ext == ".txt" || ext == ".lua")
                {
                    Icon = "📜";
                    IconColor = "#F59E0B";
                }
                else if (ext == ".rowlpkg")
                {
                    Icon = "📦";
                    IconColor = "#10B981";
                }
                else
                {
                    Icon = "📄";
                    IconColor = "#94A3B8";
                }
            }
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

            if (string.IsNullOrWhiteSpace(EditingName) || EditingName.Trim() == Name)
            {
                return;
            }

            try
            {
                string? parentDir = System.IO.Path.GetDirectoryName(FullPath);
                if (string.IsNullOrEmpty(parentDir)) return;

                string newFullPath = System.IO.Path.Combine(parentDir, EditingName.Trim());

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

    public partial class AssetItemViewModel : ViewModelBase
    {
        public string Name { get; }
        public string Icon { get; }
        public string IconColor { get; }

        public AssetItemViewModel(string path)
        {
            Name = path;
            string ext = System.IO.Path.GetExtension(path).ToLowerInvariant();
            if (Services.MediaFormatCatalog.IsSupportedImageExtension(ext))
            {
                Icon = "🖼️";
                IconColor = "#38BDF8";
            }
            else if (Services.MediaFormatCatalog.IsSupportedAudioExtension(ext))
            {
                Icon = "🎵";
                IconColor = "#A855F7";
            }
            else if (ext == ".json" || ext == ".txt" || ext == ".lua")
            {
                Icon = "📜";
                IconColor = "#F59E0B";
            }
            else
            {
                Icon = "📦";
                IconColor = "#10B981";
            }
        }

        public override string ToString() => Name;

        public override bool Equals(object? obj)
        {
            if (obj is AssetItemViewModel other) return Name == other.Name;
            if (obj is string str) return Name == str;
            return false;
        }

        public override int GetHashCode() => Name.GetHashCode();
    }

    public partial class AssetBrowserViewModel : ViewModelBase, IDisposable
    {
        public MainWindowViewModel MainViewModel { get; }

        [ObservableProperty]
        private AssetNodeViewModel? _selectedNode;

        public ObservableCollection<AssetNodeViewModel> AssetTree { get; } = new();
        public ObservableCollection<AssetItemViewModel> Assets { get; } = new();
        public ObservableCollection<string> AssetNames { get; } = new();

        // ── MS-5: asset watcher + missing-asset tracking ──────────────────
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

        /// <summary>MS-5: true while at least one project directory is watched.</summary>
        public bool IsWatching => _watchers.Count > 0;

        public AssetBrowserViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
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

        private void RefreshAssetsCore()
        {
            AssetTree.Clear();
            Assets.Clear();
            AssetNames.Clear();

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
                    var rootNode = new AssetNodeViewModel(displayName, displayName, mountPath, true, RefreshAssets);

                    PopulateDirectoryNode(rootDir, mountPath, rootNode.Children, currentFiles);
                    AssetTree.Add(rootNode);
                }
            }

            // MS-5: previously seen files that vanished from disk become ghosts.
            var missing = _knownAssetFiles.Where(p => !currentFiles.Contains(p)).OrderBy(p => p).ToList();
            _knownAssetFiles.UnionWith(currentFiles);

            var missingSet = new HashSet<string>(missing, StringComparer.OrdinalIgnoreCase);
            if (missing.Count > 0)
            {
                var group = new AssetNodeViewModel("⚠️ Kayıp Dosyalar", "__missing__", string.Empty, true);
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
                string msg = $"⚠️ {newlyMissing.Count} varlık diskte bulunamadı: {string.Join(", ", newlyMissing.Select(System.IO.Path.GetFileName))}";
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
                MainViewModel.AppendLog($"⚠️ {msg}");
                MainViewModel.NotifyError(msg, "Varlık Tarayıcı");
                ToastService.Instance.Show(msg, ToastType.Error, 5000);
            }
            catch (Exception reportEx)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to report asset error: {reportEx.Message}");
            }
        }

        // ── MS-5: FileSystemWatcher (debounced) ───────────────────────────
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
                    file.Name.Equals(".gitkeep", StringComparison.OrdinalIgnoreCase)) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, file.FullName);
                var fileNode = new AssetNodeViewModel(file.Name, relPath, file.FullName, false, RefreshAssets);
                currentFiles?.Add(file.FullName);

                targetCollection.Add(fileNode);
                Assets.Add(new AssetItemViewModel(relPath));
                AssetNames.Add(relPath);
            }
        }

        [RelayCommand]
        public void CreateFolder()
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

                string newFolderName = "YeniKlasor";
                string fullNewFolderPath = System.IO.Path.Combine(targetDir, newFolderName);
                int counter = 1;
                while (System.IO.Directory.Exists(fullNewFolderPath))
                {
                    newFolderName = $"YeniKlasor_{counter++}";
                    fullNewFolderPath = System.IO.Path.Combine(targetDir, newFolderName);
                }

                System.IO.Directory.CreateDirectory(fullNewFolderPath);
                System.IO.File.WriteAllText(System.IO.Path.Combine(fullNewFolderPath, ".gitkeep"), "");

                RefreshAssets();
            }
            catch (Exception ex)
            {
                ReportAssetError("klasör oluşturma", ex);
            }
        }

        [RelayCommand]
        public void DeleteAsset()
        {
            if (SelectedNode == null) return;
            // MS-5: ghosts are already gone from disk; just drop the selection.
            if (SelectedNode.IsMissing || string.IsNullOrEmpty(SelectedNode.FullPath))
            {
                SelectedNode = null;
                RefreshAssets();
                return;
            }
            try
            {
                if (SelectedNode.IsDirectory)
                {
                    if (System.IO.Directory.Exists(SelectedNode.FullPath))
                    {
                        System.IO.Directory.Delete(SelectedNode.FullPath, true);
                    }
                }
                else
                {
                    if (System.IO.File.Exists(SelectedNode.FullPath))
                    {
                        System.IO.File.Delete(SelectedNode.FullPath);
                    }
                }
                SelectedNode = null;
                RefreshAssets();
            }
            catch (Exception ex)
            {
                ReportAssetError("varlık silme", ex);
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
