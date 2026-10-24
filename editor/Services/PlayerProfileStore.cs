using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services;

/// <summary>How a <see cref="PlayerProfile"/> load resolved.</summary>
public enum PlayerProfileLoadStatus
{
    /// <summary>Profile file parsed cleanly (sanitized).</summary>
    Loaded,
    /// <summary>No profile file yet; fresh defaults (first run, not an error).</summary>
    MissingDefaults,
    /// <summary>Unreadable file; defaults with a diagnostic (never a half-state).</summary>
    CorruptDefaults,
    /// <summary>Newer schema version; defaults with a diagnostic (never a half-state).</summary>
    UnsupportedVersionDefaults,
    /// <summary>
    /// D01c-2 — the file's <c>game_id</c> differs from the expected game;
    /// defaults with a diagnostic and the file must stay untouched (read-only).
    /// </summary>
    WrongGameDefaults,
}

/// <summary>
/// Faz 2 Dilim 1 — atomic, versioned <see cref="PlayerProfile"/> persistence.
/// <list type="bullet">
/// <item>D01c-2: with a native store mounted the native engine is the only
/// writer and the editor reads through <see cref="Parse"/>; this class then
/// serves as the per-game managed fallback (see <see cref="PlayerProfileSession"/>).
/// It keeps <c>game_id</c>, <c>unlocks</c> and unknown keys like the native store.</item>
/// <item>Location: <c>profiles/player-profile.json</c> below the PlatformHost
/// user-data root (the same <c>rowl-engine/profiles</c> layout the native
/// <c>Rowl::Platform::resolveUserDataDirectories</c> produces). Hosts holding
/// a native handle should pass the <c>RowlEngine_GetProfileDirectoryUtf8</c>
/// result as <paramref name="profileDirectory"/>; otherwise
/// <see cref="ResolveDefaultProfileDirectory"/> mirrors the native layout in
/// managed code.</item>
/// <item>Writes are atomic (temp file + rename) via
/// <see cref="ProjectFileSystem.WriteAllTextAtomically"/>; readers never
/// observe a half-written profile.</item>
/// <item>Loads never throw and never swallow failures silently: every fallback
/// carries a <c>Diagnostic</c> the caller can log or toast.</item>
/// </list>
/// </summary>
public static class PlayerProfileStore
{
    /// <summary>Profile file name inside the profile directory.</summary>
    public const string FileName = "player-profile.json";

    public sealed record LoadResult(
        PlayerProfile Profile,
        PlayerProfileLoadStatus Status,
        string? Diagnostic);

    /// <summary>Known top-level keys (mirrors the native store's kKnownKeys).</summary>
    private static readonly HashSet<string> KnownKeys = new(StringComparer.Ordinal)
    {
        "version", "game_id", "language", "read_content_ids", "master_volume", "bgm_volume",
        "voice_volume", "sfx_volume", "ambience_volume", "ui_volume", "text_speed_multiplier",
        "auto_advance_delay", "text_scale", "high_contrast", "reduced_motion", "skip_mode",
        "auto_enabled", "mixer_fade_curve", "sfx_pool_depth", "unlocks",
    };

    /// <summary>
    /// Loads the profile, falling back to sanitized defaults with a diagnostic.
    /// <paramref name="expectedGameId"/> (per-game directories) makes a file
    /// stamped with another game's id resolve to
    /// <see cref="PlayerProfileLoadStatus.WrongGameDefaults"/>.
    /// </summary>
    public static LoadResult Load(string profileDirectory, string? expectedGameId = null)
    {
        string path = Path.Combine(profileDirectory, FileName);
        if (!File.Exists(path))
        {
            return new LoadResult(
                new PlayerProfile(),
                PlayerProfileLoadStatus.MissingDefaults,
                $"No player profile at '{path}'; using defaults.");
        }
        string raw;
        try
        {
            raw = File.ReadAllText(path);
        }
        catch (Exception readFailure) when (readFailure is IOException or UnauthorizedAccessException)
        {
            return new LoadResult(
                new PlayerProfile(),
                PlayerProfileLoadStatus.CorruptDefaults,
                $"Player profile '{path}' could not be read ({readFailure.GetType().Name}: {readFailure.Message}); using defaults.");
        }
        return Parse(raw, path, expectedGameId);
    }

