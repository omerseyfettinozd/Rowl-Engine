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
    return value;
}

bool isSupportedCode(const std::string& code) {
    if (code.empty() || code.size() > 32) return false;
    return std::all_of(code.begin(), code.end(), [](unsigned char c) {
        return std::isalnum(c) != 0;
    });
}

const nlohmann::json* firstObjectKey(const nlohmann::json& object,
                                     const char* first, const char* second) {
    const auto firstIt = object.find(first);
    if (firstIt != object.end()) return &(*firstIt);
    const auto secondIt = object.find(second);
    if (secondIt != object.end()) return &(*secondIt);
    return nullptr;
}

} // namespace

std::string LocalizationManager::normalizeLocale(const std::string& locale) {
    std::string code = trimLower(locale);
    const std::size_t dash = code.find_first_of("-_");
    if (dash != std::string::npos) code.resize(dash);
    if (!isSupportedCode(code)) return {};
    return code;
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
}

bool LocalizationManager::loadCatalog(const std::string& locale,
                                      const std::string& catalogJson) {
    const std::string code = normalizeLocale(locale);
    if (code.empty() || !isLocaleSupported(code)) return false;
    try {
        const auto document = nlohmann::json::parse(catalogJson);
        if (!document.is_object()) return false;
        if (!document.contains("schema_version") ||
            document.at("schema_version").get<int>() != 1) {
            return false;
        }
        if (!document.contains("locale") || !document.at("locale").is_string() ||
            normalizeLocale(document.at("locale").get<std::string>()) != code) {
            return false;
        }
        if (!document.contains("entries") || !document.at("entries").is_object()) {
            return false;
        }
        std::unordered_map<std::string, LocalizedEntry> entries;
        for (const auto& [contentId, entry] : document.at("entries").items()) {
            if (contentId.empty() || !entry.is_object()) return false;
            for (const char* field : {"speaker", "text", "alt_text"}) {
                if (!entry.contains(field) || !entry.at(field).is_string()) {
                    return false;
                }
            }
            entries.emplace(contentId, LocalizedEntry{
                                           entry.at("speaker").get<std::string>(),
                                           entry.at("text").get<std::string>(),
                                           entry.at("alt_text").get<std::string>(),
                                       });
        }
        m_catalogs[code] = std::move(entries);
        return true;
    } catch (...) {
        return false;
    }
}

void LocalizationManager::clearCatalogs() {
    m_catalogs.clear();
}

bool LocalizationManager::setLocale(const std::string& locale) {
    const std::string code = normalizeLocale(locale);
    if (code.empty() || !isLocaleSupported(code)) return false;
    m_activeLocale = code;
    return true;
}

bool LocalizationManager::isLocaleSupported(const std::string& locale) const {
    const std::string code = normalizeLocale(locale);
    if (code.empty()) return false;
    return std::find(m_manifest.supportedLocales.begin(),
                     m_manifest.supportedLocales.end(),
                     code) != m_manifest.supportedLocales.end();
}

bool LocalizationManager::isCatalogLoaded(const std::string& locale) const {
    const std::string code = normalizeLocale(locale);
    if (code.empty()) return false;
    return m_catalogs.find(code) != m_catalogs.end();
}

const LocalizedEntry* LocalizationManager::findIn(
    const std::string& locale, const std::string& contentId) const {
    if (contentId.empty()) return nullptr;
    const auto catalog = m_catalogs.find(locale);
    if (catalog == m_catalogs.end()) return nullptr;
    const auto entry = catalog->second.find(contentId);
    if (entry == catalog->second.end()) return nullptr;
    return &entry->second;
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
