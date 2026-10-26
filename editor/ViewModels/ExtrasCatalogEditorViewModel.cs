using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Extras;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels;

/// <summary>
/// D07-3 — author surface for the project's unlockable-content catalog
/// (<c>Assets/json/extras.json</c>, D07-1): three lists (gallery, music room, achievements) with
/// add, remove, move up / down and the per-field editors. Every change is checked with the native
/// engine's rules (<see cref="ExtrasCatalogModel.Validate"/>); saving is refused while any item
/// would be skipped by the player, and writes the file atomically with unknown keys kept.
/// The window is a thin shell (file pickers live in its code-behind); everything here runs headless.
/// </summary>
public partial class ExtrasCatalogEditorViewModel : ViewModelBase
{
    public const int GalleryTab = 0;
    public const int MusicTab = 1;
    public const int AchievementsTab = 2;

    private readonly string _assetsPath;
    private ExtrasCatalogModel _model = new();
    private bool _loading;

    /// <summary>Copies a picked image into the project; returns the name to store (tests replace it).</summary>
    internal Func<string, string> ImportImage { get; set; }

    /// <summary>Copies a picked audio file into the project; returns the name to store.</summary>
    internal Func<string, string> ImportAudio { get; set; }

    public ExtrasCatalogEditorViewModel(string assetsPath)
    {
        _assetsPath = assetsPath ?? string.Empty;
        ImportImage = path => EditorAssetImportService.ImportImageFile(path, _assetsPath);
        ImportAudio = path => EditorAssetImportService.ImportAudioFile(path, _assetsPath);
        Load();
    }

    /// <summary>The catalog being edited (an empty model while the file cannot be edited).</summary>
    public ExtrasCatalogModel Model => _model;

    public ObservableCollection<ExtrasGalleryItem> Gallery => _model.Gallery;
    public ObservableCollection<ExtrasMusicItem> Music => _model.Music;
    public ObservableCollection<ExtrasAchievementItem> Achievements => _model.Achievements;

    /// <summary>False when extras.json exists but the engine would ignore it whole (nothing to edit safely).</summary>
    [ObservableProperty]
    private bool _canEdit = true;

    /// <summary>Why the file cannot be edited (empty when it can).</summary>
    [ObservableProperty]
    private string _loadErrorText = string.Empty;

    [ObservableProperty]
    private bool _isDirty;

    /// <summary>Problems found by the last check (0 = the player loads every item).</summary>
    [ObservableProperty]
    private int _issueCount;

    [ObservableProperty]
    private string _statusText = string.Empty;

    [ObservableProperty]
    private int _selectedTabIndex;

    [ObservableProperty]
    private ExtrasGalleryItem? _selectedGallery;

    [ObservableProperty]
    private ExtrasMusicItem? _selectedMusic;

    [ObservableProperty]
    private ExtrasAchievementItem? _selectedAchievement;

    public bool HasGallerySelection => SelectedGallery is not null;
    public bool HasMusicSelection => SelectedMusic is not null;
    public bool HasAchievementSelection => SelectedAchievement is not null;

    public string GalleryHeader => EditorStrings.T("Extras.Tab.Gallery") + $" ({Gallery.Count})";
    public string MusicHeader => EditorStrings.T("Extras.Tab.Music") + $" ({Music.Count})";
    public string AchievementsHeader => EditorStrings.T("Extras.Tab.Achievements") + $" ({Achievements.Count})";

    /// <summary>Notes about entries the editor could not keep (shown above the lists).</summary>
    public string DroppedNote => _model.DroppedEntryCount > 0
        ? string.Format(EditorStrings.T("Extras.Note.Dropped"), _model.DroppedEntryCount)
        : string.Empty;

    public bool HasDroppedNote => _model.DroppedEntryCount > 0;

    public bool CanSave => CanEdit && IsDirty && IssueCount == 0;

    partial void OnCanEditChanged(bool value) => RefreshState();
    partial void OnIsDirtyChanged(bool value) => RefreshState();
    partial void OnIssueCountChanged(int value) => RefreshState();
    partial void OnSelectedGalleryChanged(ExtrasGalleryItem? value) => OnPropertyChanged(nameof(HasGallerySelection));
    partial void OnSelectedMusicChanged(ExtrasMusicItem? value) => OnPropertyChanged(nameof(HasMusicSelection));
    partial void OnSelectedAchievementChanged(ExtrasAchievementItem? value) => OnPropertyChanged(nameof(HasAchievementSelection));

    private void RefreshState()
    {
        OnPropertyChanged(nameof(CanSave));
        SaveCommand.NotifyCanExecuteChanged();
        StatusText = BuildStatus();
    }

    private string BuildStatus()
    {
        if (!CanEdit) return LoadErrorText;
        if (IssueCount > 0) return string.Format(EditorStrings.T("Extras.Status.Issues"), IssueCount);
        return IsDirty ? EditorStrings.T("Extras.Status.Dirty") : EditorStrings.T("Extras.Status.Saved");
    }

    // ── Loading ──────────────────────────────────────────────────────────

