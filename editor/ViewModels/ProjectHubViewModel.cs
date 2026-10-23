using System;
using System.Collections.ObjectModel;
using System.Globalization;
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
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Views.Dialogs;

namespace RowlEngine.Editor.ViewModels
{
    public enum HubStatusKind { Neutral, Success, Error }

    public partial class ProjectHubViewModel : ViewModelBase
    {
        private readonly ProjectRegistryService _registry;

        public ObservableCollection<ProjectCardViewModel> Projects { get; } = new();

        [ObservableProperty]
        private string _statusText = string.Empty;

        [ObservableProperty]
        [NotifyPropertyChangedFor(nameof(IsStatusError), nameof(IsStatusSuccess))]
        private HubStatusKind _statusKind = HubStatusKind.Neutral;

        [ObservableProperty]
        private bool _isEmpty;

        public bool IsStatusError => StatusKind == HubStatusKind.Error;
        public bool IsStatusSuccess => StatusKind == HubStatusKind.Success;

        public event Action<string>? ProjectOpened;

        public Window? HubWindow { get; set; }
        public object? TopLevelHint { get; set; }

        public ProjectHubViewModel() : this(new ProjectRegistryService()) { }

        public ProjectHubViewModel(ProjectRegistryService registry)
        {
            _registry = registry;
            ReloadProjectsCore();
        }

        internal void SetStatus(HubStatusKind kind, string text)
        {
            StatusText = text;
            StatusKind = kind;
        }

