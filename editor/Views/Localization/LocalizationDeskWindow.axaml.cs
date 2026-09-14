using System;
using System.IO;
using System.Linq;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using RowlEngine.Editor.ViewModels.Localization;

namespace RowlEngine.Editor.Views.Localization
{
    /// <summary>
    /// Faz 3 Dilim 4 — Translation Desk shell. Pure binding host plus
    /// file-picker wiring (export/import); all desk logic lives in
    /// <see cref="LocalizationDeskViewModel"/> and the Localization services.
    /// </summary>
    public partial class LocalizationDeskWindow : Window
    {
        public LocalizationDeskWindow()
        {
            InitializeComponent();
        }

        public LocalizationDeskWindow(LocalizationDeskViewModel viewModel) : this()
        {
            DataContext = viewModel ?? throw new ArgumentNullException(nameof(viewModel));

            var exportCsv = this.FindControl<Button>("ExportCsvButton");
            if (exportCsv != null)
                exportCsv.Click += async (_, _) => await ExportTextAsync(
                    viewModel.ExportCsvText(), "translations", "CSV files", "*.csv");

            var exportJson = this.FindControl<Button>("ExportJsonButton");
            if (exportJson != null)
                exportJson.Click += async (_, _) => await ExportTextAsync(
                    viewModel.ExportCatalogJsonText(), "locale", "JSON files", "*.json");

            var importCsv = this.FindControl<Button>("ImportCsvButton");
            if (importCsv != null)
                importCsv.Click += async (_, _) =>
                {
                    var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
                    {
                        Title = "CSV Çeviri İçe Aktar",
                        AllowMultiple = false,
                        FileTypeFilter = new[] { new FilePickerFileType("CSV files") { Patterns = new[] { "*.csv" } } },
                    });
                    IStorageFile? picked = files?.FirstOrDefault();
                    if (picked is null)
                        return;
                    string csv;
                    await using (var stream = await picked.OpenReadAsync())
                    using (var reader = new StreamReader(stream))
                        csv = await reader.ReadToEndAsync();
                    viewModel.ImportCsvText(csv);
                };
        }

        private async System.Threading.Tasks.Task ExportTextAsync(
            string content, string suggestedName, string filterName, string pattern)
        {
            var file = await StorageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
            {
                Title = "Çeviri Dışa Aktar",
                SuggestedFileName = suggestedName,
                FileTypeChoices = new[] { new FilePickerFileType(filterName) { Patterns = new[] { pattern } } },
            });
            if (file is null)
                return;
            await using (var stream = await file.OpenWriteAsync())
            await using (var writer = new StreamWriter(stream))
                await writer.WriteAsync(content);
        }
    }
}