    private void Load()
    {
        var result = ExtrasCatalogStore.Load(_assetsPath);
        _loading = true;
        try
        {
            Detach(_model);
            _model = result.Model ?? new ExtrasCatalogModel();
            Attach(_model);
            CanEdit = result.Model is not null;
            LoadErrorText = result.Model is null ? DescribeLoadError(result.Error) : string.Empty;
            SelectedGallery = _model.Gallery.FirstOrDefault();
            SelectedMusic = _model.Music.FirstOrDefault();
            SelectedAchievement = _model.Achievements.FirstOrDefault();
            IsDirty = false;
        }
        finally
        {
            _loading = false;
        }
        OnPropertyChanged(nameof(Model));
        OnPropertyChanged(nameof(Gallery));
        OnPropertyChanged(nameof(Music));
        OnPropertyChanged(nameof(Achievements));
        OnPropertyChanged(nameof(DroppedNote));
        OnPropertyChanged(nameof(HasDroppedNote));
        RaiseHeaders();
        Revalidate();
    }

    private static string DescribeLoadError(string? error) => error switch
    {
        "FileTooLarge" => EditorStrings.T("Extras.Load.TooLarge"),
        "NotJson" => EditorStrings.T("Extras.Load.NotJson"),
        "NotAnObject" => EditorStrings.T("Extras.Load.NotJson"),
        "NoFormat1" => EditorStrings.T("Extras.Load.NoFormat"),
        _ => EditorStrings.T("Extras.Load.Unreadable"),
    };

    [RelayCommand]
    private void Reload() => Load();

    private void Attach(ExtrasCatalogModel model)
    {
        model.Gallery.CollectionChanged += OnListChanged;
        model.Music.CollectionChanged += OnListChanged;
        model.Achievements.CollectionChanged += OnListChanged;
        foreach (var item in model.AllItems) item.PropertyChanged += OnItemChanged;
    }

    private void Detach(ExtrasCatalogModel model)
    {
        model.Gallery.CollectionChanged -= OnListChanged;
        model.Music.CollectionChanged -= OnListChanged;
        model.Achievements.CollectionChanged -= OnListChanged;
        foreach (var item in model.AllItems) item.PropertyChanged -= OnItemChanged;
    }

