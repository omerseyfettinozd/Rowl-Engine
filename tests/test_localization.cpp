/**
 * test_localization.cpp — Faz 3 Dilim 1 contract:
 * manifest locale declaration, content_id catalog format, the
 * active -> default -> original fallback chain, and the additive
 * C ABI (SetLocale / GetLocale / GetSupportedLocalesJson +
 * CAPABILITY_LOCALIZATION). Legacy projects without locale keys
 * keep the "en" fallback; no older entry point changes behaviour.
 */
#include "rowl_test_harness.hpp"
#include "rowl/i18n/localization_manager.hpp"

#include <chrono>
#include <vector>

namespace {

using Rowl::I18n::LocaleManifest;
using Rowl::I18n::LocaleResolutionSource;
using Rowl::I18n::LocalizationManager;

void fail(const std::string& message) {
    std::cerr << message << std::endl;
    exit(1);
}

const char* kCamelManifest = R"({
    "name": "Second Signal",
    "defaultLocale": "en",
    "supportedLocales": ["en", "tr"]
})";

const char* kSnakeManifest = R"({
    "name": "Sinyal",
    "default_locale": "tr",
    "supported_locales": ["tr", "en"]
})";

const char* kCatalogEn = R"({
    "schema_version": 1,
    "locale": "en",
    "entries": {
        "id-only-en": {
            "speaker": "Margot",
            "text": "English fallback line.",
            "alt_text": "English alt."
        },
        "id-both": {
            "speaker": "Voice",
            "text": "Shared line (en).",
            "alt_text": ""
        }
    }
})";

const char* kCatalogTr = R"({
    "schema_version": 1,
    "locale": "tr",
    "entries": {
        "id-both": {
            "speaker": "Ses",
            "text": "Ortak satır (tr).",
            "alt_text": ""
        }
    }
})";

std::string readCallerString(
    RowlEngineHandle handle,
    RowlEngine_ResultCode (*getter)(RowlEngineHandle, char*, uint32_t,
                                    uint32_t*),
    const char* caseName) {
    uint32_t required = 0;
    if (getter(handle, nullptr, 0, &required) != ROWL_RESULT_OK ||
        required < 2) {
        fail(std::string("Locale size query failed: ") + caseName);
    }
    std::vector<char> undersized(required - 1, 'x');
    uint32_t repeated = 0;
    if (getter(handle, undersized.data(),
               static_cast<uint32_t>(undersized.size()),
               &repeated) != ROWL_RESULT_BUFFER_TOO_SMALL ||
        repeated != required || undersized.front() != '\0') {
        fail(std::string("Locale undersized contract failed: ") + caseName);
    }
    std::vector<char> buffer(required, '\0');
    if (getter(handle, buffer.data(), static_cast<uint32_t>(buffer.size()),
               &repeated) != ROWL_RESULT_OK ||
        repeated != required || buffer.back() != '\0') {
        fail(std::string("Locale exact-size copy failed: ") + caseName);
    }
    return std::string(buffer.data());
}

void testManifestContract() {
    const LocaleManifest camel =
        LocalizationManager::parseManifestJson(kCamelManifest);
    if (camel.defaultLocale != "en" || camel.supportedLocales.size() != 2 ||
        camel.supportedLocales[0] != "en" ||
        camel.supportedLocales[1] != "tr") {
        fail("camelCase manifest locale declaration was misparsed");
    }

    const LocaleManifest snake =
        LocalizationManager::parseManifestJson(kSnakeManifest);
    if (snake.defaultLocale != "tr" || snake.supportedLocales.size() != 2 ||
        snake.supportedLocales[0] != "tr" ||
        snake.supportedLocales[1] != "en") {
        fail("snake_case manifest locale declaration was misparsed");
    }

    // Legacy projects (first_light) carry no locale keys at all.
    const LocaleManifest legacy = LocalizationManager::parseManifestJson(
        R"({"name": "First Light", "version": "1.0.0"})");
    if (legacy.defaultLocale != "en" || legacy.supportedLocales.size() != 1 ||
        legacy.supportedLocales[0] != "en") {
        fail("Legacy manifest did not fall back to default \"en\"");
    }

    const LocaleManifest broken =
        LocalizationManager::parseManifestJson("{not json");
    if (broken.defaultLocale != "en" || broken.supportedLocales.size() != 1) {
        fail("Malformed manifest did not fall back to default \"en\"");
    }

    // A default outside the supported list is honoured, not dropped.
    const LocaleManifest drifted = LocalizationManager::parseManifestJson(
        R"({"default_locale": "tr", "supported_locales": ["en"]})");
    if (drifted.defaultLocale != "tr" || drifted.supportedLocales.front() != "tr" ||
        drifted.supportedLocales.size() != 2) {
        fail("Manifest default outside the supported list was dropped");
    }

    // Region tags collapse to their primary subtag.
    const LocaleManifest tagged = LocalizationManager::parseManifestJson(
        R"({"default_locale": "tr-TR", "supported_locales": ["tr-TR", "en-US"]})");
    if (tagged.defaultLocale != "tr" || tagged.supportedLocales.size() != 2 ||
        tagged.supportedLocales[0] != "tr" ||
        tagged.supportedLocales[1] != "en") {
        fail("BCP-47 locale tags were not normalized to primary subtags");
    }
    TEST_PASS("Manifest locale declaration (both spellings + legacy fallback)");
}

