using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Faz 3 Dilim 1 — manifest locale sözleşmesi, katalog formatı,
/// PlayerProfile dil tercihi ve native seçim köprüsü (fail-closed).
/// Bütün durumlar yönetilen kodda çalışır; native kitaplık gerekmez.
/// </summary>
public sealed class EditorLocalizationSlice1Tests
{
    private const string SnakeManifest =
        "{\"name\": \"Sinyal\", \"default_locale\": \"tr\", " +
        "\"supported_locales\": [\"tr\", \"en\"]}";

    private const string CamelManifest =
        "{\"name\": \"Second Signal\", \"defaultLocale\": \"en\", " +
        "\"supportedLocales\": [\"en\", \"tr\"]}";

    private const string CatalogTr =
        "{\"schema_version\": 1, \"locale\": \"tr\", \"entries\": {" +
        "\"e3421d4a-c61f-5f2b-8dd0-1984d5c4e911\": {" +
        "\"speaker\": \"Margot\", \"text\": \"Röle cızırtıyla uyanıyor.\", " +
        "\"alt_text\": \"Bir telsiz operatörü bekliyor.\"}}}";

    // ── Manifest sözleşmesi ──────────────────────────────────────────

    [Fact]
    public void ParseManifest_AcceptsSnakeCaseWithTrDefault()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales(SnakeManifest);
        Assert.Equal("tr", parsed.DefaultLocale);
        Assert.Equal(new[] { "tr", "en" }, parsed.SupportedLocales);
    }

    [Fact]
    public void ParseManifest_AcceptsCamelCaseWithEnDefault()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales(CamelManifest);
        Assert.Equal("en", parsed.DefaultLocale);
        Assert.Equal(new[] { "en", "tr" }, parsed.SupportedLocales);
    }

    [Fact]
    public void ParseManifest_LegacyProjectFallsBackToEn()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales(
            "{\"name\": \"First Light\", \"version\": \"1.0.0\"}");
        Assert.Equal("en", parsed.DefaultLocale);
        Assert.Single(parsed.SupportedLocales);
        Assert.Equal("en", parsed.SupportedLocales[0]);
    }

    [Fact]
    public void ParseManifest_MalformedJsonFallsBackToEn()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales("{not json");
        Assert.Equal("en", parsed.DefaultLocale);
        Assert.Single(parsed.SupportedLocales);
    }

    [Fact]
    public void ParseManifest_DefaultOutsideListIsPrepended()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales(
            "{\"default_locale\": \"tr\", \"supported_locales\": [\"en\"]}");
        Assert.Equal("tr", parsed.DefaultLocale);
        Assert.Equal(new[] { "tr", "en" }, parsed.SupportedLocales);
    }

    [Fact]
    public void ParseManifest_RegionTagsNormalizeToPrimarySubtag()
    {
        ManifestLocales parsed = LocalizationService.ParseManifestLocales(
            "{\"default_locale\": \"tr-TR\", " +
            "\"supported_locales\": [\"tr-TR\", \"en-US\"]}");
        Assert.Equal("tr", parsed.DefaultLocale);
        Assert.Equal(new[] { "tr", "en" }, parsed.SupportedLocales);
    }

    // ── Katalog formatı ──────────────────────────────────────────────

    [Fact]
    public void ValidateCatalog_AcceptsCanonicalEntryShape()
    {
        string? error = LocalizationService.ValidateCatalog(
            CatalogTr, "tr", out int entryCount);
        Assert.Null(error);
        Assert.Equal(1, entryCount);
    }

    [Fact]
    public void ValidateCatalog_RejectsWrongSchemaAndLocaleMismatch()
    {
        string? schemaError = LocalizationService.ValidateCatalog(
            CatalogTr.Replace("\"schema_version\": 1", "\"schema_version\": 2"),
            "tr", out _);
        Assert.NotNull(schemaError);

        string? localeError = LocalizationService.ValidateCatalog(
            CatalogTr, "en", out _);
        Assert.NotNull(localeError);
    }

    [Fact]
    public void ValidateCatalog_RejectsEntryMissingAltText()
    {
        string broken = CatalogTr.Replace(", \"alt_text\": \"Bir telsiz operatörü bekliyor.\"", "");
        string? error = LocalizationService.ValidateCatalog(broken, "tr", out _);
        Assert.NotNull(error);
    }

    // ── Profil dil tercihi ───────────────────────────────────────────

    private static LocalizationService ServiceWithLanguage(
        string language, bool sanitize = true)
    {
        var profile = new PlayerProfile { Language = language };
        if (sanitize)
            profile.Sanitized();
        return new LocalizationService(profile);
    }

    [Fact]
    public void EffectiveLocale_PrefersSupportedProfileLanguage()
    {
        var service = ServiceWithLanguage("tr");
        Assert.Equal("tr", service.EffectiveLocale(new[] { "en", "tr" }, "en"));
    }

    [Fact]
    public void EffectiveLocale_FallsBackToManifestDefault()
    {
        // An unsanitized "ja" preference sits outside the supported list,
        // so the manifest default must win over the preference.
        var service = ServiceWithLanguage("ja", sanitize: false);
        Assert.Equal("en", service.EffectiveLocale(new[] { "en", "tr" }, "en"));
        Assert.Equal("tr", service.EffectiveLocale(new[] { "en", "tr" }, "tr"));
    }

    [Fact]
    public void EffectiveLocale_EmptyContractFallsBackToEn()
    {
        var service = ServiceWithLanguage("tr");
        Assert.Equal("en", service.EffectiveLocale(Array.Empty<string>(), null));
    }

    [Fact]
    public void RefreshProfile_RejectsUnknownNativeLocaleWithoutNativeLib()
    {
        var service = ServiceWithLanguage("en");
        string? error = service.RefreshProfileLanguageFromNative(IntPtr.Zero);
        Assert.NotNull(error);
        Assert.Equal("en", service.Profile.Language);
    }

    // ── Native köprü (fail-closed, kitaplksız) ───────────────────────

    [Fact]
    public void NativeCapability_MatchesOneShiftSeven()
    {
        Assert.Equal(128UL, LocalizationService.NativeCapabilityLocalization);
    }

    [Fact]
    public void NativeQueries_DegradeToEnFallbackWithoutHandle()
    {
        Assert.Equal("en", LocalizationService.GetNativeLocale(IntPtr.Zero));
        Assert.Equal(
            new[] { "en" },
            LocalizationService.GetNativeSupportedLocales(IntPtr.Zero));
        Assert.False(LocalizationService.TrySetNativeLocale(IntPtr.Zero, "tr"));
    }

    [Fact]
    public void ApplyProfileLanguage_ReportsMissingHandleInsteadOfThrowing()
    {
        var service = ServiceWithLanguage("tr");
        string? error = service.ApplyProfileLanguageToNative(
            IntPtr.Zero, new[] { "en", "tr" }, "en");
        Assert.NotNull(error);
    }
}