    private void OnListChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
            foreach (ExtrasItem item in e.OldItems) item.PropertyChanged -= OnItemChanged;
        if (e.NewItems is not null)
            foreach (ExtrasItem item in e.NewItems) item.PropertyChanged += OnItemChanged;
        RaiseHeaders();
        if (_loading) return;
        IsDirty = true;
        Revalidate();
    }

    private void OnItemChanged(object? sender, PropertyChangedEventArgs e)
    {
        // The check's own results and the derived caption are not author edits.
        if (_loading || e.PropertyName is nameof(ExtrasItem.HasIssue) or nameof(ExtrasItem.IssueText)
            or nameof(ExtrasItem.DisplayLabel))
            return;
        IsDirty = true;
        Revalidate();
    }

    private void RaiseHeaders()
    {
        OnPropertyChanged(nameof(GalleryHeader));
        OnPropertyChanged(nameof(MusicHeader));
        OnPropertyChanged(nameof(AchievementsHeader));
    }

    // ── Checking ─────────────────────────────────────────────────────────

    /// <summary>Re-runs the native rules and marks each item that would be skipped.</summary>
    public void Revalidate()
    {
        var issues = _model.Validate();
        var lines = new Dictionary<(string Kind, int Index), List<string>>();
        foreach (var issue in issues)
        {
            if (!issue.IsItemIssue || issue.Kind is null) continue;
            var key = (issue.Kind, issue.Index);
            if (!lines.TryGetValue(key, out var list)) lines[key] = list = new List<string>();
            string text = Describe(issue);
            if (!list.Contains(text)) list.Add(text);
        }
        foreach (string kind in new[] { ExtrasRules.KindCg, ExtrasRules.KindMusic, ExtrasRules.KindAchievement })
        {
            var items = _model.ItemsOf(kind);
            for (int i = 0; i < items.Count; i++)
            {
                bool has = lines.TryGetValue((kind, i), out var list);
                items[i].HasIssue = has;
                items[i].IssueText = has ? string.Join("\n", list!) : string.Empty;
            }
        }
        IssueCount = issues.Count;
        RefreshState();
    }

    /// <summary>The editor sentence for one issue (editor strings, tr/en).</summary>
    public static string Describe(ExtrasIssue issue) => issue.Code switch
    {
        ExtrasIssueCode.InvalidId => EditorStrings.T("Extras.Issue.InvalidId"),
        ExtrasIssueCode.DuplicateId => EditorStrings.T("Extras.Issue.DuplicateId"),
        ExtrasIssueCode.MissingImage => EditorStrings.T("Extras.Issue.MissingImage"),
        ExtrasIssueCode.MissingTrack => EditorStrings.T("Extras.Issue.MissingTrack"),
        ExtrasIssueCode.UnsafePath => EditorStrings.T("Extras.Issue.UnsafePath"),
        ExtrasIssueCode.TextTooLong => EditorStrings.T("Extras.Issue.TextTooLong"),
        ExtrasIssueCode.WrongType => EditorStrings.T("Extras.Issue.WrongType"),
        ExtrasIssueCode.TooManyItems => EditorStrings.T("Extras.Issue.TooManyItems"),
        ExtrasIssueCode.FileTooLarge => EditorStrings.T("Extras.Issue.FileTooLarge"),
        _ => issue.Code.ToString(),
    };

    // ── List editing (the current tab) ───────────────────────────────────

    [RelayCommand]
    private void AddItem()
    {
        if (!CanEdit) return;
        switch (SelectedTabIndex)
        {
            case GalleryTab:
            {
                var item = new ExtrasGalleryItem { Id = FreshId("cg", _model.Gallery.Select(i => i.Id)) };
                _model.Gallery.Add(item);
                SelectedGallery = item;
                break;
            }
            case MusicTab:
            {
                var item = new ExtrasMusicItem { Id = FreshId("track", _model.Music.Select(i => i.Id)) };
                _model.Music.Add(item);
                SelectedMusic = item;
                break;
            }
            default:
            {
                var item = new ExtrasAchievementItem { Id = FreshId("ach", _model.Achievements.Select(i => i.Id)) };
                _model.Achievements.Add(item);
                SelectedAchievement = item;
                break;
            }
        }
    }

    private static string FreshId(string prefix, IEnumerable<string> existing)
    {
        var taken = new HashSet<string>(existing, StringComparer.Ordinal);
        for (int n = 1; ; n++)
        {
            string candidate = $"{prefix}_{n}";
            if (!taken.Contains(candidate)) return candidate;
        }
    }

    [RelayCommand]
    private void RemoveItem()
    {
        switch (SelectedTabIndex)
        {
            case GalleryTab: Remove(_model.Gallery, SelectedGallery, v => SelectedGallery = v); break;
            case MusicTab: Remove(_model.Music, SelectedMusic, v => SelectedMusic = v); break;
            default: Remove(_model.Achievements, SelectedAchievement, v => SelectedAchievement = v); break;
        }
    }

    private static void Remove<T>(ObservableCollection<T> list, T? selected, Action<T?> select) where T : ExtrasItem
    {
        if (selected is null) return;
        int index = list.IndexOf(selected);
        if (index < 0) return;
        list.RemoveAt(index);
        select(list.Count == 0 ? null : list[Math.Min(index, list.Count - 1)]);
    }

    [RelayCommand]
    private void MoveItemUp() => Move(-1);

    [RelayCommand]
    private void MoveItemDown() => Move(1);

    private void Move(int delta)
    {
        switch (SelectedTabIndex)
        {
            case GalleryTab: Shift(_model.Gallery, SelectedGallery, delta, v => SelectedGallery = v); break;
            case MusicTab: Shift(_model.Music, SelectedMusic, delta, v => SelectedMusic = v); break;
            default: Shift(_model.Achievements, SelectedAchievement, delta, v => SelectedAchievement = v); break;
        }
    }

    private static void Shift<T>(ObservableCollection<T> list, T? selected, int delta, Action<T?> select) where T : ExtrasItem
    {
        if (selected is null) return;
        int from = list.IndexOf(selected);
        int to = from + delta;
        if (from < 0 || to < 0 || to >= list.Count) return;
        list.Move(from, to);
        select(selected);   // a Move makes the ListBox report an empty selection for a moment
    }

    // ── File picking (the window opens the OS dialog; the result lands here) ──

    /// <summary>Stores a picked image file (copied into the project) as the gallery item's image or thumbnail.</summary>
    public bool ApplyPickedImage(string fullPath, bool thumbnail)
    {
        if (SelectedGallery is not { } item || string.IsNullOrWhiteSpace(fullPath)) return false;
        string name = ImportImage(fullPath);
        if (string.IsNullOrWhiteSpace(name)) return false;
        if (thumbnail) item.Thumbnail = name; else item.Image = name;
        return true;
    }

    /// <summary>Stores a picked audio file (copied into the project) as the music item's track.</summary>
    public bool ApplyPickedTrack(string fullPath)
    {
        if (SelectedMusic is not { } item || string.IsNullOrWhiteSpace(fullPath)) return false;
        string name = ImportAudio(fullPath);
        if (string.IsNullOrWhiteSpace(name)) return false;
        // Stored with its folder, like the shipped samples ("audio/nokturn.ogg").
        item.Track = name.Contains('/') || name.Contains('\\') ? name : "audio/" + name;
        return true;
    }

    // ── Saving ───────────────────────────────────────────────────────────

    [RelayCommand(CanExecute = nameof(CanSave))]
    private void Save()
    {
        Revalidate();
        if (!CanSave) return;
        try
        {
            var issues = ExtrasCatalogStore.Save(_assetsPath, _model);
            if (issues.Count > 0)
            {
                Revalidate();
                return;
            }
            IsDirty = false;
        }
        catch (Exception error) when (error is System.IO.IOException or UnauthorizedAccessException)
        {
            StatusText = string.Format(EditorStrings.T("Extras.Status.SaveFailed"), error.Message);
        }
    }

    /// <summary>Drops the unsaved edits and reads the file again.</summary>
    public void Discard() => Load();
}
