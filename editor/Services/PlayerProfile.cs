using System;
using System.Collections.Generic;
using System.Linq;

namespace RowlEngine.Editor.Services;

/// <summary>Player skip behaviour for the Faz 2 player loop.</summary>
public enum PlayerSkipMode
{
    /// <summary>No skipping; every line plays in full.</summary>
    Off = 0,
    /// <summary>Skip only lines already recorded in <see cref="PlayerProfile.ReadContentIds"/>.</summary>
    ReadOnly = 1,
    /// <summary>Skip everything skippable, read or not.</summary>
    All = 2,
}

/// <summary>
/// Faz 2 Dilim 1 — save-slot independent, versioned player profile skeleton.
/// Carries read-tracking (<see cref="ReadContentIds"/>), language preference,
/// text/audio settings and skip/auto flags. Persisted atomically under the
/// PlatformHost profile directory via <see cref="PlayerProfileStore"/>.
/// This type is additive: <see cref="PlayerSettingsProfile"/> keeps its
/// existing shape and behaviour untouched.
/// </summary>
public sealed class PlayerProfile
{
    /// <summary>Current on-disk profile schema version.</summary>
    public const int CurrentVersion = 1;

    /// <summary>Supported UI locale codes (Faz 3 grows this list).</summary>
    public static readonly IReadOnlyList<string> SupportedLanguages = new[] { "en", "tr" };

    /// <summary>Faz 3 Dilim 5 — offered text-size steps (1.0x, 1.25x, 1.5x).</summary>
    public static readonly IReadOnlyList<float> AllowedTextScales = new[] { 1f, 1.25f, 1.5f };

    /// <summary>Default locale for fresh profiles.</summary>
    public const string DefaultLanguage = "en";

    public int Version { get; set; } = CurrentVersion;

    /// <summary>Persistent content ids the player has already read.</summary>
    public HashSet<string> ReadContentIds { get; set; } = new(StringComparer.OrdinalIgnoreCase);

    public string Language { get; set; } = DefaultLanguage;

    public float MasterVolume { get; set; } = 1;
    public float BgmVolume { get; set; } = 1;
    public float VoiceVolume { get; set; } = 1;
    public float SfxVolume { get; set; } = 1;
    public float TextSpeedMultiplier { get; set; } = 1;
    public float AutoAdvanceDelay { get; set; } = 2;

    /// <summary>
    /// Faz 3 Dilim 5 — accessibility. Dialogue/typewriter/HUD text size
    /// multiplier. Sanitized onto <see cref="AllowedTextScales"/>.
    /// </summary>
    public float TextScale { get; set; } = 1;

    /// <summary>Faz 3 Dilim 5 — dark glyph outline pass for readability.</summary>
    public bool HighContrast { get; set; } = false;

    /// <summary>Faz 3 Dilim 5 — disables camera shake and screen flash.</summary>
    public bool ReducedMotion { get; set; } = false;

    public PlayerSkipMode SkipMode { get; set; } = PlayerSkipMode.ReadOnly;
    public bool AutoEnabled { get; set; } = false;

    /// <summary>Marks one content id as read. Returns false for empty/invalid ids.</summary>
    public bool MarkRead(string? contentId)
    {
        string? normalized = ContentIdService.Normalize(contentId);
        if (normalized is null)
            return false;
        return ReadContentIds.Add(normalized);
    }

    /// <summary>Reports whether a content id was already read.</summary>
    public bool IsRead(string? contentId)
    {
        string? normalized = ContentIdService.Normalize(contentId);
        return normalized is not null && ReadContentIds.Contains(normalized);
    }

    /// <summary>
    /// Clamps every field into its valid range and drops malformed entries.
    /// Returns the number of dropped read ids so callers can log the repair.
    /// </summary>
    public int Sanitized()
    {
        if (Version <= 0)
            Version = CurrentVersion;
        Language = NormalizeLanguage(Language);
        MasterVolume = Math.Clamp(MasterVolume, 0, 1);
        BgmVolume = Math.Clamp(BgmVolume, 0, 1);
        VoiceVolume = Math.Clamp(VoiceVolume, 0, 1);
        SfxVolume = Math.Clamp(SfxVolume, 0, 1);
        TextSpeedMultiplier = Math.Clamp(TextSpeedMultiplier, 0.25f, 4);
        AutoAdvanceDelay = Math.Clamp(AutoAdvanceDelay, 0, 60);
        TextScale = SnapTextScale(TextScale);
        if (!Enum.IsDefined(SkipMode))
            SkipMode = PlayerSkipMode.ReadOnly;
        int before = ReadContentIds.Count;
        ReadContentIds = new HashSet<string>(
            ReadContentIds
                .Select(ContentIdService.Normalize)
                .Where(id => id is not null)!,
            StringComparer.OrdinalIgnoreCase);
        return before - ReadContentIds.Count;
    }

    /// <summary>Snaps any value onto the nearest allowed text-scale step.</summary>
    public static float SnapTextScale(float value)
    {
        if (!float.IsFinite(value))
            return 1f;
        float best = AllowedTextScales[0];
        float bestDistance = Math.Abs(value - best);
        foreach (float step in AllowedTextScales)
        {
            float distance = Math.Abs(value - step);
            if (distance < bestDistance)
            {
                best = step;
                bestDistance = distance;
            }
        }
        return best;
    }

    internal static string NormalizeLanguage(string? language)
    {
        string candidate = (language ?? string.Empty).Trim().ToLowerInvariant();
        // Accept "tr-TR"/"en-US" style tags by their primary subtag.
        int dash = candidate.IndexOfAny(new[] { '-', '_' });
        if (dash > 0)
            candidate = candidate[..dash];
        return SupportedLanguages.Contains(candidate, StringComparer.Ordinal) ? candidate : DefaultLanguage;
    }
}
