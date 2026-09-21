/**
 * test_locale_cluster.cpp — Dilim Locale kümesi kilit testleri
 * (#90-#96 HIGH + #98/#101, ikizler #97/#99/#100 sürüklenir):
 *
 * T1 motor kablolaması: düğüm montajında çözüm + SetLocale sonrası
 *   orijinallerden yeniden çözüm (ölü kablo #90/#98/#101).
 * T2 BCP 47: etiketler bütün korunur, zincir-bilinçli arama (#91/#93).
 * T3 SetLocale kapısı: desteklenen-ama-yüksüz -> FILE_NOT_FOUND,
 *   katalogsuz projede legacy kabul (C-API eşlemesi dahil) (#96).
 * T4 girdi-atlama: bozuk satırlar sayılarak atlanır, şema 1|2 (#94).
 * T5 plural: CLDR-altküme kategoriler + tablo çözümü (#94).
 * T6 paketli bootstrap: .rowlpkg içindeki katalog VFS'ten yüklenir (#92).
 * T7 RTL: dil etiketi FriBidi taban yönünü sabitler (#95).
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/i18n/localization_manager.hpp"
#include "rowl/text/text_shaper.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace {

using Rowl::I18n::LocalizationManager;
using Rowl::I18n::LocaleResolutionSource;

[[noreturn]] void localeFail(const std::string& message) {
    std::cerr << "Locale cluster failure: " << message << std::endl;
    std::exit(1);
}

void writeLocaleFile(const std::filesystem::path& path,
                     const std::string& text) {
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    if (!stream) localeFail("could not stage file: " + path.string());
}

std::string readLocaleString(
    RowlEngineHandle handle,
    RowlEngine_ResultCode (*getter)(RowlEngineHandle, char*, std::uint32_t,
                                    std::uint32_t*),
    const char* caseName) {
    std::uint32_t required = 0;
    if (getter(handle, nullptr, 0, &required) != ROWL_RESULT_OK ||
        required < 2) {
        localeFail(std::string("size query failed: ") + caseName);
    }
    std::vector<char> buffer(required, '\0');
    std::uint32_t repeated = 0;
    if (getter(handle, buffer.data(),
               static_cast<std::uint32_t>(buffer.size()),
               &repeated) != ROWL_RESULT_OK ||
        repeated != required) {
        localeFail(std::string("exact-size copy failed: ") + caseName);
    }
    return std::string(buffer.data());
}

std::filesystem::path freshTempDir(const std::string& prefix) {
    const auto dir =
        std::filesystem::temp_directory_path() /
        (prefix +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) localeFail("could not create temp dir");
    return dir;
}

const char* kClusterManifest = R"({
    "default_locale": "tr",
    "supported_locales": ["tr", "en"]
})";

const char* kClusterTr = R"({
    "schema_version": 1,
    "locale": "tr",
    "entries": {
        "t1-line": {
            "speaker": "Ses",
            "text": "Küme satırı (tr).",
            "alt_text": "Küme alt (tr)."
        }
    }
})";

const char* kClusterEn = R"({
    "schema_version": 1,
    "locale": "en",
    "entries": {
        "t1-line": {
            "speaker": "Voice",
            "text": "Cluster line (en).",
            "alt_text": "Cluster alt (en)."
        }
    }
})";

const char* kClusterStory = R"({
    "format_version": 4,
    "start_node_id": 1,
    "nodes": [
        {"id": 1, "components": [
            {"type": "dialogue", "data": {
                "content_id": "t1-line",
                "speaker": "ORIG_SPK",
                "dialogue": "ORIGINAL NODE TEXT",
                "typewriter_enabled": false
            }}
        ], "next_nodes": []}
    ]
})";

// T1: assembly-time resolve + refresh-from-originals on locale switch.
void testEngineWiring() {
    Rowl::Core::Engine engine;
    Rowl::Core::EngineConfig config;
    config.virtualWidth = 1920;
    config.virtualHeight = 1080;
    engine.initialize(config);
    engine.getLocalization().applyManifest(
        LocalizationManager::parseManifestJson(kClusterManifest));
    if (!engine.getLocalization().loadCatalog("tr", kClusterTr) ||
        !engine.getLocalization().loadCatalog("en", kClusterEn)) {
        localeFail("cluster catalogs were rejected");
    }

    const auto graphPath =
        freshTempDir("rowl_locale_t1_") / "story.json";
    writeLocaleFile(graphPath, kClusterStory);
    engine.loadStoryGraphFromPath(graphPath.string());
    engine.setPlayState(true);
    engine.resetToStartNode();

    // Mount default is "tr": the live line must show the catalog, not the
    // node original — this is the dead-wiring claim (#90).
    if (engine.getActiveDialogue() != "Küme satırı (tr)." ||
        engine.getActiveSpeaker() != "Ses") {
        localeFail("assembly did not resolve the active-locale catalog");
    }
    const auto& lines = engine.getActiveDialogues();
    if (lines.empty() || lines.front().originalDialogue != "ORIGINAL NODE TEXT" ||
        lines.front().originalSpeaker != "ORIG_SPK" ||
        lines.front().language != "tr") {
        localeFail("assembly did not stamp originals + language");
    }

    // Locale switch re-resolves from stored originals without a node change.
    if (engine.getLocalization().trySetLocale("en") !=
        LocalizationManager::LocaleSetResult::Ok) {
        localeFail("trySetLocale(en) was refused");
    }
    engine.refreshActiveDialogueLocalization();
    if (engine.getActiveDialogue() != "Cluster line (en)." ||
        engine.getActiveSpeaker() != "Voice") {
        localeFail("refresh did not flip the live line to English");
    }
    if (engine.getLocalization().trySetLocale("tr") !=
        LocalizationManager::LocaleSetResult::Ok) {
        localeFail("trySetLocale(tr) was refused");
    }
    engine.refreshActiveDialogueLocalization();
    if (engine.getActiveDialogue() != "Küme satırı (tr).") {
        localeFail("refresh did not flip the live line back to Turkish");
    }
    std::error_code cleanup;
    std::filesystem::remove_all(graphPath.parent_path(), cleanup);
    TEST_PASS("T1 engine wiring: assembly resolve + refresh-on-switch");
}

// T2: full tags preserved, chain-aware lookup, malformed rejected.
void testBcp47Chain() {
    if (LocalizationManager::normalizeLocale("PT_br") != "pt-br" ||
        LocalizationManager::normalizeLocale("en-US") != "en-us" ||
        LocalizationManager::normalizeLocale("zh-Hant-TW") != "zh-hant-tw") {
        localeFail("well-formed tags were not preserved whole");
    }
    for (const char* bad : {"", "x", "en--us", "e", "toolongtag-0123456789-abcdef-xyz",
                            "en_u$", "12", "-en", "en-"}) {
        if (!LocalizationManager::normalizeLocale(bad).empty()) {
            localeFail(std::string("malformed tag accepted: ") + bad);
        }
    }

    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(
        R"({"default_locale": "en", "supported_locales": ["en", "pt"]})"));
    const char* ptCatalog = R"({
        "schema_version": 1,
        "locale": "pt",
        "entries": {
            "t1-line": {
                "speaker": "Voz",
                "text": "Linha do cluster (pt).",
                "alt_text": "Alt (pt)."
            }
        }
    })";
    if (!manager.loadCatalog("pt", ptCatalog) ||
        !manager.loadCatalog("en", kClusterEn)) {
        localeFail("could not stage the BCP47 manager");
    }
    // "pt-BR" has no catalog of its own: chain-aware lookup serves "pt".
    if (!manager.isLocaleSupported("pt-BR") ||
        !manager.isCatalogLoaded("pt-BR")) {
        localeFail("pt-BR did not fall down the chain to pt");
    }
    if (manager.isLocaleSupported("ja") || manager.isCatalogLoaded("ja")) {
        localeFail("unrelated locale reported as covered");
    }
    // Best-match selection: "pt-BR" selects manifest "pt".
    if (manager.trySetLocale("pt-BR") !=
            LocalizationManager::LocaleSetResult::Ok ||
        manager.getLocale() != "pt") {
        localeFail("pt-BR did not best-match manifest pt");
    }
    TEST_PASS("T2 BCP47: whole tags, chain lookup, best-match select");
}

// T3: supported-but-unloaded gate + legacy accept + C-API mapping.
void testSetLocaleGate() {
    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(
        R"({"default_locale": "en",
            "supported_locales": ["en", "tr"]})"));
    if (!manager.loadCatalog("en", kClusterEn)) {
        localeFail("could not stage the gate manager");
    }
    // "tr" is supported but its catalog never loaded, while "en" proves the
    // system is live -> CatalogMissing, never a silent wrong-language OK.
    if (manager.trySetLocale("tr") !=
        LocalizationManager::LocaleSetResult::CatalogMissing) {
        localeFail("supported-but-unloaded locale did not gate");
    }
    if (manager.getLocale() != "en") {
        localeFail("refused trySetLocale mutated the active locale");
    }
    // Locale-free projects (no catalog anywhere) keep the legacy accept.
    LocalizationManager fresh;
    fresh.applyManifest(LocalizationManager::parseManifestJson(
        R"({"default_locale": "en",
            "supported_locales": ["en", "tr"]})"));
    if (fresh.trySetLocale("tr") != LocalizationManager::LocaleSetResult::Ok ||
        fresh.getLocale() != "tr") {
        localeFail("locale-free project lost the legacy accept");
    }

    // C-API mapping: mount a project whose "tr" catalog file is missing.
    const auto projectRoot = freshTempDir("rowl_locale_t3_");
    std::error_code error;
    std::filesystem::create_directories(projectRoot / "Assets" / "locales",
                                        error);
    writeLocaleFile(projectRoot / "project.rowlproj", kClusterManifest);
    // Manifest default is "tr" but only the "en" catalog ships: bootstrap
    // must not fail the mount, it resolves through the chain.
    writeLocaleFile(projectRoot / "Assets" / "locales" / "en.json",
                    kClusterEn);
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        localeFail("C-API engine init failed for the gate test");
    }
    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    if (RowlEngine_SetLocale(handle, "tr") != ROWL_RESULT_FILE_NOT_FOUND) {
        localeFail("C-API gate did not map CatalogMissing to FILE_NOT_FOUND");
    }
    if (readLocaleString(handle, RowlEngine_GetLocale,
                         "gate locale") != "tr") {
        // Active locale is the manifest default; the refusal keeps it.
        localeFail("refused C-API SetLocale mutated the active locale");
    }
    if (RowlEngine_SetLocale(handle, "en") != ROWL_RESULT_OK) {
        localeFail("C-API SetLocale(en) was refused");
    }
    RowlEngine_Destroy(handle);
    std::filesystem::remove_all(projectRoot, error);
    TEST_PASS("T3 SetLocale gate incl. C-API FILE_NOT_FOUND mapping");
}

// T4: malformed rows are skipped and counted; schema 1|2 accepted.
void testSkipAndCount() {
    const char* catalog = R"({
        "schema_version": 2,
        "locale": "en",
        "entries": {
            "good": {
                "speaker": "Voice",
                "text": "Good line.",
                "alt_text": "Good alt."
            },
            "no-other": {
                "speaker": "Voice",
                "text": {"one": "One."},
                "alt_text": "Alt."
            },
            "non-string": {
                "speaker": "Voice",
                "text": 42,
                "alt_text": "Alt."
            },
            "missing-field": {
                "speaker": "Voice",
                "alt_text": "Alt."
            }
        }
    })";
    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(
        R"({"default_locale": "en", "supported_locales": ["en"]})"));
    std::size_t skipped = 999;
    if (!manager.loadCatalog("en", catalog, &skipped) || skipped != 3) {
        localeFail("skip-and-count misreported (want 3)");
    }
    if (manager.skippedEntries("en") != 3) {
        localeFail("skippedEntries() did not retain the count");
    }
    const auto good = manager.resolveText("good", "ORIGINAL");
    if (good.value != "Good line." ||
        good.source != LocaleResolutionSource::ActiveLocale) {
        localeFail("surviving row did not resolve");
    }
    if (manager.resolveText("no-other", "ORIGINAL").value != "ORIGINAL") {
        localeFail("skipped row leaked into resolution");
    }
    // A structurally broken document still rejects with state untouched.
    std::size_t skipped2 = 999;
    if (manager.loadCatalog("en", R"({"schema_version": 99})",
                            &skipped2) ||
        skipped2 != 0 ||
        manager.resolveText("good", "ORIGINAL").value != "Good line.") {
        localeFail("broken document was not rejected cleanly");
    }
    TEST_PASS("T4 skip-and-count (schema 2 incl.), broken doc rejects");
}

// T5: CLDR-subset plural categories + table resolution.
void testPluralResolution() {
    if (LocalizationManager::pluralCategory("ru", 1) != "one" ||
        LocalizationManager::pluralCategory("ru", 2) != "few" ||
        LocalizationManager::pluralCategory("ru", 5) != "many" ||
        LocalizationManager::pluralCategory("ru", 1.5) != "other") {
        localeFail("Slavic three-way categories wrong");
    }
    if (LocalizationManager::pluralCategory("ar", 0) != "zero" ||
        LocalizationManager::pluralCategory("ar", 1) != "one" ||
        LocalizationManager::pluralCategory("ar", 2) != "two" ||
        LocalizationManager::pluralCategory("ar", 3) != "few" ||
        LocalizationManager::pluralCategory("ar", 11) != "many" ||
        LocalizationManager::pluralCategory("ar", 100) != "other") {
        localeFail("Arabic six-way categories wrong");
    }
    if (LocalizationManager::pluralCategory("en", 1) != "one" ||
        LocalizationManager::pluralCategory("en", 0) != "other" ||
        LocalizationManager::pluralCategory("tr", 5) != "other" ||
        LocalizationManager::pluralCategory("xx", 3) != "other") {
        localeFail("one/other/other-only categories wrong");
    }

    const char* catalog = R"({
        "schema_version": 1,
        "locale": "ru",
        "entries": {
            "coins": {
                "speaker": "Ses",
                "text": {"one": "1 монета", "few": "2 монеты",
                         "many": "5 монет", "other": "монет"},
                "alt_text": "Alt."
            },
            "plain": {
                "speaker": "Ses",
                "text": "Düz satır.",
                "alt_text": "Alt."
            }
        }
    })";
    LocalizationManager manager;
    manager.applyManifest(LocalizationManager::parseManifestJson(
        R"({"default_locale": "ru", "supported_locales": ["ru"]})"));
    if (!manager.loadCatalog("ru", catalog)) {
        localeFail("plural catalog was rejected");
    }
    if (manager.resolvePlural("coins", "ORIG", 1).value != "1 монета" ||
        manager.resolvePlural("coins", "ORIG", 2).value != "2 монеты" ||
        manager.resolvePlural("coins", "ORIG", 5).value != "5 монет") {
        localeFail("plural table did not resolve per-count");
    }
    if (manager.resolvePlural("coins", "ORIG", 1).source !=
        LocaleResolutionSource::ActiveLocale) {
        localeFail("plural hit misattributed its source");
    }
    // Plain-string entries ignore the count.
    if (manager.resolvePlural("plain", "ORIG", 5).value != "Düz satır.") {
        localeFail("plain entry did not ignore the count");
    }
    TEST_PASS("T5 plural categories + table resolution");
}

// Minimal .rowlpkg writer (flags=0 raw entries): header, payloads, index.
void writeTestPackage(const std::filesystem::path& pkgPath,
                      const std::vector<std::pair<std::string, std::string>>&
                          files) {
#pragma pack(push, 1)
    struct Header {
        char magic[4];
        std::uint16_t specVersion;
        std::uint32_t fileCount;
        std::uint64_t indexOffset;
    };
    struct EntryRaw {
        std::uint64_t pathHash;
        std::uint32_t pathLength;
        std::uint64_t offset;
        std::uint64_t compressedSize;
        std::uint64_t uncompressedSize;
        std::uint32_t flags;
    };
#pragma pack(pop)
    static_assert(sizeof(Header) == 18, "pkg header layout");
    static_assert(sizeof(EntryRaw) == 40, "pkg entry layout");

    std::ofstream out(pkgPath, std::ios::binary);
    if (!out) localeFail("could not write test package");
    Header header{};
    std::memcpy(header.magic, "ROWL", 4);
    header.specVersion = 1;
    header.fileCount = static_cast<std::uint32_t>(files.size());
    std::uint64_t cursor = sizeof(Header);
    // Payloads first; the index offset follows the last payload byte.
    for (const auto& [path, bytes] : files) cursor += bytes.size();
    header.indexOffset = cursor;
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    std::uint64_t payloadCursor = sizeof(Header);
    struct IndexRow {
        EntryRaw raw;
        std::string path;
    };
    std::vector<IndexRow> rows;
    for (const auto& [path, bytes] : files) {
        out.write(bytes.data(),
                  static_cast<std::streamsize>(bytes.size()));
        EntryRaw raw{};
        raw.pathHash = 0; // lookup key is the canonical path, not the hash
        raw.pathLength = static_cast<std::uint32_t>(path.size());
        raw.offset = payloadCursor;
        raw.compressedSize = bytes.size();
        raw.uncompressedSize = bytes.size();
        raw.flags = 0;
        rows.push_back({raw, path});
        payloadCursor += bytes.size();
    }
    for (const auto& row : rows) {
        out.write(reinterpret_cast<const char*>(&row.raw),
                  sizeof(row.raw));
        out.write(row.path.data(),
                  static_cast<std::streamsize>(row.path.size()));
    }
    out.flush();
    if (!out) localeFail("test package write failed");
}

// T6: catalog served from a packaged .rowlpkg, no loose Assets/locales.
void testPackagedBootstrap() {
    const auto projectRoot = freshTempDir("rowl_locale_t6_");
    std::error_code error;
    std::filesystem::create_directories(projectRoot / "Assets" / "packages",
                                        error);
    writeLocaleFile(projectRoot / "project.rowlproj", kClusterManifest);
    writeTestPackage(projectRoot / "Assets" / "packages" / "locales.rowlpkg",
                     {{"locales/tr.json", kClusterTr},
                      {"locales/en.json", kClusterEn}});

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        localeFail("C-API engine init failed for the packaged test");
    }
    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    // Manifest default "tr" served from the package, not the loose tree.
    if (readLocaleString(handle, RowlEngine_GetLocale,
                         "packaged locale") != "tr") {
        localeFail("packaged catalog did not bootstrap the default locale");
    }
    if (RowlEngine_SetLocale(handle, "en") != ROWL_RESULT_OK ||
        readLocaleString(handle, RowlEngine_GetLocale,
                         "packaged switch") != "en") {
        localeFail("packaged catalog did not serve the locale switch");
    }
    RowlEngine_Destroy(handle);
    std::filesystem::remove_all(projectRoot, error);
    TEST_PASS("T6 packaged .rowlpkg catalog bootstrap");
}

// T7: an explicit RTL tag anchors the FriBidi base direction; the legacy
// undifferentiated path keeps content inference. Mixed "Hello مرحبا":
// content inference starts LTR ("Hello" first), PAR_RTL starts Arabic.
void testRtlBaseDirection() {
    using namespace Rowl::Text;
    std::vector<std::uint8_t> font;
    {
        std::ifstream stream("Assets/fonts/default.ttf", std::ios::binary);
        font = {std::istreambuf_iterator<char>(stream), {}};
    }
    if (font.empty()) localeFail("test font is missing");
    if (!TextShaper::isAdvancedBackendCompiled()) {
        TEST_PASS("T7 RTL base direction (advanced backend uncompiled)");
        return;
    }
    TextShaper shaper;
    if (!shaper.loadFontFromMemory(font.data(), font.size()) ||
        !shaper.isAdvancedBackendActive()) {
        localeFail("font init failed for the RTL test");
    }
    const std::string mixed = "Hello مرحبا";
    ShapeOptions inferred;
    inferred.fontSize = 24.0f;
    const ShapedText ltr = shaper.shapeMarkup(mixed, inferred);
    ShapeOptions anchored = inferred;
    anchored.language = "ar";
    const ShapedText rtl = shaper.shapeMarkup(mixed, anchored);
    if (ltr.glyphs.empty() || rtl.glyphs.empty()) {
        localeFail("mixed-direction line produced no glyphs");
    }
    // Scalar 6 is the first Arabic codepoint ("Hello " is 0-5, Arabic 6-10).
    if (ltr.glyphs.front().logicalScalar == 6) {
        localeFail("inferred base direction did not start Latin-first");
    }
    // RTL base: the Arabic run leads, in visual (reversed) order — the
    // last logical codepoint (10) is the first visual glyph.
    if (rtl.glyphs.front().logicalScalar != 10) {
        localeFail("explicit ar tag did not anchor an RTL base direction");
    }
    // An unrelated LTR tag must behave exactly like the legacy path.
    ShapeOptions english = inferred;
    english.language = "en";
    const ShapedText en = shaper.shapeMarkup(mixed, english);
    if (en.glyphs.size() != ltr.glyphs.size() ||
        en.glyphs.front().logicalScalar !=
            ltr.glyphs.front().logicalScalar) {
        localeFail("LTR tag diverged from the legacy inference path");
    }
    TEST_PASS("T7 RTL base direction anchored by language tag");
}

} // namespace

void test_locale_cluster() {
    TEST_SECTION("Locale Cluster (Dilim Locale kumesi kilitleri)");
    testEngineWiring();
    testBcp47Chain();
    testSetLocaleGate();
    testSkipAndCount();
    testPluralResolution();
    testPackagedBootstrap();
    testRtlBaseDirection();
}
