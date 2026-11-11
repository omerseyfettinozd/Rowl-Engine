using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.Services.Extras;

/// <summary>What a catalog check found. The dialog maps each code to an editor string.</summary>
public enum ExtrasIssueCode
{
    InvalidId,
    DuplicateId,
    MissingImage,
    MissingTrack,
    UnsafePath,
    TextTooLong,
    WrongType,
    TooManyItems,
    FileTooLarge,
}

/// <param name="Kind">The list (<c>cg</c>, <c>music</c>, <c>achievement</c>); null for a file-level issue.</param>
/// <param name="Index">Position in that list, -1 for a list or file level issue.</param>
/// <param name="Field">The field name the issue is about (<c>id</c>, <c>image</c>, ...), empty when general.</param>
public sealed record ExtrasIssue(string? Kind, int Index, string Field, ExtrasIssueCode Code)
{
    public bool IsItemIssue => Index >= 0;
}

/// <summary>
/// A string that is plain or per-language (<c>"Nokturn"</c> or <c>{"tr": "...", "en": "..."}</c>).
/// Entries keep file order; a plain string lives under the empty tag and answers every language.
/// Editing one language of a plain string first expands it to explicit <c>tr</c> and <c>en</c>
/// entries with the same text, so no other language changes meaning.
/// </summary>
public sealed class ExtrasLocalizedText
{
    private readonly List<KeyValuePair<string, string>> _entries = new();

    public IReadOnlyList<KeyValuePair<string, string>> Entries => _entries;

    public bool IsEmpty => _entries.Count == 0;

    /// <summary>The entry of <paramref name="language"/> (lowercase tag), else the plain text, else empty.</summary>
    public string Get(string language)
    {
        foreach (var pair in _entries)
        {
            if (pair.Key == language) return pair.Value;
        }
        foreach (var pair in _entries)
        {
            if (pair.Key.Length == 0) return pair.Value;
        }
        return string.Empty;
    }

    /// <summary>Sets one language. Returns false when nothing changed.</summary>
    public bool Set(string language, string? text)
    {
        text ??= string.Empty;
        if (string.Equals(Get(language), text, StringComparison.Ordinal)) return false;
        ExpandPlain();
        int at = _entries.FindIndex(pair => pair.Key == language);
        if (text.Length == 0)
        {
            if (at >= 0) _entries.RemoveAt(at);
        }
        else if (at >= 0)
        {
            _entries[at] = new KeyValuePair<string, string>(language, text);
        }
        else
        {
            _entries.Add(new KeyValuePair<string, string>(language, text));
        }
        return true;
    }

    private void ExpandPlain()
    {
        int plain = _entries.FindIndex(pair => pair.Key.Length == 0);
        if (plain < 0) return;
        string text = _entries[plain].Value;
        _entries.RemoveAt(plain);
        foreach (string language in new[] { "tr", "en" })
        {
            if (_entries.All(pair => pair.Key != language))
                _entries.Insert(Math.Min(plain, _entries.Count), new KeyValuePair<string, string>(language, text));
        }
    }

    /// <summary>
    /// Reads the native shape. Returns false for a wrong shape (not a string or an object of
    /// strings); entries longer than the limits are kept so validation can name them.
    /// </summary>
    public static bool TryRead(JsonNode? node, out ExtrasLocalizedText text)
    {
        text = new ExtrasLocalizedText();
        if (node is JsonValue value && value.TryGetValue<string>(out var plain))
        {
            if (plain.Length > 0) text._entries.Add(new KeyValuePair<string, string>(string.Empty, plain));
            return true;
        }
        if (node is not JsonObject obj) return false;
        foreach (var pair in obj)
        {
            if (pair.Value is not JsonValue entry || !entry.TryGetValue<string>(out var entryText)) return false;
            if (entryText.Length > 0)
                text._entries.Add(new KeyValuePair<string, string>(pair.Key.ToLowerInvariant(), entryText));
        }
        return true;
    }

