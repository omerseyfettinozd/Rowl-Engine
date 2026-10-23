using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;

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

    /// <summary>
    /// Faz 5 Dilim 2 — mixer masası: ambience + arayüz bus hacimleri.
    /// Native yazımı <see cref="AudioMixerService"/> delegeleri üzerinden
    /// akar; sanitize [0,1] clamp + non-finite ignore uygular.
    /// </summary>
    public float AmbienceVolume { get; set; } = 1;
    public float UiVolume { get; set; } = 1;

    /// <summary>
    /// Faz 5 Dilim 2 fix turu 1 — global mixer config (tek kaynak):
    /// fade eğrisi adı ("Linear"/"EqualPower", fail-closed Linear) ve
    /// SFX havuz derinliği ([1,16], varsayılan 8). Prod playback
    /// başında <c>PlayerViewModel.ApplyVolumes</c> üzerinden native'e
    /// uygulanır.
    /// </summary>
    public string MixerFadeCurve { get; set; } = "Linear";
    public int SfxPoolDepth { get; set; } = 8;
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

    /// <summary>
    /// D01c-2 — id of the game this profile belongs to (the native store stamps
    /// it in per-game profile directories). Round-tripped verbatim; empty when
    /// the file carried none.
    /// </summary>
    public string GameId { get; set; } = string.Empty;

    /// <summary>
    /// D01c-2 — reserved, opaque <c>unlocks</c> array (native owns the meaning).
    /// Preserved verbatim on rewrite; null means "none seen" (written as <c>[]</c>).
    /// </summary>
    public JsonNode? Unlocks { get; set; }

    /// <summary>
    /// D01c-2 — unknown top-level keys of a profile written by a newer build
    /// or by the native store; kept and written back so a managed rewrite
    /// never drops data.
    /// </summary>
    public Dictionary<string, JsonNode?> ExtraKeys { get; set; } = new(StringComparer.Ordinal);

    /// <summary>
    /// Copies the preference fields (everything except the read set, game id,
    /// unlocks and unknown keys) from <paramref name="other"/>.
    /// </summary>
    public void CopyPreferencesFrom(PlayerProfile other)
    {
        ArgumentNullException.ThrowIfNull(other);
        Language = other.Language;
        MasterVolume = other.MasterVolume;
        BgmVolume = other.BgmVolume;
        VoiceVolume = other.VoiceVolume;
        SfxVolume = other.SfxVolume;
        AmbienceVolume = other.AmbienceVolume;
        UiVolume = other.UiVolume;
        MixerFadeCurve = other.MixerFadeCurve;
        SfxPoolDepth = other.SfxPoolDepth;
        TextSpeedMultiplier = other.TextSpeedMultiplier;
        AutoAdvanceDelay = other.AutoAdvanceDelay;
        TextScale = other.TextScale;
        HighContrast = other.HighContrast;
        ReducedMotion = other.ReducedMotion;
        SkipMode = other.SkipMode;
        AutoEnabled = other.AutoEnabled;
    }

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
        AmbienceVolume = AudioMixerService.AcceptBedVolume(1, AmbienceVolume);
        UiVolume = AudioMixerService.AcceptBedVolume(1, UiVolume);
        MixerFadeCurve = AudioMixerService.FadeCurveName((int)AudioMixerService.ParseFadeCurve(MixerFadeCurve));
        SfxPoolDepth = AudioMixerService.ClampPoolDepth(SfxPoolDepth);
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
