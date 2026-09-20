using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Localization;

namespace RowlEngine.Editor.ViewModels.Localization;

/// <summary>
/// Faz 3 Dilim 4 — Translation Desk state. Owns inventory loading, status
/// matching, filtering/search and catalog persistence; the Window is a thin
/// binding shell and file pickers stay in code-behind. Every behavior here
/// runs headless against a temp project directory (see xUnit slice 4).
/// </summary>
public partial class LocalizationDeskViewModel : ViewModelBase
{
    private readonly string _projectRoot;
    private List<TranslationRow> _allRows = new();

    public LocalizationDeskViewModel(string projectRoot)
    {
        _projectRoot = projectRoot ?? string.Empty;
        AvailableLocales = LoadAvailableLocales(_projectRoot);
        SourceLocale = AvailableLocales.Contains("en", StringComparer.Ordinal)
            ? "en" : AvailableLocales[0];
        TargetLocale = AvailableLocales.FirstOrDefault(
            code => !string.Equals(code, SourceLocale, StringComparison.Ordinal))
            ?? SourceLocale;
        Refresh();
    }

    public IReadOnlyList<string> AvailableLocales { get; }

    [ObservableProperty]
    private string _sourceLocale = "en";

    [ObservableProperty]
    private string _targetLocale = "tr";

    [ObservableProperty]
    private TranslationFilter _selectedFilter = TranslationFilter.All;

    /// <summary>Index mirror of <see cref="SelectedFilter"/> for the XAML filter combo.</summary>
    public int SelectedFilterIndex
    {
        get => (int)SelectedFilter;
        set
        {
            if (value >= 0 && value <= (int)TranslationFilter.ChangedOnly)
                SelectedFilter = (TranslationFilter)value;
        }
    }

    [ObservableProperty]
    private string _searchText = string.Empty;

    [ObservableProperty]
    private string _statusLine = string.Empty;

    public ObservableCollection<TranslationRow> FilteredRows { get; } = new();

    [ObservableProperty]
    private TranslationRow? _selectedRow;

    [ObservableProperty]
    private string _editSpeaker = string.Empty;

    [ObservableProperty]
    private string _editTranslation = string.Empty;

    [ObservableProperty]
    private string _editAltText = string.Empty;

    partial void OnSourceLocaleChanged(string value) => Refresh();
    partial void OnTargetLocaleChanged(string value) => Refresh();
    partial void OnSelectedFilterChanged(TranslationFilter value)
    {
        ApplyViewFilter();
        OnPropertyChanged(nameof(SelectedFilterIndex));
    }
    partial void OnSearchTextChanged(string value) => ApplyViewFilter();

    partial void OnSelectedRowChanged(TranslationRow? value)
    {
        EditSpeaker = value?.Speaker ?? string.Empty;
        EditTranslation = value?.TranslatedText ?? string.Empty;
        EditAltText = value?.AltText ?? string.Empty;
    }

    /// <summary>Reloads inventory + catalog and rebuilds the visible rows.</summary>
    [RelayCommand]
    public void Refresh()
    {
        var inventory = TranslationInventoryService.BuildInventory(_projectRoot, out string? inventoryError);
        var rows = TranslationStatusService.BuildRows(inventory, _projectRoot, TargetLocale, out string? catalogError);
        _allRows = rows;
        ApplyViewFilter();
        var (translated, missing, changed) = TranslationStatusService.CountStates(_allRows);
        StatusLine = $"{_allRows.Count} satır: {translated} çevrilmiş, {missing} eksik, {changed} değişmiş" +
            (inventoryError is not null ? $" — {inventoryError}" : string.Empty) +
            (catalogError is not null ? $" — {catalogError}" : string.Empty);
        if (SelectedRow is not null)
        {
            TranslationRow? reselected = _allRows.FirstOrDefault(
                r => string.Equals(r.ContentId, SelectedRow.ContentId, StringComparison.Ordinal));
            SelectedRow = reselected;
        }
    }

