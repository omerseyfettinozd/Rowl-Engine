/**
 * test_pause_overlay_visible_window.cpp — C02 BUG-2: Esc duraklatma menusu
 * gorunur (offscreen OLMAYAN, standalone) pencere yolunda cizilmiyordu;
 * renderPauseMenuOverlay offscreen yuzey yoksa hemen donuyordu. Yazilim
 * renderer'li gorunur pencerede dim + panel + metin pikselleri aranir.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/pause_menu.hpp"
#include "rowl/render/frame_composition.hpp"

namespace {

[[noreturn]] void pauseOverlayFail(const std::string& message) {
    std::cerr << "Pause overlay (visible window): " << message << std::endl;
    exit(1);
}

struct Shot {
    int bright = 0;   // metin: r,g,b > 120
    int panel = 0;    // panel zemini (koyu lacivert) sayisi
    bool dimmed = false;
};

Shot grab(Rowl::Render::Window& window) {
    Shot s;
    SDL_Surface* surf = SDL_RenderReadPixels(window.getRenderer(), nullptr);
    if (!surf) pauseOverlayFail(std::string("RenderReadPixels failed: ") + SDL_GetError());
    SDL_Surface* rgba = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surf);
    if (!rgba) pauseOverlayFail("surface convert failed");
    const auto* bytes = static_cast<const uint8_t*>(rgba->pixels);
    for (int y = 0; y < rgba->h; ++y) {
        for (int x = 0; x < rgba->w; ++x) {
            const uint8_t* p = bytes + y * rgba->pitch + x * 4;
            if (p[0] > 120 && p[1] > 120 && p[2] > 120) ++s.bright;
            if (p[0] < 40 && p[1] < 40 && p[2] > 15 && p[2] < 60) ++s.panel;
        }
    }
    const uint8_t* corner = bytes;  // panelin disi: beyaz zemin dim ile kararir
    s.dimmed = corner[0] < 200;
    SDL_DestroySurface(rgba);
    return s;
}

void drawOnce(Rowl::Render::Window& window, const Rowl::Core::PauseMenuView& view) {
    SDL_Renderer* r = window.getRenderer();
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);  // donmus hikaye karesi yerine
    SDL_RenderClear(r);
    window.renderPauseMenuOverlay(view);
}

}  // namespace

void test_pause_overlay_visible_window() {
    TEST_SECTION("Pause Overlay On Visible (Non-Offscreen) Window");

    Rowl::VFS::VFSManager vfs;
    vfs.remountProject(std::filesystem::current_path().string());
    Rowl::Render::Window window(&vfs);
    if (!window.initialize("pause-overlay", 960, 540, false))
        pauseOverlayFail("visible window could not be initialized");
    if (window.isOffscreen()) pauseOverlayFail("expected a non-offscreen window");
    window.reloadFonts();

    Rowl::Core::PauseMenuView view;
    view.open = true;
    view.title = "Paused";
    view.hint = "Esc to resume";
    for (const char* label : {"Resume", "Save Game", "Load Game"})
        view.rows.push_back({label, "", false});
    view.rows.push_back({"Music", "80%", true});
    view.selected = 1;

    drawOnce(window, view);
    Shot shot = grab(window);
    if (!shot.dimmed) pauseOverlayFail("frame was not dimmed (overlay not drawn)");
    if (shot.panel < 2000) pauseOverlayFail("panel missing: " + std::to_string(shot.panel));
    if (shot.bright < 150) pauseOverlayFail("menu text missing: " + std::to_string(shot.bright));
    TEST_PASS("dim + panel + text pixels present on the visible-window path");

    // Yeniden boyutlandirma: overlay yeni olcude yine cizilir.
    window.resizeViewport(640, 400);
    drawOnce(window, view);
    shot = grab(window);
    if (!shot.dimmed || shot.panel < 800 || shot.bright < 40)
        pauseOverlayFail("overlay missing after resize: panel=" + std::to_string(shot.panel) +
                         " bright=" + std::to_string(shot.bright));
    TEST_PASS("overlay redrawn after resize while paused");

    // Gorunur yolda kare onbellegi yeniden kullanilirsa donmus hikaye karesi
    // cizilmez, overlay ustune ustune dim basar (kararip siyaha gider).
    // Overlay acikken her kare hikayeyi tam yeniden cizmeli.
    Rowl::Render::ComposedFrame idle;
    idle.hasBackground = false;
    for (int i = 0; i < 3; ++i) {
        window.renderComposedFrame(idle);
        if (i > 0 && window.lastFrameReusedCache())
            pauseOverlayFail("frame cache reused under an open pause overlay (dim stacks)");
        window.renderPauseMenuOverlay(view);
        window.endFrame();
    }
    TEST_PASS("story frame is fully redrawn under the overlay every paused frame");
}