    /// <summary>Null when empty, a string for plain text, else an object in entry order.</summary>
    public JsonNode? ToJson()
    {
        if (_entries.Count == 0) return null;
        if (_entries.Count == 1 && _entries[0].Key.Length == 0) return JsonValue.Create(_entries[0].Value);
        var obj = new JsonObject();
        foreach (var pair in _entries)
            obj[pair.Key.Length == 0 ? "tr" : pair.Key] = pair.Value;
        return obj;
    }

    public bool HasOversizedEntry =>
        _entries.Any(pair => !ExtrasRules.IsTextWithinLimit(pair.Value) || !ExtrasRules.IsLanguageTagWithinLimit(pair.Key));
}

/// <summary>Common shape of a catalog entry: id, title and the keys the editor does not know.</summary>
public abstract partial class ExtrasItem : ObservableObject
{
    private readonly Dictionary<string, ExtrasIssueCode> _loadIssues = new(StringComparer.Ordinal);

    internal ExtrasLocalizedText TitleText { get; set; } = new();

    /// <summary>Keys this editor has no field for; written back untouched.</summary>
    internal JsonObject Extra { get; } = new();

    [ObservableProperty]
    private string _id = string.Empty;

    public string TitleTr
    {
        get => TitleText.Get("tr");
        set { if (TitleText.Set("tr", value)) { ClearLoadIssue("title"); OnPropertyChanged(); OnPropertyChanged(nameof(TitleEn)); OnPropertyChanged(nameof(DisplayLabel)); } }
    }

    public string TitleEn
    {
        get => TitleText.Get("en");
        set { if (TitleText.Set("en", value)) { ClearLoadIssue("title"); OnPropertyChanged(); OnPropertyChanged(nameof(TitleTr)); OnPropertyChanged(nameof(DisplayLabel)); } }
    }

    partial void OnIdChanged(string value)
    {
        ClearLoadIssue("id");
        OnPropertyChanged(nameof(DisplayLabel));
    }

    /// <summary>List caption: the id, then the Turkish title when there is one.</summary>
    public string DisplayLabel => TitleTr.Length > 0 ? $"{Id}  -  {TitleTr}" : Id;

    /// <summary>Set by the catalog editor after each check: this item has a problem the engine would skip it for.</summary>
    [ObservableProperty]
    private bool _hasIssue;

    /// <summary>The editor's sentence(s) for <see cref="HasIssue"/>, one per line.</summary>
    [ObservableProperty]
    private string _issueText = string.Empty;

    /// <summary>Problems found while reading (a wrong JSON type); cleared when the author sets the field.</summary>
    internal IReadOnlyDictionary<string, ExtrasIssueCode> LoadIssues => _loadIssues;

    internal void AddLoadIssue(string field, ExtrasIssueCode code) => _loadIssues[field] = code;

    protected void ClearLoadIssue(string field) => _loadIssues.Remove(field);

    /// <summary>Kind name of this list (<c>cg</c>, <c>music</c>, <c>achievement</c>).</summary>
    public abstract string Kind { get; }

    internal abstract IEnumerable<string> KnownKeys { get; }

    internal abstract void ReadFields(JsonObject json);

    internal abstract void WriteFields(JsonObject json);

    internal abstract IEnumerable<ExtrasIssueCode> FieldIssues(string field);

    internal void ReadCommon(JsonObject json)
    {
        if (json["id"] is JsonValue idValue && idValue.TryGetValue<string>(out var id)) Id = id;
        else if (json.ContainsKey("id")) AddLoadIssue("id", ExtrasIssueCode.WrongType);
        if (json.TryGetPropertyValue("title", out var title))
        {
            if (ExtrasLocalizedText.TryRead(title, out var text)) TitleText = text;
            else AddLoadIssue("title", ExtrasIssueCode.WrongType);
        }
        foreach (var pair in json)
        {
            if (!KnownKeys.Contains(pair.Key))
                Extra[pair.Key] = pair.Value?.DeepClone();
        }
        // Setting Id above raised OnIdChanged, which cleared nothing yet; a non-string id stays flagged.
    }

    internal JsonObject ToJsonObject()
    {
        var json = new JsonObject { ["id"] = Id };
        if (TitleText.ToJson() is { } title) json["title"] = title;
        WriteFields(json);
        foreach (var pair in Extra)
        {
            if (!json.ContainsKey(pair.Key)) json[pair.Key] = pair.Value?.DeepClone();
        }
        return json;
    }
}