        // Boş/olmayan klasör -> Missing, grafiksiz klasör -> Invalid.
        public static ProjectCardState ProbeProject(string path)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(path) || !Directory.Exists(path))
                    return ProjectCardState.Missing;
                if (!ProjectOpenCoordinator.TryResolve(path, out _))
                    return ProjectCardState.Invalid;
                return ProjectCardState.Ready;
            }
            catch
            {
                return ProjectCardState.Missing;
            }
        }

        public void LoadProjects()
        {
            ReloadProjectsCore();
        }

        public void RefreshProjects()
        {
            ReloadProjectsCore();
        }

        // Tek liste yenileme; sayaç metni burada üretilir.
        private void ReloadProjectsCore()
        {
            _registry.Load();
            Projects.Clear();
            var ordered = _registry.Projects.OrderByDescending(p => p.LastOpenedAt).ToList();
            foreach (var info in ordered)
            {
                Projects.Add(new ProjectCardViewModel(info, this, _registry));
            }

            IsEmpty = Projects.Count == 0;
            if (IsEmpty)
            {
                SetStatus(HubStatusKind.Neutral, EditorStrings.T("Hub.Status.Empty"));
                return;
            }

            int missing = Projects.Count(c => c.IsUnavailable);
            if (missing > 0)
            {
                SetStatus(HubStatusKind.Neutral, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.CountWithMissing"),
                    Projects.Count, missing));
            }
            else
            {
                SetStatus(HubStatusKind.Neutral, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.Count"),
                    Projects.Count));
            }
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
                string msg = string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Dialog.CreateFailed"),
                    name, error ?? string.Empty);
                var errDlg = new ConfirmDialog(
                    EditorStrings.T("Hub.Dialog.Error.Title"),
                    msg,
                    EditorStrings.T("Hub.Dialog.Ok"));
                await errDlg.ShowDialog(window);
                SetStatus(HubStatusKind.Error, msg);
                return;
            }

            _registry.Add(info);
            ReloadProjectsCore();
            SetStatus(HubStatusKind.Success, string.Format(
                CultureInfo.CurrentCulture,
                EditorStrings.T("Hub.Status.Created"), name));
        }

        [RelayCommand]
        public async Task ImportExistingAsync()
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var folders = await window.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = EditorStrings.T("Hub.Picker.ImportFolder"),
                AllowMultiple = false
            });

            if (folders.Count == 0) return;

            string selectedPath = folders[0].Path.LocalPath;
            ImportFolder(selectedPath);
        }

        // Seçici sonrası doğrulamalı ekleme.
        internal bool ImportFolder(string selectedPath)
        {
            string full;
            try
            {
                if (string.IsNullOrWhiteSpace(selectedPath))
                {
                    SetStatus(HubStatusKind.Error, EditorStrings.T("Hub.Status.NotAProject"));
                    return false;
                }
                full = Path.GetFullPath(selectedPath);
            }
            catch
            {
                SetStatus(HubStatusKind.Error, EditorStrings.T("Hub.Status.NotAProject"));
                return false;
            }

            if (ProbeProject(full) != ProjectCardState.Ready)
            {
                SetStatus(HubStatusKind.Error, EditorStrings.T("Hub.Status.NotAProject"));
                return false;
            }

            string normFull = full.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string name = Path.GetFileName(normFull);
            if (string.IsNullOrEmpty(name))
                name = full;

            var existing = _registry.Projects.FirstOrDefault(p =>
            {
                try
                {
                    string ef = Path.GetFullPath(p.Path).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                    return string.Equals(ef, normFull, StringComparison.OrdinalIgnoreCase);
                }
                catch
                {
                    return string.Equals(p.Path, selectedPath, StringComparison.OrdinalIgnoreCase);
                }
            });
            if (existing != null)
            {
                SetStatus(HubStatusKind.Neutral, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.AlreadyListed"), existing.Name));
                return false;
            }

            var info = new ProjectInfo
            {
                Id = Guid.NewGuid().ToString("N"),
                Name = name,
                Path = full,
                CreatedAt = DateTime.UtcNow,
                LastOpenedAt = DateTime.UtcNow
            };

            _registry.Add(info);
            ReloadProjectsCore();
            SetStatus(HubStatusKind.Success, string.Format(
                CultureInfo.CurrentCulture,
                EditorStrings.T("Hub.Status.Imported"), name));
            return true;
        }

        public void OpenProject(ProjectCardViewModel card)
        {
            card.Reprobe();
            if (!card.CanOpen)
            {
                SetStatus(HubStatusKind.Error, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.OpenFailed"),
                    card.Name, card.StateText));
                return;
            }

            _registry.Touch(card.Info.Id);
            ProjectOpened?.Invoke(card.Path);
        }

        // Onaysız kaldırma; diske dokunmaz.
        public void RemoveProject(ProjectCardViewModel card)
        {
            if (card == null) return;
            string name = card.Name;
            _registry.Remove(card.Info.Id);
            ReloadProjectsCore();
            SetStatus(HubStatusKind.Neutral, string.Format(
                CultureInfo.CurrentCulture,
                EditorStrings.T("Hub.Status.Removed"), name));
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
                var errDlg = new ConfirmDialog(
                    EditorStrings.T("Hub.Dialog.RenameFailed.Title"),
                    EditorStrings.T("Hub.Dialog.RenameFailed.Message"),
                    EditorStrings.T("Hub.Dialog.Ok"));
                await errDlg.ShowDialog(window);
                SetStatus(HubStatusKind.Error, EditorStrings.T("Hub.Status.RenameFailed"));
                return;
            }

            ReloadProjectsCore();
            SetStatus(HubStatusKind.Success, string.Format(
                CultureInfo.CurrentCulture,
                EditorStrings.T("Hub.Status.Renamed"), newName.Trim()));
        }

        public async Task SetCoverAsync(ProjectCardViewModel card)
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var files = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = EditorStrings.T("Hub.Picker.Cover"),
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType(EditorStrings.T("Hub.Picker.ImageFiles"))
                    {
                        Patterns = new[] { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga" }
                    }
                }
            });

            if (files.Count == 0) return;

            string src = files[0].Path.LocalPath;
            if (MediaFormatCatalog.RequiresExplicitRejection(src))
            {
                string reason = MediaFormatCatalog.RejectionMessage(Path.GetFileName(src));
                SetStatus(HubStatusKind.Error, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.CoverRejected"), reason));
                return;
            }
            try
            {
                string ext = Path.GetExtension(src);
                string destName = $"cover_{DateTime.Now:yyyyMMddHHmmss}{ext}";
                string destPath = System.IO.Path.Combine(card.Path, destName);
                File.Copy(src, destPath, true);

                _registry.UpdateCover(card.Info.Id, destName);
                ReloadProjectsCore();
                SetStatus(HubStatusKind.Success, EditorStrings.T("Hub.Status.CoverUpdated"));
            }
            catch (Exception ex)
            {
                SetStatus(HubStatusKind.Error, string.Format(
                    CultureInfo.CurrentCulture,
                    EditorStrings.T("Hub.Status.CoverFailed"), ex.Message));
            }
        }

        public async Task DeleteProjectAsync(ProjectCardViewModel card)
        {
            var window = HubWindow ?? GetActiveWindow();
            if (window == null) return;

            var dlg = new ConfirmDialog(
                EditorStrings.T("Hub.Dialog.Delete.Title"),
                string.Format(CultureInfo.CurrentCulture, EditorStrings.T("Hub.Dialog.Delete.Message"), card.Name),
                EditorStrings.T("Hub.Dialog.Delete.Confirm"),
                true);
            bool? confirmed = await dlg.ShowDialog<bool?>(window);
            if (confirmed != true) return;

            string name = card.Name;
            _registry.Remove(card.Info.Id);
            ReloadProjectsCore();
            SetStatus(HubStatusKind.Success, string.Format(
                CultureInfo.CurrentCulture,
                EditorStrings.T("Hub.Status.Removed"), name));
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
