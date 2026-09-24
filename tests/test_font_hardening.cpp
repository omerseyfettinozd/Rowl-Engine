/**
 * test_font_hardening.cpp — A3-tur7 kilidi: font-yükleme fail-closed.
 *
 * Kilitlenen davranışlar (font_renderer.cpp):
 *  1. Geçerli paketlenmiş font yüklenir (kontrol — guard'lar meşru yolu kırmaz).
 *  2. Kayıp dosya / dizin false döner (throw yok, crash yok).
 *  3. Dev-dosya (64MB tavanı üstü, seyrek-fixture — diskte bayt değil,
 *     tellg'de 65MB) okunmadan reddedilir: resize-denemesi yok, false.
 *  4. #57 stage-then-commit: başarısız reload (bozuk bellek/dosya) canlı
 *     fontu öldürmez — isLoaded true kalır, ölçüm aynı değeri verir
 *     (eski kod m_loaded=false yapıp canlı tamponu eziyordu).
 *  5. #57 TOCTOU-soak: tellg/read arasında kırpılan dosyaya karşı yükleme
 *     döngüsü crash üretmez; isLoaded iken ölçüm her zaman geçerlidir.
 *  6. #41-kalıntı (shaper staging): TextShaper basarisiz reload'da onceki
 *     iyi backend'i düşürmez — isAdvancedBackendActive korunur.
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/text/text_shaper.hpp"

namespace {

using Rowl::Render::FontRenderer;

std::vector<uint8_t> readFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
                                std::istreambuf_iterator<char>());
}

// Yapısal-gecersiz font: gecerli sfnt surumu + numTables=0. stb taramasini
// OOB'a sokmadan (ham 0xFF gibi numTables=0xFFFF uretip stbtt__find_table'i
// tasmis-tampon okumaya itmez — o pre-existing stb davranisi, bu testin
// konusu degil) temiz parse-fail uretir.
std::vector<uint8_t> makeStructuredInvalid(size_t size) {
    std::vector<uint8_t> bytes(size, 0u);
    if (size >= 6) {
        bytes[0] = 0x00u;
        bytes[1] = 0x01u;
        bytes[2] = 0x00u;
        bytes[3] = 0x00u; // sfnt version 1.0
        bytes[4] = 0x00u;
        bytes[5] = 0x00u; // numTables = 0
    }
    return bytes;
}

// stb-gecer / FT-gecersiz font: gecerli fontun head/hhea/loca tablolarindan
// birinin length'i devasa yazilir. stb InitFont yalnizca tablo varligi +
// alan-okumasi yaptigindan (offset gecerli, okumalar tampon-ici) gecer;
// FT yuz-kurulumu yukledigi tablolarin sinirlarini dogrulayip reddeder.
// Tum okumalar tampon-icidir (OOB yok).
std::vector<uint8_t> makeStbPassFtFail(const std::vector<uint8_t>& valid) {
    std::vector<uint8_t> bytes = valid;
    if (bytes.size() < 12) return {};
    const uint16_t numTables = static_cast<uint16_t>((bytes[4] << 8) | bytes[5]);
    // stb InitFont'un icerik okumadigi tablolar tercih edilir: sondajla
    // dogrulandi (font_probe): head/hhea/loca length-bozmasi stb=pass +
    // FT=fail verir; name/maxp/GPOS'ta FT yuz-kurulumu gecer (lazy).
    const char* const kPreferred[] = {"head", "hhea", "loca"};
    size_t targetRec = 0;
    bool targetFound = false;
    for (const char* tag : kPreferred) {
        for (uint16_t i = 0; i < numTables && !targetFound; ++i) {
            const size_t rec = 12 + static_cast<size_t>(i) * 16;
            if (rec + 16 > bytes.size()) break;
            if (bytes[rec] == static_cast<uint8_t>(tag[0]) &&
                bytes[rec + 1] == static_cast<uint8_t>(tag[1]) &&
                bytes[rec + 2] == static_cast<uint8_t>(tag[2]) &&
                bytes[rec + 3] == static_cast<uint8_t>(tag[3])) {
                targetRec = rec;
                targetFound = true;
            }
        }
        if (targetFound) break;
    }
    if (!targetFound || targetRec + 16 > bytes.size()) return {};
    bytes[targetRec + 12] = 0x7Fu;
    bytes[targetRec + 13] = 0xFFu;
    bytes[targetRec + 14] = 0xFFu;
    bytes[targetRec + 15] = 0xFFu; // length = 0x7FFFFFFF
    return bytes;
}

} // namespace

void test_font_hardening() {
    TEST_SECTION("Font Loading Fail-Closed Lock (A3-tur7)");

    // 1. Kontrol: paketlenmiş font yüklenir.
    FontRenderer control;
    if (!control.loadFontFromPath("Assets/fonts/default.ttf") || !control.isLoaded()) {
        std::cerr << "Font hardening lock failure: bundled default.ttf did not load"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Bundled default.ttf loads (guards do not break the valid path)");

    // 2. Kayıp + dizin: false, throw yok.
    FontRenderer hostile;
    if (hostile.loadFontFromPath("nope/missing_tur7.ttf")) {
        std::cerr << "Font hardening lock failure: missing font reported success"
                  << std::endl;
        std::exit(1);
    }
    if (hostile.loadFontFromPath("Assets/fonts")) {
        std::cerr << "Font hardening lock failure: directory reported success"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Missing file and directory fail closed (no throw)");

    // 3. Dev-dosya: seyrek 65MB fixture (tellg 65MB görür, disk ~baytlar).
    const auto hugePath =
        std::filesystem::temp_directory_path() / "rowl_tur7_hugefont.ttf";
    std::error_code ec;
    std::filesystem::remove(hugePath, ec);
    {
        std::ofstream out(hugePath, std::ios::binary);
        if (!out) {
            std::cerr << "Font hardening lock failure: could not stage sparse fixture"
                      << std::endl;
            std::exit(1);
        }
        out.seekp(static_cast<std::streamoff>(65ULL * 1024ULL * 1024ULL));
        out.put('\0');
    }
    FontRenderer huge;
    if (huge.loadFontFromPath(hugePath.string())) {
        std::cerr << "Font hardening lock failure: 65MB file was not rejected"
                  << std::endl;
        std::exit(1);
    }
    if (huge.isLoaded()) {
        std::cerr << "Font hardening lock failure: rejected file left renderer loaded"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Oversize (sparse 65MB) font rejected before read (fail closed)");
    std::filesystem::remove(hugePath, ec);

    // 4. #57: basarisiz reload canli fontu oldurmez (stage-then-commit).
    // Eski kod: parse-fail'de m_loaded=false + canli tampon ezik; yeni kod:
    // false doner ama onceki font aynen calisir.
    FontRenderer staged;
    if (!staged.loadFontFromPath("Assets/fonts/default.ttf") || !staged.isLoaded()) {
        std::cerr << "Font hardening lock failure: staged control font did not load"
                  << std::endl;
        std::exit(1);
    }
    const float liveWidth = staged.measureTextWidth("Ag", 24.0f);
    if (!(liveWidth > 0.0f)) {
        std::cerr << "Font hardening lock failure: control font measures zero"
                  << std::endl;
        std::exit(1);
    }
    std::vector<uint8_t> invalid = makeStructuredInvalid(512);
    if (staged.loadFontFromMemory(invalid.data(), invalid.size())) {
        std::cerr << "Font hardening lock failure: structured-invalid buffer "
                     "reported success"
                  << std::endl;
        std::exit(1);
    }
    if (!staged.isLoaded()) {
        std::cerr << "Font hardening lock failure: failed reload killed live font"
                  << std::endl;
        std::exit(1);
    }
    if (staged.measureTextWidth("Ag", 24.0f) != liveWidth) {
        std::cerr << "Font hardening lock failure: live font metrics changed after "
                     "failed reload"
                  << std::endl;
        std::exit(1);
    }
    // #41a pini: stb-gecer ama FT-gecersiz bayt artik sessiz fallback
    // degil, yukleme-basarisizligidir (eski kod 'stb fallback' ile true
    // donuyordu); canli font yine korunur.
    // Yapi-notu: ileri backend derlenmemis build'de (ROWL_TEXT_SHAPING_AVAILABLE
    // kapali — örn. sistemde harfbuzz/fribidi yoksa) stb yolu mesru yukleme
    // yoludur ve bu alt-kontrol uygulanmaz; CI sanitizer/tsan job'lari backend
    // paketlerini kurdugu icin orada her zaman calisir.
    const bool advancedCompiled =
        Rowl::Text::TextShaper::isAdvancedBackendCompiled();
    const std::vector<uint8_t> validBytesEarly =
        readFileBytes("Assets/fonts/default.ttf");
    if (validBytesEarly.empty()) {
        std::cerr << "Font hardening lock failure: could not read control font bytes"
                  << std::endl;
        std::exit(1);
    }
    if (!advancedCompiled) {
        TEST_PASS("Advanced backend absent: stb-only path is the legitimate build contract (FT-reject check skipped)");
    } else {
    const std::vector<uint8_t> stbOnly = makeStbPassFtFail(validBytesEarly);
    if (stbOnly.empty()) {
        std::cerr << "Font hardening lock failure: could not craft stb-pass fixture"
                  << std::endl;
        std::exit(1);
    }
    if (staged.loadFontFromMemory(stbOnly.data(), stbOnly.size())) {
        std::cerr << "Font hardening lock failure: FT-rejected buffer reported "
                     "success (silent backend downgrade)"
                  << std::endl;
        std::exit(1);
    }
    if (!staged.isLoaded() || staged.measureTextWidth("Ag", 24.0f) != liveWidth) {
        std::cerr << "Font hardening lock failure: FT-rejected reload clobbered "
                     "live font"
                  << std::endl;
        std::exit(1);
    }
    } // advancedCompiled
    // Bozuk dosya yolu da ayni garantiyi verir.
    const auto badPath =
        std::filesystem::temp_directory_path() / "rowl_tur7_garbage.ttf";
    {
        std::ofstream out(badPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(invalid.data()),
                  static_cast<std::streamsize>(invalid.size()));
    }
    if (staged.loadFontFromPath(badPath.string())) {
        std::cerr << "Font hardening lock failure: garbage file reported success"
                  << std::endl;
        std::exit(1);
    }
    if (!staged.isLoaded() || staged.measureTextWidth("Ag", 24.0f) != liveWidth) {
        std::cerr << "Font hardening lock failure: garbage file reload clobbered "
                     "live font"
                  << std::endl;
        std::exit(1);
    }
    std::filesystem::remove(badPath, ec);
    TEST_PASS("Failed reload keeps live font working (stage-then-commit)");

    // 4b. Kesik dosya (gecerli baslik + yarim dizin) parse'e girmeden
    // reddedilir; stb ic-tarama OOB'una dusmez, canli font korunur.
    const auto cutPath =
        std::filesystem::temp_directory_path() / "rowl_tur7_cut.ttf";
    {
        std::ofstream out(cutPath, std::ios::binary);
        const size_t cutSize = std::min<size_t>(200, validBytesEarly.size());
        out.write(reinterpret_cast<const char*>(validBytesEarly.data()),
                  static_cast<std::streamsize>(cutSize));
    }
    if (staged.loadFontFromPath(cutPath.string())) {
        std::cerr << "Font hardening lock failure: truncated file reported success"
                  << std::endl;
        std::exit(1);
    }
    if (!staged.isLoaded() || staged.measureTextWidth("Ag", 24.0f) != liveWidth) {
        std::cerr << "Font hardening lock failure: truncated file reload clobbered "
                     "live font"
                  << std::endl;
        std::exit(1);
    }
    std::filesystem::remove(cutPath, ec);
    TEST_PASS("Truncated file rejected before stb scan (live font kept)");

    // 5. #57 TOCTOU-soak: yazar-thread dosyayi gecerli/sifir arasi
    // cevirirken yukleme dongusu crash uretmez; isLoaded iken olcum gecerli.
    // Iki durum da stb-taramasi icin guvenlidir (sifirlar numTables=0 verir;
    // ayni-boy + atomik rename ile yirtik-icerik uretilmez — yirtik girdide
    // stb ic-taramasi pre-existing OOB yapar, bu testin konusu degil).
    const auto racePath =
        std::filesystem::temp_directory_path() / "rowl_tur7_race.ttf";
    const auto raceTmp =
        std::filesystem::temp_directory_path() / "rowl_tur7_race.tmp";
    const std::vector<uint8_t>& validBytes = validBytesEarly;
    const std::vector<uint8_t> zeroBytes(validBytes.size(), 0u);
    std::atomic<bool> raceStop{false};
    std::thread churn([&] {
        bool flip = false;
        while (!raceStop.load()) {
            const std::vector<uint8_t>& payload = flip ? validBytes : zeroBytes;
            {
                std::ofstream out(raceTmp, std::ios::binary | std::ios::trunc);
                if (out) {
                    out.write(reinterpret_cast<const char*>(payload.data()),
                              static_cast<std::streamsize>(payload.size()));
                }
            }
            std::error_code renameEc;
            std::filesystem::rename(raceTmp, racePath, renameEc);
            flip = !flip;
        }
    });
    FontRenderer racy;
    for (int i = 0; i < 200; ++i) {
        (void)racy.loadFontFromPath(racePath.string());
        // Degismez: yasar font olculur durumda ya da yukleyici bos;
        // sarkan-info deref'i burada crash verirdi.
        if (racy.isLoaded() && !(racy.measureTextWidth("Ag", 24.0f) > 0.0f)) {
            raceStop.store(true);
            churn.join();
            std::cerr << "Font hardening lock failure: loaded renderer measures "
                         "zero mid-churn (dangling font info?)"
                      << std::endl;
            std::exit(1);
        }
    }
    raceStop.store(true);
    churn.join();
    std::filesystem::remove(racePath, ec);
    if (!racy.loadFontFromPath("Assets/fonts/default.ttf") || !racy.isLoaded() ||
        !(racy.measureTextWidth("Ag", 24.0f) > 0.0f)) {
        std::cerr << "Font hardening lock failure: renderer did not recover after churn"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("TOCTOU churn: no crash, loaded renderer always measures");

    // 6. #41-kalinti: shaper basarisiz reload'da canli backend'i korur.
    if (Rowl::Text::TextShaper::isAdvancedBackendCompiled()) {
        Rowl::Text::TextShaper shaper;
        if (!shaper.loadFontFromMemory(validBytes.data(), validBytes.size()) ||
            !shaper.isAdvancedBackendActive()) {
            std::cerr << "Font hardening lock failure: shaper control load failed"
                      << std::endl;
            std::exit(1);
        }
        if (shaper.loadFontFromMemory(invalid.data(), invalid.size())) {
            std::cerr << "Font hardening lock failure: shaper invalid load succeeded"
                      << std::endl;
            std::exit(1);
        }
        if (!shaper.isAdvancedBackendActive()) {
            std::cerr << "Font hardening lock failure: failed shaper reload dropped "
                         "live backend"
                      << std::endl;
            std::exit(1);
        }
        TEST_PASS("Shaper failed reload keeps live backend active");
    } else {
        TEST_PASS("Shaper staging skipped (advanced backend not compiled)");
    }
}