public sealed partial class ExtrasGalleryItem : ExtrasItem
{
    [ObservableProperty] private string _image = string.Empty;
    [ObservableProperty] private string _thumbnail = string.Empty;

    public override string Kind => ExtrasRules.KindCg;
    internal override IEnumerable<string> KnownKeys { get; } = new[] { "id", "title", "image", "thumbnail" };

    partial void OnImageChanged(string value) => ClearLoadIssue("image");
    partial void OnThumbnailChanged(string value) => ClearLoadIssue("thumbnail");

    internal override void ReadFields(JsonObject json)
    {
        ReadPath(json, "image", v => Image = v);
        ReadPath(json, "thumbnail", v => Thumbnail = v);
    }

    internal override void WriteFields(JsonObject json)
    {
        json["image"] = Image;
        if (Thumbnail.Length > 0) json["thumbnail"] = Thumbnail;
    }

    internal override IEnumerable<ExtrasIssueCode> FieldIssues(string field)
    {
        if (field == "image")
        {
            if (Image.Length == 0) yield return ExtrasIssueCode.MissingImage;
            else if (!ExtrasRules.IsSafeMediaPath(Image)) yield return ExtrasIssueCode.UnsafePath;
        }
        else if (field == "thumbnail" && Thumbnail.Length > 0 && !ExtrasRules.IsSafeMediaPath(Thumbnail))
        {
            yield return ExtrasIssueCode.UnsafePath;
        }
    }

    private void ReadPath(JsonObject json, string key, Action<string> assign)
    {
        if (!json.TryGetPropertyValue(key, out var node)) return;
        if (node is JsonValue value && value.TryGetValue<string>(out var text)) assign(text);
        else AddLoadIssue(key, ExtrasIssueCode.WrongType);
    }
}

public sealed partial class ExtrasMusicItem : ExtrasItem
{
    [ObservableProperty] private string _track = string.Empty;

    public override string Kind => ExtrasRules.KindMusic;
    internal override IEnumerable<string> KnownKeys { get; } = new[] { "id", "title", "track" };

    partial void OnTrackChanged(string value) => ClearLoadIssue("track");

    internal override void ReadFields(JsonObject json)
    {
        if (!json.TryGetPropertyValue("track", out var node)) return;
        if (node is JsonValue value && value.TryGetValue<string>(out var text)) Track = text;
        else AddLoadIssue("track", ExtrasIssueCode.WrongType);
    }

    internal override void WriteFields(JsonObject json) => json["track"] = Track;

    internal override IEnumerable<ExtrasIssueCode> FieldIssues(string field)
    {
        if (field != "track") yield break;
        if (Track.Length == 0) yield return ExtrasIssueCode.MissingTrack;
        else if (!ExtrasRules.IsSafeMediaPath(Track)) yield return ExtrasIssueCode.UnsafePath;
    }
}

public sealed partial class ExtrasAchievementItem : ExtrasItem
{
    [ObservableProperty] private bool _hidden;

    internal ExtrasLocalizedText DescriptionText { get; set; } = new();

    public override string Kind => ExtrasRules.KindAchievement;
    internal override IEnumerable<string> KnownKeys { get; } = new[] { "id", "title", "description", "hidden" };

    partial void OnHiddenChanged(bool value) => ClearLoadIssue("hidden");

    public string DescriptionTr
    {
        get => DescriptionText.Get("tr");
        set { if (DescriptionText.Set("tr", value)) { ClearLoadIssue("description"); OnPropertyChanged(); OnPropertyChanged(nameof(DescriptionEn)); } }
    }

    public string DescriptionEn
    {
        get => DescriptionText.Get("en");
        set { if (DescriptionText.Set("en", value)) { ClearLoadIssue("description"); OnPropertyChanged(); OnPropertyChanged(nameof(DescriptionTr)); } }
    }