void testCatalogAndFallbackChain() {
    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(kSnakeManifest));
    if (manager.getLocale() != "tr") {
        fail("Active locale did not reset to the manifest default");
    }
    if (!manager.loadCatalog("en", kCatalogEn) ||
        !manager.loadCatalog("tr", kCatalogTr)) {
        fail("Valid canonical catalogs were rejected");
    }
    if (manager.loadCatalog("tr", kCatalogEn) ||
        manager.loadCatalog("ja", kCatalogEn) ||
        manager.loadCatalog("en", R"({"schema_version": 2})")) {
        fail("Malformed or mismatched catalog was accepted");
    }

    // Active-locale hit.
    const auto both = manager.resolveText("id-both", "ORIGINAL");
    if (both.value != "Ortak satır (tr)." ||
        both.source != LocaleResolutionSource::ActiveLocale) {
        fail("Active-locale catalog hit did not resolve");
    }

    // Default-locale fallback needs a manager whose default differs from
    // the active locale: "id-only-en" exists only in the "en" catalog.
    LocalizationManager enDefault;
    enDefault.applyManifest(
        LocalizationManager::parseManifestJson(kCamelManifest));
    if (!enDefault.loadCatalog("en", kCatalogEn) ||
        !enDefault.loadCatalog("tr", kCatalogTr) ||
        !enDefault.setLocale("tr")) {
        fail("Could not stage the default-fallback manager");
    }
    const auto fallback = enDefault.resolveText("id-only-en", "ORIGINAL NODE");
    if (fallback.value != "English fallback line." ||
        fallback.source != LocaleResolutionSource::DefaultLocale) {
        fail("Default-locale fallback did not resolve");
    }
    // Missing everywhere -> the node's original text, never an error.
    const auto original = manager.resolveText("id-unknown", "ORIGINAL NODE");
    if (original.value != "ORIGINAL NODE" ||
        original.source != LocaleResolutionSource::OriginalText) {
        fail("Unknown content_id did not fall back to the original text");
    }
    const auto speaker = manager.resolveSpeaker("id-unknown", "Narrator");
    if (speaker.value != "Narrator") {
        fail("Unknown content_id did not fall back to the original speaker");
    }

    if (manager.setLocale("ja") || manager.setLocale("") ||
        manager.getLocale() != "tr") {
        fail("Unsupported locale changed the active locale");
    }
    TEST_PASS("Catalog format and active -> default -> original fallback");
}

void testCanonicalFixtureCatalogs() {
    // The shipped second_signal catalogs must satisfy the same contract
    // the runtime enforces; this pins the fixture to the parser.
    namespace fs = std::filesystem;
    const fs::path locales = fs::path("samples/second_signal/Assets/locales");
    std::string enText;
    std::string trText;
    try {
        std::ifstream en(locales / "en.json");
        std::ifstream tr(locales / "tr.json");
        enText = std::string((std::istreambuf_iterator<char>(en)),
                             std::istreambuf_iterator<char>());
        trText = std::string((std::istreambuf_iterator<char>(tr)),
                             std::istreambuf_iterator<char>());
    } catch (...) {
        fail("Could not read the canonical locale fixture files");
    }
    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(kCamelManifest));
    if (!manager.loadCatalog("en", enText) ||
        !manager.loadCatalog("tr", trText)) {
        fail("Canonical second_signal catalogs were rejected");
    }
    if (!manager.setLocale("tr")) fail("Canonical project rejected locale tr");
    const auto resolved = manager.resolveText(
        "e3421d4a-c61f-5f2b-8dd0-1984d5c4e911", "ORIGINAL");
    if (resolved.value.find("Röle") == std::string::npos ||
        resolved.source != LocaleResolutionSource::ActiveLocale) {
        fail("Canonical tr catalog did not resolve the opening line");
    }
    TEST_PASS("Canonical second_signal catalogs satisfy the contract");
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) fail("Could not stage locale test project file");
}

