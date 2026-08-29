using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform.Storage;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Views.Dialogs;

namespace RowlEngine.Editor.ViewModels
{
    public partial class ProjectHubViewModel : ViewModelBase
    {
        private readonly ProjectRegistryService _registry;

        public ObservableCollection<ProjectCardViewModel> Projects { get; } = new();

        [ObservableProperty]
        private string _statusText = "Projeler yükleniyor...";

        [ObservableProperty]
        private bool _isEmpty;

        public event Action<string>? ProjectOpened;

        public Window? HubWindow { get; set; }
        public object? TopLevelHint { get; set; }

        public ProjectHubViewModel() : this(new ProjectRegistryService()) { }

        public ProjectHubViewModel(ProjectRegistryService registry)
        {
            _registry = registry;
            LoadProjects();
        }

        public void LoadProjects()
        {
            _registry.Load();
            Projects.Clear();
            var ordered = _registry.Projects.OrderByDescending(p => p.LastOpenedAt).ToList();
            foreach (var info in ordered)
            {
                Projects.Add(new ProjectCardViewModel(info, this, _registry));
            }

            IsEmpty = Projects.Count == 0;
            StatusText = Projects.Count == 0
                ? "Henüz proje yok — ilk projeni oluştur!"
                : $"{Projects.Count} proje bulundu";
        }

        public void RefreshProjects()
        {
            _registry.Load();
            Projects.Clear();
            var ordered = _registry.Projects.OrderByDescending(p => p.LastOpenedAt).ToList();
            foreach (var info in ordered)
            {
                Projects.Add(new ProjectCardViewModel(info, this, _registry));
            }

            IsEmpty = Projects.Count == 0;
            StatusText = IsEmpty ? "Henüz proje yok — ilk projeni oluştur!" : $"{Projects.Count} proje bulundu";
        }

        [RelayCommand]
        public async Task CreateProjectAsync()
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var dlg = new CreateProjectDialog();
            var result = await dlg.ShowDialog<(string name, string folder)?>(window);
            if (result == null) return;

            var (name, folder) = result.Value;
            var (ok, error, info) = ProjectFactory.CreateNewProject(name, folder);
            if (!ok || info == null)
            {
                var errDlg = new ConfirmDialog("Hata", error ?? "Proje oluşturulamadı.", "Tamam");
                await errDlg.ShowDialog(window);
                return;
            }

            _registry.Add(info);
            RefreshProjects();
            StatusText = $"✅ '{name}' oluşturuldu";
        }

        [RelayCommand]
        public async Task ImportExistingAsync()
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var folders = await window.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "Mevcut Rowl Engine Proje Klasörünü Seçin",
                AllowMultiple = false
            });

            if (folders.Count == 0) return;

            string selectedPath = folders[0].Path.LocalPath;
            string name = Path.GetFileName(selectedPath);

            var info = new ProjectInfo
            {
                Id = Guid.NewGuid().ToString("N"),
                Name = name,
                Path = selectedPath,
                CreatedAt = DateTime.UtcNow,
                LastOpenedAt = DateTime.UtcNow
            };

            _registry.Add(info);
            RefreshProjects();
            StatusText = $"📂 '{name}' içe aktarıldı";
        }

        public void OpenProject(ProjectCardViewModel card)
        {
            _registry.Touch(card.Info.Id);
            ProjectOpened?.Invoke(card.Path);
        }

        public async Task RenameProjectAsync(ProjectCardViewModel card)
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var dlg = new RenameProjectDialog(card.Name);
            var newName = await dlg.ShowDialog<string?>(window);
            if (string.IsNullOrWhiteSpace(newName) || newName == card.Name) return;

            bool ok = _registry.Rename(card.Info.Id, newName.Trim());
            if (!ok)
            {
                var errDlg = new ConfirmDialog("Yeniden Adlandırma Başarısız", "Bu isimde bir proje zaten mevcut.", "Tamam");
                await errDlg.ShowDialog(window);
                return;
            }

            RefreshProjects();
            StatusText = $"✏️ '{newName}' olarak yeniden adlandırıldı";
        }

        public async Task SetCoverAsync(ProjectCardViewModel card)
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var files = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Kapak Resmi Seçin",
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("Resim Dosyaları (*.png;*.jpg;*.jpeg;*.webp)")
                    {
                        Patterns = new[] { "*.png", "*.jpg", "*.jpeg", "*.webp" }
                    }
                }
            });

            if (files.Count == 0) return;

            string src = files[0].Path.LocalPath;
            try
            {
                string ext = Path.GetExtension(src);
                string destName = $"cover_{DateTime.Now:yyyyMMddHHmmss}{ext}";
                string destPath = Path.Combine(card.Path, destName);
                File.Copy(src, destPath, true);

                _registry.UpdateCover(card.Info.Id, destName);
                RefreshProjects();
                StatusText = "🖼️ Kapak resmi güncellendi";
            }
            catch (Exception ex)
            {
                StatusText = $"⚠️ Kapak değiştirilemedi: {ex.Message}";
            }
        }

        public async Task DeleteProjectAsync(ProjectCardViewModel card)
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var dlg = new ConfirmDialog("Projeyi Sil", $"'{card.Name}' projesini listeden kaldırmak istediğinize emin misiniz?", "Kaldır", true);
            bool? confirmed = await dlg.ShowDialog<bool?>(window);
            if (confirmed != true) return;

            _registry.Remove(card.Info.Id);
            RefreshProjects();
            StatusText = $"🗑️ '{card.Name}' listeden kaldırıldı";
        }

        private Window? GetActiveWindow()
        {
            if (Avalonia.Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            {
                return desktop.Windows.FirstOrDefault(w => w.IsActive) ?? desktop.MainWindow;
            }
            return null;
        }
    }
}