    internal override void ReadFields(JsonObject json)
    {
        if (json.TryGetPropertyValue("description", out var description))
        {
            if (ExtrasLocalizedText.TryRead(description, out var text)) DescriptionText = text;
            else AddLoadIssue("description", ExtrasIssueCode.WrongType);
        }
        if (json.TryGetPropertyValue("hidden", out var hidden))
        {
            if (hidden is JsonValue value && value.TryGetValue<bool>(out var flag)) Hidden = flag;
            else AddLoadIssue("hidden", ExtrasIssueCode.WrongType);
        }
    }

    internal override void WriteFields(JsonObject json)
    {
        if (DescriptionText.ToJson() is { } description) json["description"] = description;
        if (Hidden) json["hidden"] = true;
    }

    internal override IEnumerable<ExtrasIssueCode> FieldIssues(string field)
    {
        if (field == "description" && DescriptionText.HasOversizedEntry) yield return ExtrasIssueCode.TextTooLong;
    }
}

/// <summary>Result of reading an <c>extras.json</c>: the model, or why the file cannot be edited.</summary>
public sealed record ExtrasLoadResult(ExtrasCatalogModel? Model, bool FileExists, string? Error);

/// <summary>
/// Editable form of a project's <c>Assets/json/extras.json</c> (format 1; D07-1). Reading keeps
/// everything the editor has no field for (unknown top-level keys, unknown item keys, language
/// entries other than tr/en), so an untouched open + save leaves the document the same in meaning.
/// Items with problems stay in the lists so the author can fix them; <see cref="Validate"/> names
/// the problems with the native engine's rules and saving is refused until there are none.
/// </summary>
public sealed class ExtrasCatalogModel : ObservableObject
{
    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private readonly JsonObject _unknownTopLevel = new();
    private readonly HashSet<string> _presentLists = new(StringComparer.Ordinal);

    public ObservableCollection<ExtrasGalleryItem> Gallery { get; } = new();
    public ObservableCollection<ExtrasMusicItem> Music { get; } = new();
    public ObservableCollection<ExtrasAchievementItem> Achievements { get; } = new();

    /// <summary>Entries the editor could not represent (a list item that is not an object); dropped on save.</summary>
    public int DroppedEntryCount { get; private set; }

    public bool IsEmpty => Gallery.Count == 0 && Music.Count == 0 && Achievements.Count == 0;

    public IEnumerable<ExtrasItem> AllItems => Gallery.Cast<ExtrasItem>().Concat(Music).Concat(Achievements);

    public IReadOnlyList<ExtrasItem> ItemsOf(string kind) => kind switch
    {
        ExtrasRules.KindCg => Gallery.ToArray(),
        ExtrasRules.KindMusic => Music.ToArray(),
        ExtrasRules.KindAchievement => Achievements.ToArray(),
        _ => Array.Empty<ExtrasItem>(),
    };

    /// <summary>Parses a document. A file the engine would ignore as a whole yields an error, never a model.</summary>
    public static ExtrasLoadResult Parse(string json)
    {
        if (System.Text.Encoding.UTF8.GetByteCount(json) > ExtrasRules.MaxFileBytes)
            return new ExtrasLoadResult(null, true, "FileTooLarge");
        JsonNode? root;
        try
        {
            root = JsonNode.Parse(json);
        }
        catch (Exception error) when (error is JsonException or ArgumentException)
        {
            // ArgumentException: a repeated key (the engine's parser keeps the last one; the
            // editor refuses to guess which one the author meant).
            return new ExtrasLoadResult(null, true, "NotJson");
        }
        if (root is not JsonObject obj) return new ExtrasLoadResult(null, true, "NotAnObject");
        if (obj["format"] is not JsonValue format || !format.TryGetValue<int>(out int formatNumber)
            || formatNumber != ExtrasRules.Format || format.ToJsonString().Contains('.'))
            return new ExtrasLoadResult(null, true, "NoFormat1");

        var model = new ExtrasCatalogModel();
        foreach (var pair in obj)
        {
            switch (pair.Key)
            {
                case "format":
                    break;
                case "gallery":
                    model.ReadList(pair.Key, pair.Value, model.Gallery, () => new ExtrasGalleryItem());
                    break;
                case "music":
                    model.ReadList(pair.Key, pair.Value, model.Music, () => new ExtrasMusicItem());
                    break;
                case "achievements":
                    model.ReadList(pair.Key, pair.Value, model.Achievements, () => new ExtrasAchievementItem());
                    break;
                default:
                    model._unknownTopLevel[pair.Key] = pair.Value?.DeepClone();
                    break;
            }
        }
        return new ExtrasLoadResult(model, true, null);
    }