    /// <summary>
    /// Stages the editor fields into the selected row (no disk write yet).
    /// Returns false when nothing is selected.
    /// </summary>
    public bool StageSelectedRowEdits()
    {
        if (SelectedRow is null)
            return false;
        SelectedRow.Speaker = EditSpeaker;
        SelectedRow.TranslatedText = EditTranslation;
        SelectedRow.AltText = EditAltText;
        SelectedRow.State = string.IsNullOrEmpty(EditTranslation)
            ? TranslationState.Missing
            : TranslationState.Translated;
        SelectedRow.LegacyNoHash = false;
        ApplyViewFilter();
        return true;
    }

    /// <summary>Stages the selection (if any) and saves the catalog atomically.</summary>
    [RelayCommand]
    public void Save()
    {
        StageSelectedRowEdits();
        string? error = TranslationStatusService.SaveCatalog(_projectRoot, TargetLocale, _allRows);
        Refresh();
        if (error is not null)
            StatusLine += $" — {error}";
        else
            StatusLine += " — kaydedildi";
    }

    /// <summary>Serializes the current rows for a spreadsheet round trip.</summary>
    public string ExportCsvText() => TranslationExchangeService.ExportCsv(_allRows);

    /// <summary>Applies a translator CSV onto the rows; message describes the outcome.</summary>
    public string ImportCsvText(string? csv)
    {
        ImportResult result = TranslationExchangeService.TryImportCsv(csv, _allRows);
        if (!result.Accepted)
            return $"İçe aktarma reddedildi: {result.Error}";
        ApplyViewFilter();
        RefreshCountsOnly();
        return $"{result.UpdatedCount} satır güncellendi" +
            (result.SkippedUnknownCount > 0 ? $", {result.SkippedUnknownCount} bilinmeyen anahtar atlandı" : string.Empty);
    }

    /// <summary>Serializes the current rows as a catalog JSON document.</summary>
    public string ExportCatalogJsonText() =>
        TranslationExchangeService.ExportCatalogJson(TargetLocale, _allRows);

    /// <summary>
    /// Generates the pseudo-locale test catalog into
    /// <c>Assets/locales/qps-ploc.json</c> atomically.
    /// </summary>
    [RelayCommand]
    public void GeneratePseudoCatalog()
    {
        var inventory = TranslationInventoryService.BuildInventory(_projectRoot, out string? error);
        if (error is not null && inventory.Count == 0)
        {
            StatusLine += $" — pseudo üretilemedi: {error}";
            return;
        }
        try
        {
            string localesDir = TranslationStatusService.GetLocalesDirectory(_projectRoot);
            Directory.CreateDirectory(localesDir);
            string json = PseudoLocaleGenerator.GenerateCatalog(inventory);
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(localesDir, PseudoLocaleGenerator.PseudoFileName), json);
            StatusLine += $" — pseudo katalog yazıldı ({PseudoLocaleGenerator.PseudoFileName})";
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            StatusLine += $" — pseudo yazılamadı: {exception.Message}";
        }
    }

    private void ApplyViewFilter()
    {
        IReadOnlyList<TranslationRow> visible =
            TranslationStatusService.ApplyFilter(_allRows, SelectedFilter, SearchText);
        FilteredRows.Clear();
        foreach (TranslationRow row in visible)
            FilteredRows.Add(row);
    }

    private void RefreshCountsOnly()
    {
        var (translated, missing, changed) = TranslationStatusService.CountStates(_allRows);
        StatusLine = $"{_allRows.Count} satır: {translated} çevrilmiş, {missing} eksik, {changed} değişmiş";
    }

    private static IReadOnlyList<string> LoadAvailableLocales(string projectRoot)
    {
        try
        {
            string manifestPath = Path.Combine(projectRoot, "project.rowlproj");
            if (!File.Exists(manifestPath))
                return new[] { LocalizationService.FallbackDefaultLocale };
            ManifestLocales parsed = LocalizationService.ParseManifestLocales(
                File.ReadAllText(manifestPath));
            return parsed.SupportedLocales.Count > 0
                ? parsed.SupportedLocales
                : new[] { LocalizationService.FallbackDefaultLocale };
        }
        catch (Exception)
        {
            return new[] { LocalizationService.FallbackDefaultLocale };
        }
    }
}
