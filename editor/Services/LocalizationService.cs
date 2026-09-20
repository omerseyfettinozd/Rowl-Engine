using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using RowlEngine.Editor.Native;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Parsed <c>project.rowlproj</c> locale declaration.
/// </summary>
public sealed record ManifestLocales(
    string DefaultLocale,
    IReadOnlyList<string> SupportedLocales);

/// <summary>
/// Faz 3 Dilim 1 — managed side of the runtime localization contract.
///
/// <list type="bullet">
/// <item>Parses the manifest locale declaration (<c>default_locale</c> /
/// <c>supported_locales</c>, camelCase spellings accepted; missing keys
/// fall back to <c>"en"</c> so legacy projects load untouched).</item>
/// <item>Validates <c>Assets/locales/&lt;locale&gt;.json</c> catalog
/// documents against the <c>content_id → speaker/text/alt_text</c>
/// contract without claiming resolution (resolution lives in the native
/// <c>LocalizationManager</c>: active → default → original text).</item>
/// <item>Bridges <see cref="PlayerProfile.Language"/> with the native
/// locale selection through caller-supplied handles, so the logic is
/// fully unit-testable without native code. Production passes the
/// offscreen worker handle (see <c>EngineHost.Handle</c>); this type
/// never grows <c>EngineHost</c> or <c>MainWindowViewModel</c>.</item>
/// </list>
/// </summary>
public sealed class LocalizationService
{
    /// <summary>Mirrors ROWL_ENGINE_CAPABILITY_LOCALIZATION (1 &lt;&lt; 7).</summary>
    public const ulong NativeCapabilityLocalization = 128UL;

    /// <summary>Canonical default when a manifest predates the contract.</summary>
    public const string FallbackDefaultLocale = "en";

    public PlayerProfile Profile { get; }

    public LocalizationService(PlayerProfile profile)
    {
        Profile = profile ?? throw new ArgumentNullException(nameof(profile));
    }

    /// <summary>Loads (or defaults) the profile for a directory.</summary>
    public static LocalizationService Load(string profileDirectory)
    {
        var loaded = PlayerProfileStore.Load(profileDirectory);
        return new LocalizationService(loaded.Profile);
    }

    /// <summary>
    /// Normalizes a locale tag to its primary subtag (<c>"tr-TR"</c> →
    /// <c>"tr"</c>). Returns null for blank or malformed tags.
    /// </summary>
    public static string? NormalizeLocale(string? locale)
    {
        if (string.IsNullOrWhiteSpace(locale))
            return null;
        string candidate = locale.Trim().ToLowerInvariant();
        int dash = candidate.IndexOfAny(new[] { '-', '_' });
        if (dash > 0)
            candidate = candidate[..dash];
        if (candidate.Length == 0 || candidate.Length > 32 ||
            !candidate.All(char.IsLetterOrDigit))
            return null;
        return candidate;
    }

    /// <summary>
    /// Parses a manifest JSON document tolerantly: both snake_case and
    /// camelCase keys are accepted, and unusable input yields the
    /// <c>"en"</c> fallback declaration instead of throwing.
    /// </summary>
    public static ManifestLocales ParseManifestLocales(string manifestJson)
    {
        string defaultLocale = FallbackDefaultLocale;
        var supported = new List<string>();
        try
        {
            using var document = JsonDocument.Parse(manifestJson);
            if (document.RootElement.ValueKind != JsonValueKind.Object)
                return Fallback();
            var root = document.RootElement;
            if (TryGetProperty(root, "default_locale", "defaultLocale",
                    out JsonElement defaultElement) &&
                defaultElement.ValueKind == JsonValueKind.String &&
                NormalizeLocale(defaultElement.GetString()) is string code)
                defaultLocale = code;
            if (TryGetProperty(root, "supported_locales", "supportedLocales",
                    out JsonElement supportedElement) &&
                supportedElement.ValueKind == JsonValueKind.Array)
            {
                foreach (JsonElement entry in supportedElement.EnumerateArray())
                {
                    if (entry.ValueKind != JsonValueKind.String)
                        continue;
                    if (NormalizeLocale(entry.GetString()) is string entryCode &&
                        !supported.Contains(entryCode, StringComparer.Ordinal))
                        supported.Add(entryCode);
                }
            }
        }
        catch (JsonException)
        {
            return Fallback();
        }
        if (supported.Count == 0)
            supported.Add(defaultLocale);
        if (!supported.Contains(defaultLocale, StringComparer.Ordinal))
            supported.Insert(0, defaultLocale);
        return new ManifestLocales(defaultLocale, supported);
    }

