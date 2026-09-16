using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

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
}

/// <summary>
/// Faz 2 Dilim 1 — atomic, versioned <see cref="PlayerProfile"/> persistence.
/// <list type="bullet">
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

    /// <summary>Loads the profile, falling back to sanitized defaults with a diagnostic.</summary>
    public static LoadResult Load(string profileDirectory)
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
            profile.Language = ReadString(root, "language", PlayerProfile.DefaultLanguage);
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
            var payload = new Dictionary<string, object?>
            {
                ["version"] = profile.Version,
                ["language"] = profile.Language,
                ["read_content_ids"] = profile.ReadContentIds
                    .OrderBy(id => id, StringComparer.Ordinal)
                    .ToArray(),
                ["master_volume"] = profile.MasterVolume,
                ["bgm_volume"] = profile.BgmVolume,
                ["voice_volume"] = profile.VoiceVolume,
                ["sfx_volume"] = profile.SfxVolume,
                ["ambience_volume"] = profile.AmbienceVolume,
                ["ui_volume"] = profile.UiVolume,
                ["text_speed_multiplier"] = profile.TextSpeedMultiplier,
                ["auto_advance_delay"] = profile.AutoAdvanceDelay,
                ["text_scale"] = profile.TextScale,
                ["high_contrast"] = profile.HighContrast,
                ["reduced_motion"] = profile.ReducedMotion,
                ["skip_mode"] = SkipModeToString(profile.SkipMode),
                ["auto_enabled"] = profile.AutoEnabled,
                ["mixer_fade_curve"] = profile.MixerFadeCurve,
                ["sfx_pool_depth"] = profile.SfxPoolDepth,
            };
            string json = JsonSerializer.Serialize(payload, new JsonSerializerOptions { WriteIndented = true });
            ProjectFileSystem.WriteAllTextAtomically(path, json);
            return null;
        }
        catch (Exception saveFailure) when (saveFailure is IOException or UnauthorizedAccessException)
        {
            return $"Player profile could not be saved to '{profileDirectory}' ({saveFailure.GetType().Name}: {saveFailure.Message}).";
        }
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
