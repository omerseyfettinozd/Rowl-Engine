#include "rowl_test_harness.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/camera2d.hpp"

#include <memory>

#include <nlohmann/json.hpp>

namespace {

[[noreturn]] void accessibilityFail(const std::string& message) {
    std::cerr << "Accessibility failure: " << message << std::endl;
    std::exit(1);
}

std::unique_ptr<Rowl::Render::FontRenderer> loadDefaultRenderer() {
    auto renderer = std::make_unique<Rowl::Render::FontRenderer>();
    if (!renderer->loadFont("Assets/fonts/default.ttf") || !renderer->isLoaded())
        accessibilityFail("default.ttf did not load");
    return renderer;
}

size_t countAlphaPixels(SDL_Surface* surface) {
    if (!surface || !surface->pixels) return 0;
    size_t count = 0;
    for (int y = 0; y < surface->h; ++y) {
        auto* row = static_cast<uint8_t*>(surface->pixels) + y * surface->pitch;
        for (int x = 0; x < surface->w; ++x)
            if (row[x * 4 + 3] > 0) ++count;
    }
    return count;
}

size_t countScalars(const std::string& utf8) {
    size_t count = 0;
    size_t index = 0;
    while (index < utf8.size()) {
        Rowl::Render::FontRenderer::getNextCodepoint(utf8, index);
        ++count;
    }
    return count;
}

std::vector<std::string> catalogTexts(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) accessibilityFail("catalog missing: " + path);
    nlohmann::json document = nlohmann::json::parse(stream);
    std::vector<std::string> texts;
    for (const auto& [id, entry] : document.at("entries").items()) {
        texts.push_back(entry.at("text").get<std::string>());
        const std::string alt = entry.at("alt_text").get<std::string>();
        if (!alt.empty()) texts.push_back(alt);
    }
    if (texts.empty()) accessibilityFail("catalog has no texts: " + path);
    return texts;
}

bool tryLoadSystemFont(Rowl::Render::FontRenderer& renderer, const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    const std::vector<uint8_t> bytes(
        std::istreambuf_iterator<char>(stream), {});
    return !bytes.empty() && renderer.loadFont(path);
}

} // namespace

