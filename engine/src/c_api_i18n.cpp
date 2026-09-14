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
#include "rowl/platform/user_data_directories.hpp"

#include "filesystem"
#include "fstream"
#include "nlohmann/json.hpp"

namespace {

/// Reads the manifest locale declaration and the supported catalog files
/// for a freshly mounted project. Missing keys keep the "en" fallback;
/// missing catalog files stay unloaded so resolution falls down the
/// active -> default -> original chain instead of failing the mount.
void bootstrapProjectLocales(Rowl::Core::Engine* engine,
                             const std::filesystem::path& projectPath) {
    using Rowl::I18n::LocaleManifest;
    using Rowl::I18n::LocalizationManager;

    LocaleManifest manifest;
    const auto manifestPath = projectPath / "project.rowlproj";
    std::error_code manifestError;
    if (std::filesystem::is_regular_file(manifestPath, manifestError) &&
        !manifestError &&
        std::filesystem::file_size(manifestPath, manifestError) <= 1024 * 1024 &&
        !manifestError) {
        try {
            std::ifstream stream(manifestPath);
            std::string text((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
            manifest = LocalizationManager::parseManifestJson(text);
        } catch (...) {
            manifest = LocaleManifest{};
        }
    }

    auto& localization = engine->getLocalization();
    localization.applyManifest(manifest);

    for (const auto& locale : manifest.supportedLocales) {
        const auto catalogPath =
            projectPath / "Assets" / "locales" / (locale + ".json");
        std::error_code catalogError;
        if (!std::filesystem::is_regular_file(catalogPath, catalogError) ||
            catalogError ||
            std::filesystem::file_size(catalogPath, catalogError) > 16 * 1024 * 1024 ||
            catalogError) {
            continue;
        }
        try {
            std::ifstream stream(
                Rowl::Platform::pathFromUtf8(Rowl::Platform::pathToUtf8(catalogPath)));
            std::string text((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
            localization.loadCatalog(locale, text);
        } catch (...) {
            // One broken catalog never blocks the mount; that locale
            // simply resolves through the fallback chain.
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
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return engine->getLocalization().setLocale(locale)
                   ? ROWL_RESULT_OK
                   : ROWL_RESULT_INVALID_ARGUMENT;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLocale(RowlEngineHandle handle, char* buffer,
                                           uint32_t bufferSize,
                                           uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
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
        auto* engine = toEngine(handle);
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