void testCabiSurface() {
    TEST_SECTION("Localization C ABI (additive, backward compatible)");

    uint64_t capabilities = 0;
    if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
        (capabilities & ROWL_ENGINE_CAPABILITY_LOCALIZATION) == 0) {
        fail("CAPABILITY_LOCALIZATION (128) is not advertised");
    }
    if ((capabilities & (ROWL_ENGINE_CAPABILITY_RESULT_CODES |
                         ROWL_ENGINE_CAPABILITY_CALLER_BUFFERS |
                         ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT |
                         ROWL_ENGINE_CAPABILITY_PLAYER_LOOP |
                         ROWL_ENGINE_CAPABILITY_SAVE_METADATA |
                         ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES)) == 0) {
        fail("Capability advertisement dropped a pre-existing flag");
    }
    TEST_PASS("CAPABILITY_LOCALIZATION advertised, older flags intact");

    // Dead-handle and bad-argument guards must never unwind into the host.
    if (RowlEngine_SetLocale(nullptr, "tr") != ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_GetLocale(nullptr, nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_GetSupportedLocalesJson(nullptr, nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_HANDLE) {
        fail("Locale C API null-handle guards failed");
    }
    TEST_PASS("Locale C API null-handle guards");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        fail("C-API engine init failed for the locale surface");
    }

    // Fresh handles speak the "en" fallback before any project mounts.
    if (readCallerString(handle, RowlEngine_GetLocale, "fresh locale") != "en" ||
        readCallerString(handle, RowlEngine_GetSupportedLocalesJson,
                         "fresh supported") != "[\"en\"]") {
        fail("Fresh handle did not report the \"en\" fallback");
    }
    if (RowlEngine_SetLocale(handle, nullptr) != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_SetLocale(handle, "") != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_SetLocale(handle, "ja") != ROWL_RESULT_INVALID_ARGUMENT ||
        readCallerString(handle, RowlEngine_GetLocale,
                         "rejected locale") != "en") {
        fail("Rejected SetLocale mutated state or misreported");
    }
    TEST_PASS("Fresh-handle fallback and SetLocale rejection");

    // Mount a task-shaped project: snake_case manifest, tr default.
    namespace fs = std::filesystem;
    const auto projectRoot =
        fs::temp_directory_path() /
        ("rowl_locale_slice1_" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code mountError;
    fs::create_directories(projectRoot / "Assets" / "locales", mountError);
    if (mountError) fail("Could not stage the locale test project");
    writeFile(projectRoot / "project.rowlproj", kSnakeManifest);
    writeFile(projectRoot / "Assets" / "locales" / "en.json", kCatalogEn);
    writeFile(projectRoot / "Assets" / "locales" / "tr.json", kCatalogTr);

    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    if (readCallerString(handle, RowlEngine_GetLocale, "mounted locale") !=
            "tr" ||
        readCallerString(handle, RowlEngine_GetSupportedLocalesJson,
                         "mounted supported") != "[\"tr\",\"en\"]") {
        fail("Project mount did not apply the manifest locale declaration");
    }
    if (RowlEngine_SetLocale(handle, "en-US") != ROWL_RESULT_OK ||
        readCallerString(handle, RowlEngine_GetLocale,
                         "normalized locale") != "en") {
        fail("Region-tagged locale was not normalized on selection");
    }
    RowlEngine_Destroy(handle);
    fs::remove_all(projectRoot, mountError);
    TEST_PASS("Project mount applies manifest locales; tags normalize");
}

} // namespace

void test_localization() {
    TEST_SECTION("Localization — Manifest, Catalogs, Fallback, C ABI");
    testManifestContract();
    testCatalogAndFallbackChain();
    testCanonicalFixtureCatalogs();
    testCabiSurface();
}
