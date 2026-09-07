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
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif")
                {
                    Icon = "🖼️";
                    IconColor = "#38BDF8";
                }
                else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
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
                System.Diagnostics.Debug.WriteLine($"Failed to rename asset: {ex.Message}");
            }
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
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || ext == ".gif")
            {
                Icon = "🖼️";
                IconColor = "#38BDF8";
            }
            else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac")
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

    public partial class AssetBrowserViewModel : ViewModelBase
    {
        public MainWindowViewModel MainViewModel { get; }

        [ObservableProperty]
        private AssetNodeViewModel? _selectedNode;

        public ObservableCollection<AssetNodeViewModel> AssetTree { get; } = new();
        public ObservableCollection<AssetItemViewModel> Assets { get; } = new();
        public ObservableCollection<string> AssetNames { get; } = new();

        public AssetBrowserViewModel(MainWindowViewModel main)
        {
            MainViewModel = main;
            RefreshAssets();
        }

        public void RefreshAssets()
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

            foreach (var mountPoint in mountPoints)
            {
                string displayName = mountPoint.displayName;
                string mountPath = mountPoint.path;

                if (System.IO.Directory.Exists(mountPath))
                {
                    var rootDir = new System.IO.DirectoryInfo(mountPath);
                    var rootNode = new AssetNodeViewModel(displayName, displayName, mountPath, true, RefreshAssets);

                    PopulateDirectoryNode(rootDir, mountPath, rootNode.Children);
                    AssetTree.Add(rootNode);
                }
            }
        }

        private static readonly HashSet<string> IgnoredDirectoryNames = new(StringComparer.OrdinalIgnoreCase)
        {
            "json", "packages", "test_assets", "bin", "obj", ".git", ".vs"
        };

        private static readonly HashSet<string> IgnoredFileExtensions = new(StringComparer.OrdinalIgnoreCase)
        {
            ".json", ".rowlproj", ".rowlpkg", ".gitkeep", ".tmp", ".log"
        };

        private void PopulateDirectoryNode(System.IO.DirectoryInfo dirInfo, string rootPath, ObservableCollection<AssetNodeViewModel> targetCollection)
        {
            foreach (var subDir in dirInfo.GetDirectories().OrderBy(d => d.Name))
            {
                if (subDir.Name.StartsWith(".") || IgnoredDirectoryNames.Contains(subDir.Name)) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, subDir.FullName);
                var dirNode = new AssetNodeViewModel(subDir.Name, relPath, subDir.FullName, true, RefreshAssets);

                PopulateDirectoryNode(subDir, rootPath, dirNode.Children);

                targetCollection.Add(dirNode);
            }

            foreach (var file in dirInfo.GetFiles().OrderBy(f => f.Name))
            {
                if (file.Name.StartsWith(".") ||
                    IgnoredFileExtensions.Contains(file.Extension) ||
                    file.Name.Equals(".gitkeep", StringComparison.OrdinalIgnoreCase)) continue;

                string relPath = System.IO.Path.GetRelativePath(rootPath, file.FullName);
                var fileNode = new AssetNodeViewModel(file.Name, relPath, file.FullName, false, RefreshAssets);

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

                if (SelectedNode != null)
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
                System.Diagnostics.Debug.WriteLine($"Failed to create folder: {ex.Message}");
            }
        }

        [RelayCommand]
        public void DeleteAsset()
        {
            if (SelectedNode == null) return;
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
                System.Diagnostics.Debug.WriteLine($"Failed to delete asset: {ex.Message}");
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
            if (SelectedNode != null)
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
