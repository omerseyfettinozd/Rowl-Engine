/**
 * test_utf8_decoder.cpp — A3-tur4 (metin turu) K2 kilidi: paylasimli strict
 * UTF-8 decoder (rowl/text/utf8.hpp) sozlesmesi + uc bagli decoder'in
 * (getNextCodepoint, decodeScalar-uzerinden-shape, path'li font yukleme)
 * davranisi. Gecerli girdi kimligi + gecersiz girdi -> U+FFFD burada
 * sabitlenir; sonraki metin-turu/bölünme degisiklikleri bu kilidi
 * kirmadan ilerleyemez.
 */
#include "rowl_test_harness.hpp"
#include "rowl/text/utf8.hpp"
#include "rowl/text/text_shaper.hpp"
#include "rowl/render/font_renderer.hpp"

#include <filesystem>

namespace {

[[noreturn]] void utf8Fail(const std::string& message) {
    std::cerr << "UTF-8 decoder failure: " << message << std::endl;
    std::exit(1);
}

struct ScalarCase {
    const char* bytes;
    std::size_t size;
    uint32_t codepoint;
    std::size_t length;
    bool valid;
};

void checkScalar(const ScalarCase& c, const char* name) {
    const Rowl::Text::Utf8Scalar got = Rowl::Text::decodeUtf8Scalar(
        c.bytes, c.bytes + c.size);
    if (got.codepoint != c.codepoint || got.length != c.length ||
        got.valid != c.valid) {
        utf8Fail(std::string("scalar case '") + name + "' mismatch");
    }
}

}  // namespace

