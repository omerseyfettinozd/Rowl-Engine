/**
 * test_font_hardening.cpp — A3-tur7 kilidi: font-yükleme fail-closed.
 *
 * Kilitlenen davranışlar (font_renderer.cpp):
 *  1. Geçerli paketlenmiş font yüklenir (kontrol — guard'lar meşru yolu kırmaz).
 *  2. Kayıp dosya / dizin false döner (throw yok, crash yok).
 *  3. Dev-dosya (64MB tavanı üstü, seyrek-fixture — diskte bayt değil,
 *     tellg'de 65MB) okunmadan reddedilir: resize-denemesi yok, false.
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/font_renderer.hpp"

namespace {

using Rowl::Render::FontRenderer;

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
}