    /// <summary>
    /// Parses one profile document (a file's text or the native
    /// <c>RowlEngine_ProfileGetJson</c> output). <paramref name="source"/> only
    /// labels diagnostics. Never throws.
    /// </summary>
    public static LoadResult Parse(string raw, string source, string? expectedGameId = null)
    {
        string path = source;
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(raw);
        }
        catch (JsonException parseFailure)
        {
            return new LoadResult(
                new PlayerProfile(),
                PlayerProfileLoadStatus.CorruptDefaults,
                $"Player profile '{path}' is not valid JSON ({parseFailure.Message}); using defaults.");
        }
        using (document)
        {
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                return new LoadResult(
                    new PlayerProfile(),
                    PlayerProfileLoadStatus.CorruptDefaults,
                    $"Player profile '{path}' does not contain a JSON object; using defaults.");
            }
            var root = document.RootElement;
            int version = PlayerProfile.CurrentVersion;
            if (root.TryGetProperty("version", out var versionElement))
            {
                if (versionElement.ValueKind != JsonValueKind.Number || !versionElement.TryGetInt32(out version))
                {
                    return new LoadResult(
                        new PlayerProfile(),
                        PlayerProfileLoadStatus.CorruptDefaults,
                        $"Player profile '{path}' carries a non-numeric version; using defaults.");
                }
            }
            if (version > PlayerProfile.CurrentVersion)
            {
                return new LoadResult(
                    new PlayerProfile(),
                    PlayerProfileLoadStatus.UnsupportedVersionDefaults,
                    $"Player profile '{path}' targets schema v{version} (reader v{PlayerProfile.CurrentVersion}); using defaults.");
            }
            var profile = new PlayerProfile { Version = Math.Max(version, 1) };
            var notes = new List<string>();
            if (root.TryGetProperty("game_id", out var gameElement))
            {
                if (gameElement.ValueKind == JsonValueKind.String)
                {
                    string fileGame = gameElement.GetString() ?? string.Empty;
                    if (!string.IsNullOrEmpty(expectedGameId) && fileGame.Length > 0
                        && !string.Equals(fileGame, expectedGameId, StringComparison.Ordinal))
                    {
                        return new LoadResult(
                            new PlayerProfile(),
                            PlayerProfileLoadStatus.WrongGameDefaults,
                            $"Player profile '{path}' belongs to game '{fileGame}', not '{expectedGameId}'; using defaults and leaving the file untouched.");
                    }
                    profile.GameId = fileGame;
                }
                else
                {
                    notes.Add("Non-string game_id ignored.");
                }
            }
            if (root.TryGetProperty("unlocks", out var unlocksElement))
            {
                // D07-1: the native store writes {"cg": [...], "music": [...], "achievements": [...]};
                // the managed side stays opaque and keeps an array (legacy / empty) or an object verbatim.
                if (unlocksElement.ValueKind == JsonValueKind.Array || unlocksElement.ValueKind == JsonValueKind.Object)
                    profile.Unlocks = JsonNode.Parse(unlocksElement.GetRawText());
                else
                    notes.Add("Non-array, non-object unlocks reset to an empty array.");
            }
            foreach (var property in root.EnumerateObject())
            {
                if (!KnownKeys.Contains(property.Name))
                    profile.ExtraKeys[property.Name] = JsonNode.Parse(property.Value.GetRawText());
            }
            profile.Language = ReadString(root, "language", PlayerProfile.DefaultLanguage);
            // D05-1: an invalid tag resets to "not chosen" (Sanitized below); say so.
            PlayerProfile.NormalizeLanguage(profile.Language, out bool languageValid);
            if (!languageValid)
                notes.Add($"Invalid language '{(profile.Language.Length > 32 ? profile.Language[..32] : profile.Language)}' reset to \"\" (not chosen).");
            profile.MasterVolume = ReadSingle(root, "master_volume", 1);
            profile.BgmVolume = ReadSingle(root, "bgm_volume", 1);
            profile.VoiceVolume = ReadSingle(root, "voice_volume", 1);
            profile.SfxVolume = ReadSingle(root, "sfx_volume", 1);
            profile.AmbienceVolume = ReadSingle(root, "ambience_volume", 1);
            profile.UiVolume = ReadSingle(root, "ui_volume", 1);
            profile.TextSpeedMultiplier = ReadSingle(root, "text_speed_multiplier", 1);
            profile.AutoAdvanceDelay = ReadSingle(root, "auto_advance_delay", 2);
            profile.SkipMode = ReadSkipMode(root, notes);
            profile.AutoEnabled = ReadBool(root, "auto_enabled", false);
            profile.MixerFadeCurve = ReadString(root, "mixer_fade_curve", "Linear");
            profile.SfxPoolDepth = ReadInt(root, "sfx_pool_depth", 8);
            profile.TextScale = ReadSingle(root, "text_scale", 1);
            profile.HighContrast = ReadBool(root, "high_contrast", false);
            profile.ReducedMotion = ReadBool(root, "reduced_motion", false);
            int dropped = ReadContentIds(root, profile, notes);
            int sanitized = profile.Sanitized();
            if (sanitized > 0)
                notes.Add($"{sanitized} malformed read id(s) dropped by range check.");
            string? diagnostic = notes.Count == 0 ? null : string.Join(" ", notes);
            return new LoadResult(profile, PlayerProfileLoadStatus.Loaded, diagnostic);
        }
    }

    /// <summary>
    /// Saves the profile atomically. Returns null on success or a structured
    /// error message (the previous file is left untouched on failure).
    /// </summary>
    public static string? Save(string profileDirectory, PlayerProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        try
        {
            profile.Version = PlayerProfile.CurrentVersion;
            profile.Sanitized();
            string path = Path.Combine(profileDirectory, FileName);
            string json = ToJson(profile);
            ProjectFileSystem.WriteAllTextAtomically(path, json);
            return null;
        }
        catch (Exception saveFailure) when (saveFailure is IOException or UnauthorizedAccessException)
        {
            return $"Player profile could not be saved to '{profileDirectory}' ({saveFailure.GetType().Name}: {saveFailure.Message}).";
        }
    }

    /// <summary>
    /// Serializes the full document the way the native store does: known keys
    /// (plus <c>game_id</c> when set), the opaque <c>unlocks</c> array and every
    /// unknown top-level key the profile carried.
    /// </summary>
    public static string ToJson(PlayerProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        var payload = new Dictionary<string, object?>
        {
            ["version"] = profile.Version,
        };
        if (!string.IsNullOrEmpty(profile.GameId))
            payload["game_id"] = profile.GameId;
        payload["language"] = profile.Language;
        payload["read_content_ids"] = profile.ReadContentIds
            .OrderBy(id => id, StringComparer.Ordinal)
            .ToArray();
        AddPreferences(payload, profile, includeLanguage: false);
        payload["unlocks"] = profile.Unlocks?.DeepClone() ?? new JsonArray();
        foreach (var extra in profile.ExtraKeys)
        {
            if (!KnownKeys.Contains(extra.Key))
                payload[extra.Key] = extra.Value?.DeepClone();
        }
        return JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true });
    }

    /// <summary>
    /// Preference keys only (no read set, game id, unlocks or extras) - the
    /// document accepted by <c>RowlEngine_ProfileSetPreferencesJson</c>.
    /// </summary>
    public static string PreferencesToJson(PlayerProfile profile)
    {
        ArgumentNullException.ThrowIfNull(profile);
        var payload = new Dictionary<string, object?>();
        AddPreferences(payload, profile, includeLanguage: true);
        return JsonSerializer.Serialize(payload);
    }

    private static void AddPreferences(Dictionary<string, object?> payload, PlayerProfile profile, bool includeLanguage)
    {
        if (includeLanguage)
            payload["language"] = profile.Language;
        payload["master_volume"] = profile.MasterVolume;
        payload["bgm_volume"] = profile.BgmVolume;
        payload["voice_volume"] = profile.VoiceVolume;
        payload["sfx_volume"] = profile.SfxVolume;
        payload["ambience_volume"] = profile.AmbienceVolume;
        payload["ui_volume"] = profile.UiVolume;
        payload["text_speed_multiplier"] = profile.TextSpeedMultiplier;
        payload["auto_advance_delay"] = profile.AutoAdvanceDelay;
        payload["text_scale"] = profile.TextScale;
        payload["high_contrast"] = profile.HighContrast;
        payload["reduced_motion"] = profile.ReducedMotion;
        payload["skip_mode"] = SkipModeToString(profile.SkipMode);
        payload["auto_enabled"] = profile.AutoEnabled;
        payload["mixer_fade_curve"] = profile.MixerFadeCurve;
        payload["sfx_pool_depth"] = profile.SfxPoolDepth;
    }

    /// <summary>
    /// Managed mirror of the native PlatformHost profile directory
    /// (<c>rowl-engine/profiles</c> below the per-user data root).
    /// </summary>
    public static string ResolveDefaultProfileDirectory()
    {
        string root;
        if (OperatingSystem.IsWindows())
        {
            root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (string.IsNullOrWhiteSpace(root))
                root = Path.GetTempPath();
            return Path.Combine(root, "rowl-engine", "profiles");
        }
        string? xdg = Environment.GetEnvironmentVariable("XDG_DATA_HOME");
        if (!string.IsNullOrWhiteSpace(xdg) && Path.IsPathFullyQualified(xdg))
            root = xdg;
        else
            root = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                ".local", "share");
        return Path.Combine(root, "rowl-engine", "profiles");
    }

    private static string ReadString(JsonElement root, string key, string fallback) =>
        root.TryGetProperty(key, out var element) && element.ValueKind == JsonValueKind.String
            ? element.GetString() ?? fallback
            : fallback;

    private static float ReadSingle(JsonElement root, string key, float fallback) =>
        root.TryGetProperty(key, out var element) && element.ValueKind == JsonValueKind.Number && element.TryGetSingle(out var value)
            ? value
            : fallback;

    private static bool ReadBool(JsonElement root, string key, bool fallback) =>
        root.TryGetProperty(key, out var element) &&
        (element.ValueKind == JsonValueKind.True || element.ValueKind == JsonValueKind.False)
            ? element.GetBoolean()
            : fallback;

    private static int ReadInt(JsonElement root, string key, int fallback)
    {
        if (!root.TryGetProperty(key, out var element))
            return fallback;
        if (element.ValueKind == JsonValueKind.Number)
        {
            if (element.TryGetInt32(out int direct))
                return direct;
            if (element.TryGetDouble(out double real) && double.IsFinite(real))
                return (int)real;
        }
        return fallback;
    }

    private static PlayerSkipMode ReadSkipMode(JsonElement root, List<string> notes)
    {
        if (!root.TryGetProperty("skip_mode", out var element))
            return PlayerSkipMode.ReadOnly;
        if (element.ValueKind == JsonValueKind.String)
        {
            string? text = element.GetString()?.Trim().ToLowerInvariant();
            return text switch
            {
                "off" or "closed" => PlayerSkipMode.Off,
                "all" => PlayerSkipMode.All,
                "read_only" or "readonly" or "read-only" => PlayerSkipMode.ReadOnly,
                _ => PlayerSkipMode.ReadOnly,
            };
        }
        if (element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out int numeric)
            && Enum.IsDefined(typeof(PlayerSkipMode), numeric))
            return (PlayerSkipMode)numeric;
        notes.Add("Unrecognized skip_mode repaired to read_only.");
        return PlayerSkipMode.ReadOnly;
    }

    private static int ReadContentIds(JsonElement root, PlayerProfile profile, List<string> notes)
    {
        if (!root.TryGetProperty("read_content_ids", out var element))
            return 0;
        if (element.ValueKind != JsonValueKind.Array)
        {
            notes.Add("Non-array read_content_ids ignored.");
            return 0;
        }
        int dropped = 0;
        foreach (var item in element.EnumerateArray())
        {
            if (item.ValueKind != JsonValueKind.String)
            {
                dropped++;
                continue;
            }
            if (!profile.MarkRead(item.GetString()))
                dropped++;
        }
        if (dropped > 0)
            notes.Add($"{dropped} malformed read id(s) ignored.");
        return dropped;
    }

    internal static string SkipModeToString(PlayerSkipMode mode) => mode switch
    {
        PlayerSkipMode.Off => "off",
        PlayerSkipMode.All => "all",
        _ => "read_only",
    };
}
