#include "rowl_test_harness.hpp"
#include "rowl/text/text_shaper.hpp"

#include <nlohmann/json.hpp>

namespace {

[[noreturn]] void shapingFail(const std::string& message) {
    std::cerr << "Text shaping failure: " << message << std::endl;
    std::exit(1);
}

std::vector<uint8_t> loadTestFont() {
    std::ifstream stream("Assets/fonts/default.ttf", std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

Rowl::Text::ShapeOptions shapeOptions(float fontSize, float maxWidth = 0.0f) {
    Rowl::Text::ShapeOptions options;
    options.fontSize = fontSize;
    options.maxWidth = maxWidth;
    return options;
}

} // namespace

void test_text_shaping() {
    using namespace Rowl::Text;
    const auto font = loadTestFont();
    if (font.empty()) shapingFail("test font is missing");

    TextShaper shaper;
    if (!TextShaper::isAdvancedBackendCompiled()) {
        const auto fallback = shaper.shapeMarkup(
            "e\xCC\x81 \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB",
            shapeOptions(28.0f));
        if (fallback.backend != ShapingBackend::StbFallback ||
            fallback.revealUnits.size() != 3 ||
            fallback.revealUnits[0].scalarCount != 2 ||
            fallback.revealUnits[2].scalarCount != 3)
            shapingFail("minimal fallback split an atomic reveal group");
        uint32_t required = 0;
        if (RowlEngine_ShapeMarkup(
                "fallback", font.data(), static_cast<uint32_t>(font.size()),
                24.0f, 0.0f, nullptr, nullptr, 0, &required) != ROWL_RESULT_OK)
            shapingFail("minimal fallback C ABI is unavailable");
        TEST_PASS("stb text fallback build and grapheme-safe reveal contract");
        return;
    }
    if (!shaper.loadFontFromMemory(font.data(), font.size()) ||
        !shaper.isAdvancedBackendActive())
        shapingFail("FreeType/HarfBuzz font initialization failed");

    {
        const auto shaped = shaper.shapeMarkup(
            "e\xCC\x81 \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB",
            shapeOptions(28.0f));
        if (shaped.backend != ShapingBackend::HarfBuzzFriBidi ||
            shaped.revealUnits.size() != 3 ||
            shaped.revealUnits[0].scalarCount != 2 ||
            shaped.revealUnits[2].scalarCount != 3)
            shapingFail("UAX #29 combining/ZWJ reveal grouping is not atomic");
        for (const auto& glyph : shaped.glyphs) {
            if (glyph.logicalScalar < 2 && glyph.revealIndex != 0)
                shapingFail("combining glyph escaped its reveal cluster");
        }
    }

    {
        const auto shaped = shaper.shapeMarkup("LTR \xD7\x90\xD7\x91\xD7\x92 end",
                                               shapeOptions(24.0f));
        bool descendingRtlPair = false;
        for (std::size_t i = 1; i < shaped.glyphs.size(); ++i) {
            const auto left = shaped.glyphs[i - 1].logicalScalar;
            const auto right = shaped.glyphs[i].logicalScalar;
            if (left >= 4 && left <= 6 && right >= 4 && right <= 6 && left > right)
                descendingRtlPair = true;
        }
        if (!descendingRtlPair)
            shapingFail("FriBidi did not produce a visual-order RTL run");
    }

    {
        // Arabic lam + alef is two graphemes but an Arabic-capable font
        // shapes it as one ligature cluster; reveal grouping must follow
        // that HB cluster. Assets/fonts/default.ttf (Liberation Sans) has
        // no Arabic glyphs, so resolve a system font that does and skip
        // gracefully when none is available.
        const char* arabicFontCandidates[] = {
            "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf",
            "/usr/share/fonts/noto/NotoNaskhArabic-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
        };
        bool ligatureVerified = false;
        for (const char* candidate : arabicFontCandidates) {
            std::ifstream candidateStream(candidate, std::ios::binary);
            if (!candidateStream) continue;
            const std::vector<uint8_t> candidateFont(
                std::istreambuf_iterator<char>(candidateStream), {});
            if (candidateFont.empty()) continue;
            TextShaper candidateShaper;
            if (!candidateShaper.loadFontFromMemory(
                    candidateFont.data(), candidateFont.size()) ||
                !candidateShaper.isAdvancedBackendActive())
                continue;
            const auto shaped = candidateShaper.shapeMarkup(
                "\xD9\x84\xD8\xA7", shapeOptions(28.0f));
            if (shaped.glyphs.size() != 1) continue;
            if (shaped.revealUnits.size() != 1 ||
                shaped.revealUnits.front().scalarCount != 2)
                shapingFail("HarfBuzz ligature cluster was split during reveal: glyphs=" +
                    std::to_string(shaped.glyphs.size()) + " reveals=" +
                    std::to_string(shaped.revealUnits.size()));
            ligatureVerified = true;
            break;
        }
        if (!ligatureVerified)
            std::cout << "  SKIP Arabic ligature check: "
                         "no Arabic-capable system font produced a ligature"
                      << std::endl;
    }

    {
        // Hermetic ligature-shape contract (T0b): with the BUNDLED test font
        // (no Arabic glyphs — tofu path) lam+alef must still shape into a
        // well-formed reveal structure on ANY backend: every scalar covered
        // exactly once, every glyph pointing at a live reveal unit. This runs
        // everywhere the system-font probe above skips.
        const auto tofu = shaper.shapeMarkup(
            "\xD9\x84\xD8\xA7", shapeOptions(28.0f));
        if (tofu.revealUnits.empty())
            shapingFail("bundled-font lam+alef produced zero reveal units");
        size_t coveredScalars = 0;
        for (const auto& unit : tofu.revealUnits) {
            if (unit.scalarCount == 0)
                shapingFail("bundled-font lam+alef has an empty reveal unit");
            coveredScalars += unit.scalarCount;
        }
        if (coveredScalars != 2)
            shapingFail("bundled-font lam+alef reveal coverage != 2 scalars: " +
                std::to_string(coveredScalars));
        for (const auto& glyph : tofu.glyphs) {
            if (glyph.revealIndex >= tofu.revealUnits.size())
                shapingFail("bundled-font lam+alef glyph escaped its reveal cluster");
        }
    }

    {
        const auto unwrapped = shaper.shapeMarkup("one two three four",
                                                  shapeOptions(24.0f));
        const auto wrapped = shaper.shapeMarkup("one two three four",
            shapeOptions(24.0f, unwrapped.width * 0.45f));
        if (wrapped.lines.size() < 2 || wrapped.width > unwrapped.width)
            shapingFail("libunibreak wrapping did not create bounded lines");
    }

    {
        const auto shaped = shaper.shapeMarkup(
            "A<pause=0.5><speed=2>B</speed>C<pause=0.25>",
            shapeOptions(24.0f));
        if (shaped.revealUnits.size() != 3 ||
            std::fabs(shaped.revealUnits[1].pauseBefore - 0.5f) > 1e-5f ||
            std::fabs(shaped.revealUnits[1].speed - 2.0f) > 1e-5f)
            shapingFail("markup timing did not attach to shaped reveal units");
        const auto early = evaluateReveal(shaped, 0.11, 100.0);
        const auto duringPause = evaluateReveal(shaped, 0.55, 100.0);
        const auto complete = evaluateReveal(shaped, 1.0, 100.0);
        if (early.visibleUnits != 1 || duringPause.visibleUnits != 1 ||
            !complete.complete || complete.visibleUnits != 3)
            shapingFail("pause/speed/trailing-pause timeline is inconsistent");
    }

    {
        Rowl::Text::RasterizedShapedGlyph bitmap;
        const auto shaped = shaper.shapeMarkup("A", shapeOptions(32.0f));
        if (shaped.glyphs.empty() ||
            !shaper.rasterizeGlyph(shaped.glyphs.front().glyphIndex, 32.0f, bitmap) ||
            bitmap.width <= 0 || bitmap.height <= 0 || bitmap.bitmap.empty())
            shapingFail("FreeType glyph rasterization failed");
    }

    {
        uint64_t capabilities = 0;
        if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
            !(capabilities & ROWL_ENGINE_CAPABILITY_TEXT_SHAPING))
            shapingFail("TEXT_SHAPING capability is absent");
        uint32_t required = 0;
        const auto query = RowlEngine_ShapeMarkup(
            "A<b>fi</b>", font.data(), static_cast<uint32_t>(font.size()),
            24.0f, 200.0f, "en", nullptr, 0, &required);
        if (query != ROWL_RESULT_OK || required < 3)
            shapingFail("C ABI size query failed");
        if (RowlEngine_ShapeMarkup(
                "x", font.data(), static_cast<uint32_t>(font.size()),
                std::numeric_limits<float>::quiet_NaN(), 0.0f, nullptr,
                nullptr, 0, &required) != ROWL_RESULT_INVALID_ARGUMENT ||
            RowlEngine_ShapeMarkup(
                "x", reinterpret_cast<const uint8_t*>("bad"), 3,
                24.0f, 0.0f, nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_VALIDATION_ERROR)
            shapingFail("C ABI hostile size/font validation failed");
        std::vector<char> buffer(required);
        if (RowlEngine_ShapeMarkup(
                "A<b>fi</b>", font.data(), static_cast<uint32_t>(font.size()),
                24.0f, 200.0f, "en", buffer.data(), required, &required) != ROWL_RESULT_OK)
            shapingFail("C ABI exact buffer call failed");
        const auto json = nlohmann::json::parse(buffer.data());
        if (json.at("backend") != "harfbuzz_freetype_fribidi_unibreak" ||
            json.at("plain_text") != "Afi" || json.at("glyphs").empty())
            shapingFail("C ABI JSON schema/content mismatch");
    }

    TEST_PASS("FreeType + HarfBuzz + FriBidi + libunibreak shaping/reveal/C ABI");
}
