using System;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Extras
{
    /// <summary>
    /// D07-3 — the extras catalog editor window: a binding shell. Lists, fields, checks and
    /// saving live in <see cref="ExtrasCatalogEditorViewModel"/>; only the OS file pickers are here.
    /// </summary>
    public partial class ExtrasCatalogEditorWindow : Window
    {
        public ExtrasCatalogEditorWindow()
        {
            InitializeComponent();
            var close = this.FindControl<Button>("CloseButton");
            if (close != null) close.Click += (_, _) => Close();
            Wire("GalleryImagePick", () => PickImageAsync(thumbnail: false));
            Wire("GalleryThumbPick", () => PickImageAsync(thumbnail: true));
            Wire("MusicTrackPick", PickTrackAsync);
        }

        public ExtrasCatalogEditorWindow(ExtrasCatalogEditorViewModel viewModel) : this()
        {
            DataContext = viewModel ?? throw new ArgumentNullException(nameof(viewModel));
        }

        private void Wire(string name, Func<System.Threading.Tasks.Task> action)
        {
            var button = this.FindControl<Button>(name);
            if (button != null) button.Click += async (_, _) => await action();
        }

        private async System.Threading.Tasks.Task PickImageAsync(bool thumbnail)
        {
            if (DataContext is not ExtrasCatalogEditorViewModel vm) return;
            var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = RowlEngine.Editor.Services.Strings.EditorStrings.T("Extras.Btn.Pick"),
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType(MediaFormatCatalog.ImagePickerLabel) { Patterns = MediaFormatCatalog.ImagePickerPatterns }
                }
            });
            if (files.Count > 0) vm.ApplyPickedImage(files[0].Path.LocalPath, thumbnail);
        }

        private async System.Threading.Tasks.Task PickTrackAsync()
        {
            if (DataContext is not ExtrasCatalogEditorViewModel vm) return;
            var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = RowlEngine.Editor.Services.Strings.EditorStrings.T("Extras.Btn.Pick"),
                AllowMultiple = false
            });
            if (files.Count > 0) vm.ApplyPickedTrack(files[0].Path.LocalPath);
        }
    }
}
