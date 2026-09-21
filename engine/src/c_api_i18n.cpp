/**
 * c_api_i18n.cpp
 *
 * Faz 3 Dilim 1 — C-API localization surface: locale selection queries
 * and the project-mount locale bootstrap. Additive only; every older
 * entry point is untouched. Bodies delegate to Rowl::I18n state owned
 * by the Engine — engine.cpp itself gains no logic.
 */

#include "c_api_internal.hpp"
#include "rowl/i18n/localization_manager.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/vfs/vfs.hpp"

#include "filesystem"
#include "fstream"
#include "nlohmann/json.hpp"

namespace {

/// Reads one small text document for the locale bootstrap: VFS first
/// (packaged releases), loose filesystem as fallback. Empty when absent,
/// oversized, or unreadable — callers treat that as "not loaded".
std::string readLocaleDocument(Rowl::VFS::VFSManager* vfs,
                               const std::filesystem::path& loosePath,
                               const std::string& vfsName,
                               std::uint64_t maxBytes) {
    if (vfs && !vfsName.empty()) {
        // Quiet by contract: multi-source probing must not log.
        const std::string probes[] = {"locales/" + vfsName,
                                      "Assets/locales/" + vfsName};
        for (const auto& probe : probes) {
            try {
                const std::string text = vfs->readString(probe);
                if (!text.empty() && text.size() <= maxBytes) return text;
            } catch (...) {
            }
        }
    }
    std::error_code sizeError;
    if (!std::filesystem::is_regular_file(loosePath, sizeError) ||
        sizeError ||
        std::filesystem::file_size(loosePath, sizeError) > maxBytes ||
        sizeError) {
        return {};
    }
    try {
        std::ifstream stream(
            Rowl::Platform::pathFromUtf8(Rowl::Platform::pathToUtf8(loosePath)));
        return std::string((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    } catch (...) {
        return {};
    }
}

/// Reads the manifest locale declaration and the supported catalog files
/// for a freshly mounted project. Missing keys keep the "en" fallback;
/// missing catalog files stay unloaded so resolution falls down the
/// active -> default -> original chain instead of failing the mount.
/// A supported locale whose catalog cannot be loaded is WARN-logged so
/// the silent fallback the release gate would otherwise hide is at least
/// visible in the log.
void bootstrapProjectLocales(Rowl::Core::Engine* engine,
                             const std::filesystem::path& projectPath) {
    using Rowl::I18n::LocaleManifest;
    using Rowl::I18n::LocalizationManager;

    LocaleManifest manifest;
    // The manifest lives next to the (necessarily loose) project root —
    // SetProjectDirectory itself requires a writable filesystem root —
    // so it is read from the loose path only. Catalogs below go
    // VFS-first: packaged releases forbid the loose Assets tree.
    const std::string manifestText = readLocaleDocument(
        nullptr, projectPath / "project.rowlproj", "", 1024 * 1024);
    if (!manifestText.empty()) {
        try {
            manifest = LocalizationManager::parseManifestJson(manifestText);
        } catch (...) {
            manifest = LocaleManifest{};
        }
    }

    auto& localization = engine->getLocalization();
    localization.applyManifest(manifest);

    for (const auto& locale : manifest.supportedLocales) {
        const std::string text = readLocaleDocument(
            engine->getVfs(),
            projectPath / "Assets" / "locales" / (locale + ".json"),
            locale + ".json", 16 * 1024 * 1024);
        if (text.empty()) {
            ROWL_LOG_WARN("Locale '" + locale +
                          "' is manifest-listed but has no readable catalog; "
                          "it resolves through the fallback chain");
            continue;
        }
        std::size_t skipped = 0;
        if (!localization.loadCatalog(locale, text, &skipped)) {
            ROWL_LOG_WARN("Locale catalog '" + locale +
                          "' failed validation and stays unloaded; "
                          "it resolves through the fallback chain");
        } else if (skipped > 0) {
            ROWL_LOG_WARN("Locale catalog '" + locale + "' loaded with " +
                          std::to_string(skipped) +
                          " malformed entries skipped");
        }
    }
}

} // namespace

namespace Rowl::I18n {

/// Project-mount bootstrap shared with SetProjectDirectory.
/// Defined here so engine.cpp and window.cpp stay untouched.
void applyProjectLocalesToEngine(Rowl::Core::Engine& engine,
                                 const std::string& projectRootUtf8);

void applyProjectLocalesToEngine(Rowl::Core::Engine& engine,
                                 const std::string& projectRootUtf8) {
    if (projectRootUtf8.empty()) return;
    bootstrapProjectLocales(
        &engine, Rowl::Platform::pathFromUtf8(projectRootUtf8));
}

} // namespace Rowl::I18n

extern "C" {

RowlEngine_ResultCode RowlEngine_SetLocale(RowlEngineHandle handle,
                                           const char* locale) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    if (!locale || !*locale) return ROWL_RESULT_INVALID_ARGUMENT;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        using Rowl::I18n::LocalizationManager;
        switch (engine->getLocalization().trySetLocale(locale)) {
            case LocalizationManager::LocaleSetResult::Ok:
                // The newly selected language must reach the already
                // mounted dialogue: re-resolve the live lines from their
                // stored originals so GetDialogue/render flip at once.
                engine->refreshActiveDialogueLocalization();
                return ROWL_RESULT_OK;
            case LocalizationManager::LocaleSetResult::CatalogMissing: {
                if (auto* ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::FileNotFound,
                                  std::string("SetLocale refused: locale '") +
                                      locale +
                                      "' is supported but its catalog is not "
                                      "loaded",
                                  "set_locale", "");
                }
                return ROWL_RESULT_FILE_NOT_FOUND;
            }
            case LocalizationManager::LocaleSetResult::Unsupported:
            default:
                return ROWL_RESULT_INVALID_ARGUMENT;
        }
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLocale(RowlEngineHandle handle, char* buffer,
                                           uint32_t bufferSize,
                                           uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getLocalization().getLocale(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetSupportedLocalesJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        nlohmann::json locales = nlohmann::json::array();
        for (const auto& locale :
             engine->getLocalization().getSupportedLocales()) {
            locales.push_back(locale);
        }
        return copyUtf8ToCaller(locales.dump(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
