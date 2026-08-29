using System;
using System.IO;
using Avalonia.Controls;
using Avalonia.Platform.Storage;

namespace RowlEngine.Editor.Views.Dialogs
{
    public partial class CreateProjectDialog : Window
    {
        public CreateProjectDialog()
        {
            InitializeComponent();

            string defaultFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Belgeler", "Rowl Engine Project");
            if (!Directory.Exists(defaultFolder))
            {
                try { Directory.CreateDirectory(defaultFolder); } catch { }
            }

            var folderBox = this.FindControl<TextBox>("FolderBox");
            if (folderBox != null) folderBox.Text = defaultFolder;

            var browseBtn = this.FindControl<Button>("BrowseButton");
            if (browseBtn != null)
            {
                browseBtn.Click += async (_, _) =>
                {
                    var topLevel = TopLevel.GetTopLevel(this);
                    if (topLevel != null)
                    {
                        var folders = await topLevel.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
                        {
                            Title = "Proje Klasörünü Seçin",
                            AllowMultiple = false
                        });
                        if (folders.Count > 0 && folderBox != null)
                        {
                            folderBox.Text = folders[0].Path.LocalPath;
                        }
                    }
                };
            }

            var cancelBtn = this.FindControl<Button>("CancelButton");
            if (cancelBtn != null) cancelBtn.Click += (_, _) => Close(null);

            var createBtn = this.FindControl<Button>("CreateButton");
            var nameBox = this.FindControl<TextBox>("NameBox");
            var errorText = this.FindControl<TextBlock>("ErrorText");

            if (createBtn != null)
            {
                createBtn.Click += (_, _) =>
                {
                    string name = nameBox?.Text?.Trim() ?? "";
                    string folder = folderBox?.Text?.Trim() ?? "";

                    if (string.IsNullOrWhiteSpace(name))
                    {
                        if (errorText != null) { errorText.Text = "Proje adı boş olamaz."; errorText.IsVisible = true; }
                        return;
                    }
                    if (string.IsNullOrWhiteSpace(folder) || !Directory.Exists(folder))
                    {
                        if (errorText != null) { errorText.Text = "Geçersiz hedef klasör."; errorText.IsVisible = true; }
                        return;
                    }

                    Close(((string name, string folder)?)(name, folder));
                };
            }
        }
    }
}