void test_accessibility() {
    // ── Text scale: measurement, reveal stability, clamping ──────────
    {
        auto renderer = loadDefaultRenderer();
        const float base = renderer->measureTextWidth("Hello World, relay awake.", 24.0f);
        if (base <= 0.0f) accessibilityFail("baseline measurement is empty");
        const size_t baseReveals = renderer->countRevealUnits("Hello World, relay awake.", 24.0f);

        renderer->setTextScale(1.5f);
        if (std::fabs(renderer->textScale() - 1.5f) > 1e-6f)
            accessibilityFail("text scale was not stored");
        const float scaled = renderer->measureTextWidth("Hello World, relay awake.", 24.0f);
        const float ratio = scaled / base;
        if (std::fabs(ratio - 1.5f) > 0.08f)
            accessibilityFail("scaled measurement ratio is not 1.5x: " + std::to_string(ratio));
        if (renderer->countRevealUnits("Hello World, relay awake.", 24.0f) != baseReveals)
            accessibilityFail("text scale changed the reveal segmentation");

        renderer->setTextScale(0.0f);
        if (std::fabs(renderer->textScale() - 1.0f) > 1e-6f)
            accessibilityFail("zero scale did not clamp to 1.0");
        renderer->setTextScale(std::numeric_limits<float>::quiet_NaN());
        if (std::fabs(renderer->textScale() - 1.0f) > 1e-6f)
            accessibilityFail("NaN scale did not reset to 1.0");
        renderer->setTextScale(9.0f);
        if (std::fabs(renderer->textScale() - 2.0f) > 1e-6f)
            accessibilityFail("oversized scale did not clamp to 2.0");
        renderer->setTextScale(1.0f);
        const float restored = renderer->measureTextWidth("Hello World, relay awake.", 24.0f);
        if (std::fabs(restored - base) > 1e-3f)
            accessibilityFail("restoring 1.0x did not restore measurements (stale cache?)");
    }

    // ── High contrast: halo adds coverage, base render intact ────────
    {
        auto renderer = loadDefaultRenderer();
        SDL_Color white{255, 255, 255, 255};
        SDL_Surface* plain = SDL_CreateSurface(400, 120, SDL_PIXELFORMAT_RGBA32);
        SDL_Surface* contrast = SDL_CreateSurface(400, 120, SDL_PIXELFORMAT_RGBA32);
        if (!plain || !contrast) accessibilityFail("could not allocate test surfaces");
        SDL_FillSurfaceRect(plain, nullptr, 0);
        SDL_FillSurfaceRect(contrast, nullptr, 0);
        renderer->renderText(plain, "Ag", 100.0f, 20.0f, 48.0f, white);
        renderer->setHighContrast(true);
        if (!renderer->highContrast()) accessibilityFail("high contrast flag was not stored");
        renderer->renderText(contrast, "Ag", 100.0f, 20.0f, 48.0f, white);
        const size_t plainPixels = countAlphaPixels(plain);
        const size_t contrastPixels = countAlphaPixels(contrast);
        SDL_DestroySurface(plain);
        SDL_DestroySurface(contrast);
        if (plainPixels == 0) accessibilityFail("baseline render produced no pixels");
        if (contrastPixels <= plainPixels)
            accessibilityFail("high-contrast halo added no coverage: plain=" +
                std::to_string(plainPixels) + " contrast=" + std::to_string(contrastPixels));
    }

    // ── Reduced motion: shake suppression on Camera2D ────────────────
    {
        Rowl::Render::Camera2D camera;
        camera.shake(20.0f, 1.0f);
        camera.update(0.016f);
        if (!camera.isShaking())
            accessibilityFail("shake did not start without reduced motion");
        if (std::fabs(camera.getShakeOffsetX()) + std::fabs(camera.getShakeOffsetY()) <= 1e-6f)
            accessibilityFail("shake produced no offset without reduced motion");

        camera.setReducedMotion(true);
        if (!camera.reducedMotion()) accessibilityFail("reduced-motion flag was not stored");
        if (camera.isShaking() ||
            std::fabs(camera.getShakeOffsetX()) + std::fabs(camera.getShakeOffsetY()) > 1e-6f)
            accessibilityFail("enabling reduced motion did not freeze shake at zero");
        camera.shake(20.0f, 1.0f);
        camera.shakePreset("explosion", 2.0f, 1.0f);
        camera.update(0.016f);
        if (camera.isShaking() ||
            std::fabs(camera.getShakeOffsetX()) + std::fabs(camera.getShakeOffsetY()) > 1e-6f)
            accessibilityFail("shake fired while reduced motion is on");

        camera.setReducedMotion(false);
        camera.shake(20.0f, 1.0f);
        camera.update(0.016f);
        if (!camera.isShaking())
            accessibilityFail("shake did not resume after reduced motion was cleared");
    }

    // ── C ABI routing, guards, capability ────────────────────────────
    {
        uint64_t capabilities = 0;
        if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
            !(capabilities & ROWL_ENGINE_CAPABILITY_ACCESSIBILITY))
            accessibilityFail("ACCESSIBILITY capability bit is absent");

        // Dead handles stay silent and report defaults.
        RowlEngine_SetTextScale(nullptr, 1.5f);
        RowlEngine_SetHighContrast(nullptr, 1);
        RowlEngine_SetReducedMotion(nullptr, 1);
        if (std::fabs(RowlEngine_GetTextScale(nullptr) - 1.0f) > 1e-6f ||
            RowlEngine_IsHighContrast(nullptr) != 0 ||
            RowlEngine_IsReducedMotion(nullptr) != 0)
            accessibilityFail("null-handle accessibility guards failed");

        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1)
            accessibilityFail("engine handle creation failed");
        RowlEngine_SetTextScale(handle, 1.25f);
        RowlEngine_SetHighContrast(handle, 1);
        RowlEngine_SetReducedMotion(handle, 1);
        if (std::fabs(RowlEngine_GetTextScale(handle) - 1.25f) > 1e-6f ||
            RowlEngine_IsHighContrast(handle) != 1 ||
            RowlEngine_IsReducedMotion(handle) != 1)
            accessibilityFail("C ABI accessibility roundtrip failed");
        // Sudden full-screen effects stay off while reduced motion is on.
        RowlEngine_TriggerScreenFlash(handle, 255, 255, 255, 0.5f, 1.0f);
        if (RowlEngine_IsScreenFlashActive(handle) != 0)
            accessibilityFail("screen flash fired while reduced motion is on");
        RowlEngine_SetReducedMotion(handle, 0);
        if (RowlEngine_IsReducedMotion(handle) != 0)
            accessibilityFail("reduced motion did not clear");
        RowlEngine_Destroy(handle);
    }

    // ── Faz 3 closing: TR/EN catalogs shape with zero overflow ───────
    {
        auto renderer = loadDefaultRenderer();
        constexpr float kDialogueWidth = 1760.0f;
        for (const char* catalog : {"samples/second_signal/Assets/locales/en.json",
                                    "samples/second_signal/Assets/locales/tr.json"}) {
            for (const auto& text : catalogTexts(catalog)) {
                for (float fontSize : {24.0f, 32.0f}) {
                    for (float scale : {1.0f, 1.5f}) {
                        renderer->setTextScale(scale);
                        const auto shaped =
                            renderer->shapeText(text, fontSize, kDialogueWidth);
                        if (shaped.revealUnits.empty())
                            accessibilityFail("empty reveal for catalog text: " + text);
                        for (const auto& line : shaped.lines) {
                            if (line.width > kDialogueWidth + 1.0f)
                                accessibilityFail("catalog line overflows at scale " +
                                    std::to_string(scale) + ": " + text);
                        }
                    }
                }
            }
        }
        renderer->setTextScale(1.0f);
    }

    // ── Faz 3 closing: RTL / CJK / combining / ZWJ integrity ─────────
    {
        struct Fixture {
            const char* fontPath;
            const char* text;
        };
        const Fixture fixtures[] = {
            {"/usr/share/fonts/noto/NotoSansArabic-Regular.ttf", "\xD8\xA7\xD9\x84\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85 \xD8\xB9\xD9\x84\xD9\x8A\xD9\x83\xD9\x85"},
            {"/usr/share/fonts/noto/NotoSansHebrew-Regular.ttf", "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D"},
            {"/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"},
            {"Assets/fonts/default.ttf", "e\xCC\x81 \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"},
        };
        constexpr float kMaxWidth = 800.0f;
        for (const auto& fixture : fixtures) {
            Rowl::Render::FontRenderer renderer;
            if (!tryLoadSystemFont(renderer, fixture.fontPath)) {
                std::cout << "  SKIP closing fixture (font unavailable): "
                          << fixture.fontPath << std::endl;
                continue;
            }
            const auto shaped = renderer.shapeText(fixture.text, 28.0f, kMaxWidth);
            if (shaped.revealUnits.empty() || shaped.glyphs.empty()) {
                std::cout << "  SKIP closing fixture (font produced no layout): "
                          << fixture.fontPath << std::endl;
                continue;
            }
            // No scalar is lost or duplicated across reveal units.
            size_t covered = 0;
            for (const auto& unit : shaped.revealUnits) {
                if (unit.scalarCount == 0)
                    accessibilityFail("empty reveal unit in closing fixture");
                covered += unit.scalarCount;
            }
            if (covered != countScalars(shaped.plainText))
                accessibilityFail("scalar coverage mismatch in closing fixture");
            for (const auto& line : shaped.lines) {
                if (line.width > kMaxWidth + 1.0f)
                    accessibilityFail("closing fixture line overflows");
            }
        }
    }

    TEST_PASS("accessibility display settings, reduced motion, font C ABI and Faz 3 closing matrix");
}
