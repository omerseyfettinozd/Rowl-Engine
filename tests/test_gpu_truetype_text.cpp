/**
 * test_gpu_truetype_text.cpp — A11: GPU yolunda TrueType metin yedeği.
 *
 * Gerçek ekran denemesi (2026-10-20): standalone oyuncu MSDF atlası metni tam
 * karşılamayınca (varsayılan atlas yalnız ASCII) SDL_RenderDebugText'e düşüyor,
 * Türkçe "ş/ı/ğ" ve "—" kutu çıkıyordu. drawTrueTypeTextOnGpu aynı FontRenderer
 * ile metni dokuya çizer; bu test onu bir yazılım renderer'ında sınar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/render/font_renderer.hpp"
#include "rowl/render/gpu_truetype_text.hpp"

namespace {

[[noreturn]] void gpuTextFail(const std::string& message) {
    std::cerr << "GPU TrueType text: " << message << std::endl;
    exit(1);
}

// Opak siyah zemine beyaz metin: kaplanan piksellerin sayısı ve yatay yayılımı.
struct Coverage {
    int lit = 0;
    int minX = 1 << 30;
    int maxX = -1;
};

Coverage measure(SDL_Surface* surface) {
    Coverage c;
    const auto* bytes = static_cast<const uint8_t*>(surface->pixels);
    for (int y = 0; y < surface->h; ++y) {
        for (int x = 0; x < surface->w; ++x) {
            const uint8_t* p = bytes + y * surface->pitch + x * 4;
            if (p[0] > 96 || p[1] > 96 || p[2] > 96) {
                ++c.lit;
                c.minX = std::min(c.minX, x);
                c.maxX = std::max(c.maxX, x);
            }
        }
    }
    return c;
}

}  // namespace

void test_gpu_truetype_text() {
    TEST_SECTION("GPU TrueType Text Fallback (A11)");

    Rowl::Render::FontRenderer font;
    if (!font.loadFont("Assets/fonts/default.ttf") || !font.isLoaded())
        gpuTextFail("Assets/fonts/default.ttf did not load");

    SDL_Surface* surface = SDL_CreateSurface(640, 96, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) gpuTextFail("software surface failed");
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    if (renderer == nullptr) gpuTextFail("software renderer failed");

    const auto clear = [&] {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
    };

    // Türkçe + uzun tire: debug-bitmap yolunda kutu/eksik çıkan karakterler.
    const std::string turkish = "Işık sönüyor — çğıöşü ÇĞİÖŞÜ";
    clear();
    if (!Rowl::Render::drawTrueTypeTextOnGpu(renderer, &font, nullptr, turkish, 12.0f, 20.0f,
                                             32.0f, SDL_Color{255, 255, 255, 255}, 600.0f, 60.0f))
        gpuTextFail("Turkish text was not drawn");
    SDL_RenderPresent(renderer);
    const Coverage drawn = measure(surface);
    // 32 px'te ~29 karakter: metin geniş bir alanı kaplamalı. 8x8 debug-bitmap
    // aynı dizgeyi en fazla ~230 px'e sığdırırdı (ve Türkçe glifleri hiç çizmezdi).
    if (drawn.lit < 400) gpuTextFail("too few lit pixels: " + std::to_string(drawn.lit));
    if (drawn.maxX - drawn.minX < 300)
        gpuTextFail("text span too narrow for 32px TrueType: " +
                    std::to_string(drawn.maxX - drawn.minX));
    if (drawn.minX < 4 || drawn.minX > 30)
        gpuTextFail("text not placed at the requested x origin: " + std::to_string(drawn.minX));
    TEST_PASS("Turkish + em dash drawn through TrueType on the GPU path");

    // Daktilo sınırı: şekilli metinde görünür birimler kısıtlanınca daha az çizilir.
    const auto shaped = font.shapeTextShared(turkish, 32.0f, 600.0f);
    clear();
    if (!Rowl::Render::drawTrueTypeTextOnGpu(renderer, &font, shaped.get(), turkish, 12.0f, 20.0f,
                                             32.0f, SDL_Color{255, 255, 255, 255}, 600.0f, 60.0f,
                                             "Left", 4))
        gpuTextFail("partially revealed text was not drawn");
    SDL_RenderPresent(renderer);
    const Coverage partial = measure(surface);
    if (partial.lit <= 0 || partial.lit >= drawn.lit / 2)
        gpuTextFail("reveal limit ignored: " + std::to_string(partial.lit) + " of " +
                    std::to_string(drawn.lit));
    TEST_PASS("Typewriter reveal limit honoured on the GPU TrueType path");

    // Çizemiyorsa dokunmadan false (çağıran debug-bitmap'e düşebilsin).
    Rowl::Render::FontRenderer unloaded;
    clear();
    SDL_RenderPresent(renderer);
    if (Rowl::Render::drawTrueTypeTextOnGpu(renderer, &unloaded, nullptr, turkish, 12.0f, 20.0f,
                                            32.0f, SDL_Color{255, 255, 255, 255}, 600.0f, 60.0f))
        gpuTextFail("unloaded font reported success");
    if (Rowl::Render::drawTrueTypeTextOnGpu(renderer, &font, nullptr, "", 12.0f, 20.0f, 32.0f,
                                            SDL_Color{255, 255, 255, 255}, 600.0f, 60.0f))
        gpuTextFail("empty text reported success");
    if (measure(surface).lit != 0) gpuTextFail("a failed call touched the renderer");
    TEST_PASS("Fail-closed: unloaded font / empty text draw nothing and return false");

    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}
