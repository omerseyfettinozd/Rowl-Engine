/**
 * rowl/i18n/localization_manager.hpp
 *
 * Faz 3 Dilim 1 — Runtime localization state.
 *
 * Owns the manifest locale contract (project.rowlproj default/supported
 * locales), the content_id catalog tables (Assets/locales/<locale>.json)
 * and the fallback chain:
 *
 *   active locale catalog -> default locale catalog -> node original text
 *
 * The manager performs no file I/O on the hot path: catalogs are loaded
 * once per project mount (see applyProjectLocales in c_api_i18n.cpp) and
 * every resolve is a hash lookup. All parsing is fail-closed — a missing
 * or malformed manifest falls back to default "en" with ["en"] supported,
 * and a missing key falls back down the chain instead of crashing.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rowl::I18n {

/// Canonical default when a project predates the locale contract.
inline constexpr const char* kFallbackDefaultLocale = "en";

/// One catalog row: content_id -> speaker / text / alt_text.
struct LocalizedEntry {
    std::string speaker;
    std::string text;
    std::string altText;
};

/// Parsed manifest locale declaration.
struct LocaleManifest {
    std::string defaultLocale = kFallbackDefaultLocale;
    std::vector<std::string> supportedLocales = {kFallbackDefaultLocale};
};

/// Which link of the fallback chain produced a resolved string.
enum class LocaleResolutionSource : uint8_t {
    ActiveLocale = 0,
    DefaultLocale = 1,
    OriginalText = 2,
};

struct ResolvedText {
    std::string value;
    LocaleResolutionSource source = LocaleResolutionSource::OriginalText;
};

class LocalizationManager {
public:
    LocalizationManager() = default;

    // ── Manifest contract ──────────────────────────────────────────────
    //
    // Accepts both spellings so the snake_case form named in the Faz 3
    // slice ("default_locale" / "supported_locales") and the camelCase
    // form already shipped in second_signal ("defaultLocale" /
    // "supportedLocales") resolve identically. Projects without either
    // key (e.g. first_light) keep the "en" fallback untouched.

    /// Parses a manifest JSON document; never throws, never fails —
    /// unusable input yields the "en" fallback declaration.
    static LocaleManifest parseManifestJson(const std::string& manifestJson);

    /// Applies a parsed declaration, resetting the active locale to the
    /// manifest default and dropping previously loaded catalogs.
    void applyManifest(const LocaleManifest& manifest);

    // ── Catalog contract ───────────────────────────────────────────────
    //
    // Canonical files live at Assets/locales/<locale>.json with:
    //   { "schema_version": 1, "locale": "<code>",
    //     "entries": { "<content_id>":
    //       { "speaker": "...", "text": "...", "alt_text": "..." } } }

    /// Loads one catalog document. Returns false (leaving prior state
    /// untouched) when the document is malformed or names another locale.
    bool loadCatalog(const std::string& locale, const std::string& catalogJson);

    /// Forgets every loaded catalog (project unmount boundary).
    void clearCatalogs();

    // ── Runtime selection ──────────────────────────────────────────────

    /// Switches the active locale. Returns false for empty/unknown codes
    /// and leaves the active locale unchanged.
    bool setLocale(const std::string& locale);
    const std::string& getLocale() const { return m_activeLocale; }
    const std::string& getDefaultLocale() const { return m_manifest.defaultLocale; }
    const std::vector<std::string>& getSupportedLocales() const {
        return m_manifest.supportedLocales;
    }
    bool isLocaleSupported(const std::string& locale) const;
    bool isCatalogLoaded(const std::string& locale) const;

    // ── Fallback chain ─────────────────────────────────────────────────
    //
    // resolveEntry tries the active catalog, then the default catalog.
    // resolveText additionally falls back to the node's original text,
    // so callers always receive a displayable string.

    std::optional<LocalizedEntry> resolveEntry(const std::string& contentId) const;
    ResolvedText resolveText(const std::string& contentId,
                             const std::string& originalText) const;
    ResolvedText resolveSpeaker(const std::string& contentId,
                                const std::string& originalSpeaker) const;
    ResolvedText resolveAltText(const std::string& contentId,
                                const std::string& originalAltText) const;

    /// Normalizes a locale tag ("tr-TR" -> "tr"); empty when blank.
    static std::string normalizeLocale(const std::string& locale);

private:
    const LocalizedEntry* findIn(const std::string& locale,
                                 const std::string& contentId) const;

    LocaleManifest m_manifest;
    std::string m_activeLocale = kFallbackDefaultLocale;
    std::unordered_map<std::string,
                       std::unordered_map<std::string, LocalizedEntry>>
        m_catalogs;
};

} // namespace Rowl::I18n

namespace Rowl::Core {
class Engine;
} // namespace Rowl::Core

namespace Rowl::I18n {

/// Project-mount bootstrap (defined in c_api_i18n.cpp): reads the
/// manifest locale declaration and supported catalog files for a freshly
/// mounted project. Kept out of engine.cpp by design.
void applyProjectLocalesToEngine(Rowl::Core::Engine& engine,
                                 const std::string& projectRootUtf8);

} // namespace Rowl::I18n