void test_utf8_decoder() {
    using Rowl::Text::decodeUtf8Scalar;
    TEST_SECTION("UTF-8 strict decoder contract (A3-tur4)");

    // Gecerli: ASCII + 2/3/4-bayt + sinir degerleri.
    checkScalar({"A", 1, 0x41u, 1, true}, "ascii");
    checkScalar({"\xC3\xA9", 2, 0xE9u, 2, true}, "e-acute");
    checkScalar({"\xE2\x82\xAC", 3, 0x20ACu, 3, true}, "euro");
    checkScalar({"\xF0\x9D\x8E\x98", 4, 0x1D398u, 4, true}, "u1d398");
    checkScalar({"\x7F", 1, 0x7Fu, 1, true}, "del");
    checkScalar({"\xC2\x80", 2, 0x80u, 2, true}, "min-2byte");
    checkScalar({"\xF4\x8F\xBF\xBF", 4, 0x10FFFFu, 4, true}, "max-scalar");

    // Gecersiz: basibos continuation / 0xF8+ lead -> FFFD + 1 bayt.
    checkScalar({"\x80", 1, 0xFFFDu, 1, false}, "stray-continuation");
    checkScalar({"\xBF", 1, 0xFFFDu, 1, false}, "stray-bf");
    checkScalar({"\xF8", 1, 0xFFFDu, 1, false}, "lead-f8");
    // Bozuk continuation ortada -> 1 bayt tuketim, cagiran devam eder.
    checkScalar({"\xE2\x28\xA1", 3, 0xFFFDu, 1, false}, "bad-continuation");
    // Overlong / surrogate / aralik-disi -> FFFD.
    checkScalar({"\xC0\xAF", 2, 0xFFFDu, 1, false}, "overlong-2");
    checkScalar({"\xE0\x80\x80", 3, 0xFFFDu, 1, false}, "overlong-3");
    checkScalar({"\xED\xA0\x80", 3, 0xFFFDu, 1, false}, "surrogate");
    checkScalar({"\xF4\x90\x80\x80", 4, 0xFFFDu, 1, false}, "beyond-max");
    // Sonda kesik: kalanin tamami tek FFFD.
    checkScalar({"\xE2\x82", 2, 0xFFFDu, 2, false}, "truncated-3");
    checkScalar({"\xF0\x9D", 2, 0xFFFDu, 2, false}, "truncated-4");
    {
        const Rowl::Text::Utf8Scalar empty =
            decodeUtf8Scalar(std::string_view{});
        if (empty.valid || empty.length != 0)
            utf8Fail("empty input must be invalid with zero length");
    }
    TEST_PASS("decodeUtf8Scalar strict contract (valid + invalid)");

    // Akis: getNextCodepoint gecerli metinde kimlik + ilerleme.
    {
        const std::string text = "A\xC3\xA9\xE2\x82\xAC\xF0\x9D\x8E\x98";
        const uint32_t want[] = {0x41u, 0xE9u, 0x20ACu, 0x1D398u};
        const std::size_t wantIndex[] = {1, 3, 6, 10};
        std::size_t index = 0;
        for (int k = 0; k < 4; ++k) {
            const uint32_t got =
                Rowl::Render::FontRenderer::getNextCodepoint(text, index);
            if (got != want[k] || index != wantIndex[k])
                utf8Fail("getNextCodepoint valid stream mismatch");
        }
        if (Rowl::Render::FontRenderer::getNextCodepoint(text, index) != 0)
            utf8Fail("getNextCodepoint must return 0 at end");
    }
    // Akis: bozuk bayt FFFD olur, akis 1'er bayt ilerler, metin yutulmaz.
    {
        const std::string text("\x80X\xC0\xAFY", 5);
        std::size_t index = 0;
        using Rowl::Render::FontRenderer;
        if (FontRenderer::getNextCodepoint(text, index) != 0xFFFDu || index != 1)
            utf8Fail("stray byte must decode to U+FFFD consuming 1");
        if (FontRenderer::getNextCodepoint(text, index) != uint32_t('X') ||
            index != 2)
            utf8Fail("stream must resume after invalid byte");
        if (FontRenderer::getNextCodepoint(text, index) != 0xFFFDu || index != 3)
            utf8Fail("overlong lead must decode to U+FFFD consuming 1");
        // Overlong devam bayti (AF) ayri basibos continuation olarak yeniden
        // taranir: C0 AF iki FFFD uretir (strict sozlesme: tuketim 1 bayt).
        if (FontRenderer::getNextCodepoint(text, index) != 0xFFFDu || index != 4)
            utf8Fail("overlong continuation must rescan as stray U+FFFD");
        if (FontRenderer::getNextCodepoint(text, index) != uint32_t('Y') ||
            index != 5)
            utf8Fail("stream must resume after overlong");
        if (FontRenderer::countCodepoints(text) != 5)
            utf8Fail("countCodepoints must count each invalid byte once");
    }
    TEST_PASS("getNextCodepoint stream identity + invalid progression");

    // decodeScalar uzerinden shape: fontsuz (stb yedek) + ham gecersiz
    // girdi crash'siz FFFD reveal-uretir (backend-bagimsiz).
    {
        Rowl::Text::TextShaper shaper;
        Rowl::Text::ShapeOptions options;
        options.fontSize = 24.0f;
        const std::string bad("\xFF\xFE", 2);
        const Rowl::Text::ShapedText shaped = shaper.shapeMarkup(bad, options);
        if (shaped.revealUnits.size() != 2)
            utf8Fail("invalid bytes must yield one reveal unit each");
        for (const auto& unit : shaped.revealUnits) {
            if (unit.representativeCodepoint != 0xFFFDu)
                utf8Fail("invalid input representative must be U+FFFD");
        }
    }
    TEST_PASS("shapeMarkup invalid-input crash-freedom + U+FFFD reveal");

    // Yol-nesneli font yukleme: gecerli dosya + kayip dosya (throw yok).
    {
        Rowl::Render::FontRenderer renderer;
        if (!renderer.loadFontFromPath(std::filesystem::path("Assets/fonts/default.ttf")) ||
            !renderer.isLoaded())
            utf8Fail("path-based loadFontFromPath must load default.ttf");
        Rowl::Render::FontRenderer missing;
        if (missing.loadFontFromPath(std::filesystem::path(
                "Assets/fonts/does-not-exist-zzz.ttf")) ||
            missing.isLoaded())
            utf8Fail("path-based loadFontFromPath must fail closed on missing file");
    }
    TEST_PASS("loadFontFromPath success + fail-closed missing file");
}