    /// <summary>
    /// Validates one catalog document against the
    /// <c>content_id → speaker/text/alt_text</c> contract. Returns null on
    /// success (writing the entry count), otherwise a structured error.
    /// </summary>
    public static string? ValidateCatalog(
        string catalogJson, string expectedLocale, out int entryCount)
    {
        entryCount = 0;
        string? expected = NormalizeLocale(expectedLocale);
        if (expected is null)
            return $"Expected locale '{expectedLocale}' is not a valid locale code.";
        try
        {
            using var document = JsonDocument.Parse(catalogJson);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object)
                return "Locale catalog root must be a JSON object.";
            if (!root.TryGetProperty("schema_version", out JsonElement schema) ||
                schema.ValueKind != JsonValueKind.Number ||
                !schema.TryGetInt32(out int version) || version != 1)
                return "Locale catalog schema_version must be 1.";
            if (!root.TryGetProperty("locale", out JsonElement locale) ||
                locale.ValueKind != JsonValueKind.String ||
                !string.Equals(NormalizeLocale(locale.GetString()), expected,
                    StringComparison.Ordinal))
                return $"Locale catalog locale must be '{expected}'.";
            if (!root.TryGetProperty("entries", out JsonElement entries) ||
                entries.ValueKind != JsonValueKind.Object)
                return "Locale catalog entries must be an object.";
            foreach (JsonProperty row in entries.EnumerateObject())
            {
                if (string.IsNullOrEmpty(row.Name) ||
                    row.Value.ValueKind != JsonValueKind.Object)
                    return $"Locale entry '{row.Name}' must be an object.";
                foreach (string field in new[] { "speaker", "text", "alt_text" })
                {
                    if (!row.Value.TryGetProperty(field, out JsonElement value) ||
                        value.ValueKind != JsonValueKind.String)
                        return $"Locale entry '{row.Name}' is missing string field '{field}'.";
                }
                entryCount++;
            }
            return null;
        }
        catch (JsonException ex)
        {
            return $"Locale catalog is not valid JSON ({ex.Message}).";
        }
    }

    /// <summary>
    /// Resolves the profile language preference against the runtime
    /// contract: the preference wins when supported, otherwise the
    /// manifest default applies, otherwise <c>"en"</c>.
    /// </summary>
    public string EffectiveLocale(
        IReadOnlyList<string> supportedLocales, string? defaultLocale)
    {
        var supported = (supportedLocales ?? Array.Empty<string>())
            .Select(NormalizeLocale)
            .Where(code => code is not null)
            .Distinct(StringComparer.Ordinal)
            .ToList();
        string fallback = NormalizeLocale(defaultLocale) ?? FallbackDefaultLocale;
        if (supported.Count == 0)
            return fallback;
        string? preference = NormalizeLocale(Profile.Language);
        if (preference is not null &&
            supported.Contains(preference, StringComparer.Ordinal))
            return preference;
        return supported.Contains(fallback, StringComparer.Ordinal)
            ? fallback
            : supported[0];
    }

    // Native selection (handle-seamed, worker-thread safe)

    /// <summary>
    /// Reads the native active locale through a live engine handle.
    /// Returns <c>"en"</c> when the query fails (fail closed).
    /// </summary>
    public static string GetNativeLocale(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return FallbackDefaultLocale;
        string? locale = ReadCallerString(
            (IntPtr buffer, uint size, out uint required) =>
                NativeBridge.RowlEngine_GetLocale(handle, buffer, size, out required));
        return NormalizeLocale(locale) ?? FallbackDefaultLocale;
    }

    /// <summary>
    /// Reads the native supported locales through a live engine handle.
    /// Returns <c>["en"]</c> when the query fails (fail closed).
    /// </summary>
    public static IReadOnlyList<string> GetNativeSupportedLocales(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return new[] { FallbackDefaultLocale };
        string? json = ReadCallerString(
            (IntPtr buffer, uint size, out uint required) =>
                NativeBridge.RowlEngine_GetSupportedLocalesJson(
                    handle, buffer, size, out required));
        if (string.IsNullOrEmpty(json))
            return new[] { FallbackDefaultLocale };
        try
        {
            using var document = JsonDocument.Parse(json);
            var locales = new List<string>();
            foreach (JsonElement element in document.RootElement.EnumerateArray())
            {
                if (element.ValueKind != JsonValueKind.String)
                    continue;
                if (NormalizeLocale(element.GetString()) is string code &&
                    !locales.Contains(code, StringComparer.Ordinal))
                    locales.Add(code);
            }
            return locales.Count > 0
                ? locales
                : new[] { FallbackDefaultLocale };
        }
        catch (JsonException)
        {
            return new[] { FallbackDefaultLocale };
        }
    }

    /// <summary>
    /// Applies one locale to the native runtime. Returns false for a dead
    /// handle or a code the runtime rejects (state is then unchanged).
    /// </summary>
    public static bool TrySetNativeLocale(IntPtr handle, string? locale)
    {
        if (handle == IntPtr.Zero || NormalizeLocale(locale) is not string code)
            return false;
        try
        {
            return NativeBridge.RowlEngine_SetLocale(handle, code) ==
                NativeBridge.ResultCode.Ok;
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>
    /// Applies the effective profile language to the native runtime.
    /// Returns null on success, otherwise a structured error. Never throws.
    /// </summary>
    public string? ApplyProfileLanguageToNative(
        IntPtr handle, IReadOnlyList<string> supportedLocales, string? defaultLocale)
    {
        if (handle == IntPtr.Zero)
            return "Native handle is not available; profile language was not applied.";
        string effective = EffectiveLocale(supportedLocales, defaultLocale);
        return TrySetNativeLocale(handle, effective)
            ? null
            : $"Native runtime rejected locale '{effective}'.";
    }

    /// <summary>
    /// Adopts the native active locale into the profile when it names a
    /// known language. Returns null on success, otherwise a structured
    /// error. Never throws.
    /// </summary>
    public string? RefreshProfileLanguageFromNative(IntPtr handle)
    {
        if (handle == IntPtr.Zero)
            return "Native handle is not available; profile language is unchanged.";
        string active = GetNativeLocale(handle);
        string normalized = PlayerProfile.NormalizeLanguage(active);
        if (!string.Equals(normalized, active, StringComparison.Ordinal) &&
            !PlayerProfile.SupportedLanguages.Contains(
                active, StringComparer.Ordinal))
            return $"Native locale '{active}' is outside the known languages; profile language is unchanged.";
        Profile.Language = normalized;
        return null;
    }

    private static ManifestLocales Fallback() =>
        new(FallbackDefaultLocale, new[] { FallbackDefaultLocale });

    private static bool TryGetProperty(
        JsonElement root, string snake, string camel, out JsonElement value)
    {
        if (root.TryGetProperty(snake, out value))
            return true;
        return root.TryGetProperty(camel, out value);
    }

    private delegate NativeBridge.ResultCode CallerStringQuery(
        IntPtr buffer, uint bufferSize, out uint requiredSize);

    private static string? ReadCallerString(CallerStringQuery query)
    {
        try
        {
            if (query(IntPtr.Zero, 0, out uint required) !=
                    NativeBridge.ResultCode.Ok || required == 0)
                return null;
            IntPtr buffer = Marshal.AllocHGlobal((int)required);
            try
            {
                if (query(buffer, required, out _) != NativeBridge.ResultCode.Ok)
                    return null;
                return Marshal.PtrToStringUTF8(buffer);
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        catch (Exception)
        {
            return null;
        }
    }
}
