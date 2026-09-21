/**
 * localization_manager.cpp
 *
 * Faz 3 Dilim 1 — manifest parsing, catalog loading and the
 * active -> default -> original fallback chain. No file I/O here;
 * the project-mount boundary (c_api_i18n.cpp) feeds documents in.
 */

#include "rowl/i18n/localization_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <nlohmann/json.hpp>

namespace Rowl::I18n {

namespace {

std::string trimLower(std::string value) {
    const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::replace(value.begin(), value.end(), '_', '-');
    return value;
}

bool isAlphaRun(const std::string& part, std::size_t minLen,
                std::size_t maxLen) {
    if (part.size() < minLen || part.size() > maxLen) return false;
    return std::all_of(part.begin(), part.end(), [](unsigned char c) {
        return std::isalpha(c) != 0;
    });
}

bool isDigitRun(const std::string& part, std::size_t minLen,
                std::size_t maxLen) {
    if (part.size() < minLen || part.size() > maxLen) return false;
    return std::all_of(part.begin(), part.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

/// BCP 47 shape check on an already lowercased, '-' separated tag:
/// language(2-3 alpha) [script(4 alpha)] [region(2 alpha | 3 digit)]
/// with up to one trailing variant/extension run (2-8 alnum). Region and
/// script subtags are VALIDATED, never stripped.
bool isWellFormedTag(const std::string& code) {
    if (code.empty() || code.size() > 32 || code.front() == '-' ||
        code.back() == '-') {
        return false;
    }
    std::vector<std::string> parts;
    std::string::size_type begin = 0;
    for (std::string::size_type end = 0;; ++end) {
        if (end == code.size() || code[end] == '-') {
            if (end == begin) return false;
            parts.emplace_back(code.substr(begin, end - begin));
            begin = end + 1;
            if (end == code.size()) break;
        }
    }
    if (parts.size() < 1 || parts.size() > 4) return false;
    if (!isAlphaRun(parts[0], 2, 3)) return false;
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto& part = parts[i];
        const bool script = isAlphaRun(part, 4, 4);
        const bool region = isAlphaRun(part, 2, 2) || isDigitRun(part, 3, 3);
        const bool variant =
            part.size() >= 2 && part.size() <= 8 &&
            std::all_of(part.begin(), part.end(), [](unsigned char c) {
                return std::isalnum(c) != 0;
            });
        if (!script && !region && (i < 3 || !variant)) return false;
    }
    return true;
}

const nlohmann::json* firstObjectKey(const nlohmann::json& object,
                                     const char* first, const char* second) {
    const auto firstIt = object.find(first);
    if (firstIt != object.end()) return &(*firstIt);
    const auto secondIt = object.find(second);
    if (secondIt != object.end()) return &(*secondIt);
    return nullptr;
}

/// Parses one catalog field: a plain string, or a plural table
/// (object of category -> template, mandatory "other"). Returns false for
/// any other shape so the caller can skip-and-count the entry.
bool parseEntryField(const nlohmann::json& entry, const char* field,
                     std::string& plain,
                     std::unordered_map<std::string, std::string>& plural) {
    const auto it = entry.find(field);
    if (it == entry.end()) return false;
    if (it->is_string()) {
        plain = it->get<std::string>();
        return true;
    }
    if (!it->is_object() || it->empty() || !it->contains("other") ||
        !it->at("other").is_string()) {
        return false;
    }
    for (const auto& [category, form] : it->items()) {
        if (category.empty() || !form.is_string()) return false;
        plural.emplace(category, form.get<std::string>());
    }
    plain = it->at("other").get<std::string>();
    return true;
}

bool isOneOtherLanguage(const std::string& language) {
    static const char* const kLanguages[] = {
        "en", "de", "es", "it", "pt", "nl", "sv", "da", "fi",
        "nb", "nn", "el", "hu", "ca", "gl", "eu", "eo", "et",
    };
    return std::find(std::begin(kLanguages), std::end(kLanguages),
                     language) != std::end(kLanguages);
}

bool isSlavicThreeWay(const std::string& language) {
    static const char* const kLanguages[] = {"ru", "uk", "be", "hr",
                                             "sr", "bs", "cnr"};
    return std::find(std::begin(kLanguages), std::end(kLanguages),
                     language) != std::end(kLanguages);
}

bool isOtherOnly(const std::string& language) {
    static const char* const kLanguages[] = {
        "tr", "az", "ug", "ja", "zh", "ko", "vi",
        "th", "id", "ms", "lo", "my", "km",
    };
    return std::find(std::begin(kLanguages), std::end(kLanguages),
                     language) != std::end(kLanguages);
}

} // namespace

std::string LocalizationManager::normalizeLocale(const std::string& locale) {
    const std::string code = trimLower(locale);
    if (!isWellFormedTag(code)) return {};
    return code;
}

std::vector<std::string> LocalizationManager::fallbackChain(
    const std::string& locale) {
    std::vector<std::string> chain;
    std::string::size_type end = locale.size();
    while (end != std::string::npos && end > 0) {
        chain.emplace_back(locale.substr(0, end));
        end = locale.find_last_of('-', end - 1);
        if (end == 0) break;
    }
    return chain;
}

LocaleManifest LocalizationManager::parseManifestJson(
    const std::string& manifestJson) {
    LocaleManifest manifest;
    try {
        const auto document = nlohmann::json::parse(manifestJson);
        if (!document.is_object()) return manifest;

        if (const auto* defaultNode =
                firstObjectKey(document, "default_locale", "defaultLocale")) {
            if (defaultNode->is_string()) {
                const std::string code = normalizeLocale(defaultNode->get<std::string>());
                if (!code.empty()) manifest.defaultLocale = code;
            }
        }

        if (const auto* supportedNode = firstObjectKey(
                document, "supported_locales", "supportedLocales")) {
            if (supportedNode->is_array() && !supportedNode->empty()) {
                std::vector<std::string> supported;
                for (const auto& entry : *supportedNode) {
                    if (!entry.is_string()) continue;
                    const std::string code = normalizeLocale(entry.get<std::string>());
                    if (code.empty()) continue;
                    if (std::find(supported.begin(), supported.end(), code) ==
                        supported.end()) {
                        supported.push_back(code);
                    }
                }
                if (!supported.empty()) manifest.supportedLocales = std::move(supported);
            }
        }
    } catch (...) {
        return LocaleManifest{};
    }

    if (std::find(manifest.supportedLocales.begin(),
                  manifest.supportedLocales.end(),
                  manifest.defaultLocale) == manifest.supportedLocales.end()) {
        manifest.supportedLocales.insert(manifest.supportedLocales.begin(),
                                         manifest.defaultLocale);
    }
    return manifest;
}

void LocalizationManager::applyManifest(const LocaleManifest& manifest) {
    m_manifest = manifest;
    m_activeLocale = manifest.defaultLocale;
    m_catalogs.clear();
    m_skippedEntries.clear();
}

bool LocalizationManager::loadCatalog(const std::string& locale,
                                      const std::string& catalogJson,
                                      std::size_t* skippedOut) {
    const auto setSkipped = [&](std::size_t count) {
        if (skippedOut) *skippedOut = count;
    };
    const std::string code = normalizeLocale(locale);
    if (code.empty() || !isLocaleSupported(code)) {
        setSkipped(0);
        return false;
    }
    try {
        const auto document = nlohmann::json::parse(catalogJson);
        if (!document.is_object()) {
            setSkipped(0);
            return false;
        }
        if (!document.contains("schema_version") ||
            (document.at("schema_version").get<int>() != 1 &&
             document.at("schema_version").get<int>() != 2)) {
            setSkipped(0);
            return false;
        }
        if (!document.contains("locale") || !document.at("locale").is_string() ||
            normalizeLocale(document.at("locale").get<std::string>()) != code) {
            setSkipped(0);
            return false;
        }
        if (!document.contains("entries") || !document.at("entries").is_object()) {
            setSkipped(0);
            return false;
        }
        // #94: malformed rows are skipped and counted — one translator
        // slip no longer vetoes the whole locale. Only a broken document
        // (above) still rejects the load with prior state untouched.
        std::unordered_map<std::string, LocalizedEntry> entries;
        std::size_t skipped = 0;
        for (const auto& [contentId, entry] : document.at("entries").items()) {
            LocalizedEntry row;
            if (contentId.empty() || !entry.is_object() ||
                !parseEntryField(entry, "speaker", row.speaker,
                                 row.speakerPlural) ||
                !parseEntryField(entry, "text", row.text, row.textPlural) ||
                !parseEntryField(entry, "alt_text", row.altText,
                                 row.altPlural)) {
                ++skipped;
                continue;
            }
            entries.emplace(contentId, std::move(row));
        }
        m_catalogs[code] = std::move(entries);
        m_skippedEntries[code] = skipped;
        setSkipped(skipped);
        return true;
    } catch (...) {
        setSkipped(0);
        return false;
    }
}

void LocalizationManager::clearCatalogs() {
    m_catalogs.clear();
    m_skippedEntries.clear();
}

std::size_t LocalizationManager::skippedEntries(
    const std::string& locale) const {
    const auto it = m_skippedEntries.find(normalizeLocale(locale));
    return it != m_skippedEntries.end() ? it->second : 0;
}

std::string LocalizationManager::matchSupported(
    const std::string& locale) const {
    const std::string code = normalizeLocale(locale);
    if (code.empty()) return {};
    for (const auto& link : fallbackChain(code)) {
        if (std::find(m_manifest.supportedLocales.begin(),
                      m_manifest.supportedLocales.end(),
                      link) != m_manifest.supportedLocales.end()) {
            return link;
        }
    }
    return {};
}

bool LocalizationManager::hasCatalogFor(
    const std::vector<std::string>& chain) const {
    for (const auto& link : chain) {
        if (m_catalogs.find(link) != m_catalogs.end()) return true;
    }
    return false;
}

LocalizationManager::LocaleSetResult LocalizationManager::trySetLocale(
    const std::string& locale) {
    const std::string match = matchSupported(locale);
    if (match.empty()) return LocaleSetResult::Unsupported;
    // #96: a supported-but-unloaded locale must not report success while a
    // sibling catalog proves the system is live — the host would otherwise
    // serve one language believing it serves another. Locale-free projects
    // (no catalog loaded anywhere) keep the legacy accept so the "en"
    // fallback never breaks bootstrapping.
    if (!hasCatalogFor(fallbackChain(match)) && !m_catalogs.empty()) {
        return LocaleSetResult::CatalogMissing;
    }
    m_activeLocale = match;
    return LocaleSetResult::Ok;
}

bool LocalizationManager::setLocale(const std::string& locale) {
    return trySetLocale(locale) == LocaleSetResult::Ok;
}

bool LocalizationManager::isLocaleSupported(const std::string& locale) const {
    return !matchSupported(locale).empty();
}

bool LocalizationManager::isCatalogLoaded(const std::string& locale) const {
    const std::string code = normalizeLocale(locale);
    if (code.empty()) return false;
    return hasCatalogFor(fallbackChain(code));
}

std::string LocalizationManager::pluralCategory(const std::string& language,
                                                      double count) {
    // Documented CLDR-cardinal subset for absolute counts (the engine use
    // case: item/enemy/coin tallies). Operands follow CLDR: i = integer
    // digits, v = visible fraction digits. Unknown languages yield "other".
    const std::string lang = normalizeLocale(language);
    const std::string base =
        lang.substr(0, lang.find('-') == std::string::npos
                           ? lang.size()
                           : lang.find('-'));
    const double magnitude = std::fabs(count);
    const long long i = static_cast<long long>(std::floor(magnitude));
    const bool integral = (magnitude == std::floor(magnitude));
    const long long mod10 = i % 10;
    const long long mod100 = i % 100;

    if (base == "ar") {
        if (magnitude == 0.0) return "zero";
        if (magnitude == 1.0) return "one";
        if (magnitude == 2.0) return "two";
        const long long mod = static_cast<long long>(std::fmod(magnitude, 100.0));
        if (mod >= 3 && mod <= 10) return "few";
        if (mod >= 11 && mod <= 99) return "many";
        return "other";
    }
    if (isSlavicThreeWay(base)) {
        if (integral && mod10 == 1 && mod100 != 11) return "one";
        if (integral && mod10 >= 2 && mod10 <= 4 &&
            (mod100 < 12 || mod100 > 14)) {
            return "few";
        }
        if (integral &&
            (mod10 == 0 || (mod10 >= 5 && mod10 <= 9) ||
             (mod100 >= 11 && mod100 <= 14))) {
            return "many";
        }
        return "other";
    }
    if (base == "pl") {
        if (integral && i == 1) return "one";
        if (integral && mod10 >= 2 && mod10 <= 4 &&
            (mod100 < 12 || mod100 > 14)) {
            return "few";
        }
        if (!integral || i == 0 || mod10 == 0 || mod10 == 1 ||
            (mod10 >= 5 && mod10 <= 9) || (mod100 >= 12 && mod100 <= 14)) {
            return "many";
        }
        return "other";
    }
    if (base == "cs" || base == "sk") {
        if (integral && i == 1) return "one";
        if (integral && i >= 2 && i <= 4) return "few";
        return "other";
    }
    if (base == "fr" || base == "hy") {
        if (i == 0 || i == 1) return "one";
        return "other";
    }
    if (base == "fa") {
        if (i == 0 || magnitude == 1.0) return "one";
        return "other";
    }
    if (isOneOtherLanguage(base)) {
        if (integral && i == 1) return "one";
        return "other";
    }
    if (isOtherOnly(base)) return "other";
    return "other";
}

namespace {

const std::string* pickPluralForm(
    const std::unordered_map<std::string, std::string>& table,
    const std::string& category) {
    const auto exact = table.find(category);
    if (exact != table.end()) return &exact->second;
    const auto other = table.find("other");
    if (other != table.end()) return &other->second;
    return nullptr;
}

} // namespace

ResolvedText LocalizationManager::resolvePlural(
    const std::string& contentId, const std::string& originalText,
    double count) const {
    if (contentId.empty()) return {originalText, LocaleResolutionSource::OriginalText};
    const std::string language = m_activeLocale.substr(
        0, m_activeLocale.find('-') == std::string::npos
               ? m_activeLocale.size()
               : m_activeLocale.find('-'));
    const std::string category = pluralCategory(language, count);
    const std::vector<std::string> scopes = {m_activeLocale,
                                             m_manifest.defaultLocale};
    for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
        for (const auto& link : fallbackChain(scopes[scope])) {
            const auto catalog = m_catalogs.find(link);
            if (catalog == m_catalogs.end()) continue;
            const auto entry = catalog->second.find(contentId);
            if (entry == catalog->second.end()) continue;
            if (!entry->second.textPlural.empty()) {
                if (const auto* form =
                        pickPluralForm(entry->second.textPlural, category)) {
                    return {*form, scope == 0
                                        ? LocaleResolutionSource::ActiveLocale
                                        : LocaleResolutionSource::DefaultLocale};
                }
                continue;
            }
            return {entry->second.text, scope == 0
                                            ? LocaleResolutionSource::ActiveLocale
                                            : LocaleResolutionSource::DefaultLocale};
        }
    }
    return {originalText, LocaleResolutionSource::OriginalText};
}

const LocalizedEntry* LocalizationManager::findIn(
    const std::string& locale, const std::string& contentId) const {
    if (contentId.empty()) return nullptr;
    // Every lookup walks the BCP 47 chain, so "pt-BR" resolves from the
    // "pt" catalog when no "pt-BR" catalog is loaded — without merging
    // the two tags the way the old truncation did.
    for (const auto& link : fallbackChain(locale)) {
        const auto catalog = m_catalogs.find(link);
        if (catalog == m_catalogs.end()) continue;
        const auto entry = catalog->second.find(contentId);
        if (entry != catalog->second.end()) return &entry->second;
    }
    return nullptr;
}

std::optional<LocalizedEntry> LocalizationManager::resolveEntry(
    const std::string& contentId) const {
    if (const auto* active = findIn(m_activeLocale, contentId)) {
        return *active;
    }
    if (m_activeLocale != m_manifest.defaultLocale) {
        if (const auto* fallback = findIn(m_manifest.defaultLocale, contentId)) {
            return *fallback;
        }
    }
    return std::nullopt;
}

ResolvedText LocalizationManager::resolveText(
    const std::string& contentId, const std::string& originalText) const {
    if (const auto* active = findIn(m_activeLocale, contentId)) {
        return {active->text, LocaleResolutionSource::ActiveLocale};
    }
    if (m_activeLocale != m_manifest.defaultLocale) {
        if (const auto* fallback = findIn(m_manifest.defaultLocale, contentId)) {
            return {fallback->text, LocaleResolutionSource::DefaultLocale};
        }
    }
    return {originalText, LocaleResolutionSource::OriginalText};
}

ResolvedText LocalizationManager::resolveSpeaker(
    const std::string& contentId, const std::string& originalSpeaker) const {
    const auto entry = resolveEntry(contentId);
    if (entry) {
        const bool fromActive =
            findIn(m_activeLocale, contentId) != nullptr;
        return {entry->speaker, fromActive
                                    ? LocaleResolutionSource::ActiveLocale
                                    : LocaleResolutionSource::DefaultLocale};
    }
    return {originalSpeaker, LocaleResolutionSource::OriginalText};
}

ResolvedText LocalizationManager::resolveAltText(
    const std::string& contentId, const std::string& originalAltText) const {
    const auto entry = resolveEntry(contentId);
    if (entry) {
        const bool fromActive =
            findIn(m_activeLocale, contentId) != nullptr;
        return {entry->altText, fromActive
                                    ? LocaleResolutionSource::ActiveLocale
                                    : LocaleResolutionSource::DefaultLocale};
    }
    return {originalAltText, LocaleResolutionSource::OriginalText};
}

} // namespace Rowl::I18n