    private void ReadList<T>(string key, JsonNode? node, ObservableCollection<T> target, Func<T> create)
        where T : ExtrasItem
    {
        // A list that is not an array is ignored by the engine; nothing to edit, nothing kept.
        if (node is not JsonArray array)
        {
            DroppedEntryCount++;
            return;
        }
        _presentLists.Add(key);
        foreach (var entry in array)
        {
            if (entry is not JsonObject itemJson)
            {
                DroppedEntryCount++;
                continue;
            }
            var item = create();
            item.ReadCommon(itemJson);
            item.ReadFields(itemJson);
            target.Add(item);
        }
    }

    /// <summary>The catalog as document text (2-space indent, non-ASCII kept literal).</summary>
    public string ToJson()
    {
        var root = new JsonObject { ["format"] = ExtrasRules.Format };
        WriteList(root, "gallery", Gallery);
        WriteList(root, "music", Music);
        WriteList(root, "achievements", Achievements);
        foreach (var pair in _unknownTopLevel)
            root[pair.Key] = pair.Value?.DeepClone();
        return root.ToJsonString(WriteOptions);
    }

    private void WriteList<T>(JsonObject root, string key, ObservableCollection<T> items) where T : ExtrasItem
    {
        if (items.Count == 0 && !_presentLists.Contains(key)) return;
        var array = new JsonArray();
        foreach (var item in items) array.Add(item.ToJsonObject());
        root[key] = array;
    }

    /// <summary>
    /// Every problem the engine would skip an item for (or ignore the file for), in list order.
    /// An empty result means the player loads the whole catalog.
    /// </summary>
    public IReadOnlyList<ExtrasIssue> Validate()
    {
        var issues = new List<ExtrasIssue>();
        ValidateList(ExtrasRules.KindCg, Gallery, new[] { "image", "thumbnail" }, issues);
        ValidateList(ExtrasRules.KindMusic, Music, new[] { "track" }, issues);
        ValidateList(ExtrasRules.KindAchievement, Achievements, new[] { "description" }, issues);
        if (issues.Count == 0 && System.Text.Encoding.UTF8.GetByteCount(ToJson()) > ExtrasRules.MaxFileBytes)
            issues.Add(new ExtrasIssue(null, -1, string.Empty, ExtrasIssueCode.FileTooLarge));
        return issues;
    }

    private static void ValidateList<T>(string kind, IList<T> items, string[] fields, List<ExtrasIssue> issues)
        where T : ExtrasItem
    {
        if (items.Count > ExtrasRules.MaxItemsPerList)
            issues.Add(new ExtrasIssue(kind, -1, string.Empty, ExtrasIssueCode.TooManyItems));
        var seen = new HashSet<string>(StringComparer.Ordinal);
        for (int i = 0; i < items.Count; i++)
        {
            var item = items[i];
            foreach (var load in item.LoadIssues)
                issues.Add(new ExtrasIssue(kind, i, load.Key, load.Value));
            if (!ExtrasRules.IsValidId(item.Id))
                issues.Add(new ExtrasIssue(kind, i, "id", ExtrasIssueCode.InvalidId));
            else if (!seen.Add(item.Id))
                issues.Add(new ExtrasIssue(kind, i, "id", ExtrasIssueCode.DuplicateId));
            if (item.TitleText.HasOversizedEntry)
                issues.Add(new ExtrasIssue(kind, i, "title", ExtrasIssueCode.TextTooLong));
            foreach (string field in fields)
            {
                foreach (var code in item.FieldIssues(field))
                    issues.Add(new ExtrasIssue(kind, i, field, code));
            }
        }
    }

    /// <summary>The ids a node component of <paramref name="kind"/> may name.</summary>
    public IReadOnlyList<string> IdsOf(string kind) => ItemsOf(kind).Select(item => item.Id).ToArray();

    public bool Contains(string kind, string id) => ItemsOf(kind).Any(item => item.Id == id);
}
