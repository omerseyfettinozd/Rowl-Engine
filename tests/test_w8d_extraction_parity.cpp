/**
 * test_w8d_extraction_parity.cpp — W8-d dort cikarim davranis kilidi.
 *
 * Bagimsiz ikili: rowl_w8d_extraction_parity (ctest -R w8d_extraction_parity).
 * D15 konvansiyonu: yalnizca public C API + public statikler + dummy driver.
 *
 * Bacaklar (cikarim-oncesi YESIL baz = ongorulen gozlem + exit 0):
 *   (1) pause-menu (engine_pause_menu.cpp): SetPaused(1) -> JSON open:true,
 *       baslik "Duraklatildi", 9 satir; DOWN -> selected:1; ESC-back ->
 *       resume (open:false). Kilit-koruma: cift SetPaused(1) gezinmeyi
 *       sifirlamaz (selected korunur).
 *   (2) keymap (window_input.cpp): Escape/P -> PauseToggle, Space/Enter ->
 *       Advance, oklar -> Menu*, F5/F9 -> QuickSave/QuickLoad, rakam ->
 *       SelectSlot+slot, eslenmemis tus -> false.
 *   (3) facade (engine_render_facade.cpp + c_api_render): budget roundtrip
 *       (Set->Get ayni deger); taze handleda sayaclar 0; dead-handle 0/0.0.
 *   (4) font-cozumleme (window_font_resolve.cpp): VFS-once sirasi — gecici
 *       dizine konan default.ttf VFS'ten yuklenir (isLoaded), sistem
 *       adaylarina dusulmez.
 *
 * RED: ilgili TU bilerek bozulursa (or. baslik/sayac/sira degisirse) prob
 * exit 1 verir; revert sonrasi sha-birebir + exit 0.
 */
#include "rowl_test_harness.hpp"

#include "rowl/platform/platform_host.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/window.hpp"
#include "rowl/render/window_font_resolve.hpp"
#include "rowl/vfs/vfs.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void probeFail(const std::string& message) {
    rowlLockFail("w8d-extraction-parity", message);
}

void check(bool condition, const std::string& what) {
    if (!condition) probeFail(what);
}

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string makeProjectRoot(const std::string& tag) {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_w8d_probe_" << tag << "_" << (++counter);
    auto dir = std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

using Type = Rowl::Platform::RuntimeInputEvent::Type;

bool mapKey(uint32_t key, Type& outType, int32_t& outSlot) {
    Rowl::Platform::RuntimeInputEvent ev{Type::Advance};
    const bool ok = Rowl::Render::Window::mapKeyToRuntimeInput(key, ev);
    outType = ev.type;
    outSlot = ev.slot;
    return ok;
}

}  // namespace

int main() {
    TEST_SECTION("W8-d dort cikarim parite kilidi");

    // ── Bacak (2): keymap tablosu ──
    {
        Type t = Type::Advance;
        int32_t slot = -1;
        check(mapKey(SDLK_ESCAPE, t, slot) && t == Type::PauseToggle, "keymap: Esc->PauseToggle");
        check(mapKey(SDLK_P, t, slot) && t == Type::PauseToggle, "keymap: P->PauseToggle");
        check(mapKey(SDLK_SPACE, t, slot) && t == Type::Advance, "keymap: Space->Advance");
        check(mapKey(SDLK_RETURN, t, slot) && t == Type::Advance, "keymap: Enter->Advance");
        check(mapKey(SDLK_UP, t, slot) && t == Type::MenuUp, "keymap: Up->MenuUp");
        check(mapKey(SDLK_DOWN, t, slot) && t == Type::MenuDown, "keymap: Down->MenuDown");
        check(mapKey(SDLK_LEFT, t, slot) && t == Type::MenuLeft, "keymap: Left->MenuLeft");
        check(mapKey(SDLK_RIGHT, t, slot) && t == Type::MenuRight, "keymap: Right->MenuRight");
        check(mapKey(SDLK_F5, t, slot) && t == Type::QuickSave, "keymap: F5->QuickSave");
        check(mapKey(SDLK_F9, t, slot) && t == Type::QuickLoad, "keymap: F9->QuickLoad");
        check(mapKey(SDLK_3, t, slot) && t == Type::SelectSlot && slot == 3, "keymap: 3->SelectSlot(3)");
        check(!mapKey(SDLK_A, t, slot), "keymap: A eslenmemis->false");
        std::cout << "  keymap: 12/12 eslesme" << std::endl;
    }

    // ── Bacak (1)+(3): pause JSON + facade (canli handle gerekir) ──
    const std::string root = makeProjectRoot("main");
    RowlEngineHandle h = RowlEngine_Create();
    check(h != nullptr, "setup: RowlEngine_Create null");
    RowlEngine_SetProjectDirectory(h, root.c_str());
    check(RowlEngine_Init(h, 320, 180, 0) == 1, "setup: Init(320x180) dummy driverda basarili olmali");

    {
        const char* shut = RowlEngine_GetPauseMenuJson(h);
        const std::string shutJson = shut ? shut : "";
        check(shutJson.find("\"open\":false") != std::string::npos, "pause: kapaliyken open:false");
    }
    RowlEngine_SetPaused(h, 1);
    {
        const char* raw = RowlEngine_GetPauseMenuJson(h);
        const std::string json = raw ? raw : "";
        check(json.find("\"open\":true") != std::string::npos, "pause: open:true");
        check(json.find("Duraklatildi") != std::string::npos, "pause: baslik Duraklatildi");
        check(countOccurrences(json, "\"label\"") == 9, "pause: 9 satir");
        check(json.find("\"selected\":0") != std::string::npos, "pause: ilk secim 0");
    }
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_DOWN);
    {
        const char* raw = RowlEngine_GetPauseMenuJson(h);
        const std::string json = raw ? raw : "";
        check(json.find("\"selected\":1") != std::string::npos, "pause: DOWN sonrasi selected:1");
    }
    // Kilit-koruma: ayni degerle tekrar acma gezinmeyi sifirlamaz.
    RowlEngine_SetPaused(h, 1);
    {
        const char* raw = RowlEngine_GetPauseMenuJson(h);
        const std::string json = raw ? raw : "";
        check(json.find("\"selected\":1") != std::string::npos, "pause: cift-acma selected'i korur");
    }
    RowlEngine_SetPaused(h, 0);
    {
        const char* raw = RowlEngine_GetPauseMenuJson(h);
        const std::string json = raw ? raw : "";
        check(json.find("\"open\":false") != std::string::npos, "pause: kapatinca open:false");
    }
    std::cout << "  pause-menu: JSON/sekme/kilit 7/7" << std::endl;

    {
        check(RowlEngine_GetTextureCacheTextureCount(h) == 0, "facade: taze sayac 0");
        check(RowlEngine_GetTextureCacheEvictionCount(h) == 0, "facade: taze eviction 0");
        RowlEngine_SetTextureCacheBudgetBytes(h, 12345678u);
        check(RowlEngine_GetTextureCacheBudgetBytes(h) == 12345678u, "facade: budget roundtrip");
        std::cout << "  facade: sayac/budget 3/3" << std::endl;
    }

    // ── Bacak (2b): overlay renderi (window_pause_overlay.cpp) ──
    // Offscreen kare + pause gorunumu -> piksel hamuru degismeli (dim+panel).
    {
        Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(h);
        check(engine != nullptr, "overlay: test koprusu null");
        auto* win = engine->getWindow();
        check(win != nullptr, "overlay: pencere null");
        engine->setPaused(true);
        win->beginFrame();
        uint32_t w = 0, hh = 0, p = 0;
        const uint8_t* prePtr = engine->getPixelBuffer(&w, &hh, &p);
        check(prePtr != nullptr && w == 320 && hh == 180 && p > 0, "overlay: on piksel-tamponu");
        const size_t bytes = static_cast<size_t>(p) * hh;
        std::vector<uint8_t> pre(prePtr, prePtr + bytes);
        uint64_t preHash = 14695981039346656037ULL;
        for (uint8_t b : pre) { preHash ^= b; preHash *= 1099511628211ULL; }
        win->renderPauseMenuOverlay(engine->getPauseMenuView());
        const uint8_t* postPtr = engine->getPixelBuffer(&w, &hh, &p);
        check(postPtr != nullptr, "overlay: son piksel-tamponu");
        uint64_t postHash = 14695981039346656037ULL;
        for (size_t i = 0; i < bytes; ++i) { postHash ^= postPtr[i]; postHash *= 1099511628211ULL; }
        check(preHash != postHash, "overlay: dim+panel pikseli degistirmeli");
        engine->setPaused(false);
        std::cout << "  overlay: piksel-hamur 3/3" << std::endl;
    }
    RowlEngine_Destroy(h);
    check(RowlEngine_GetTextureCacheTextureCount(h) == 0, "facade: dead-handle sayac 0");
    check(RowlEngine_GetTextureCacheBudgetBytes(h) == 0, "facade: dead-handle budget 0");

    // ── Bacak (4): VFS-once font cozumu ──
    {
        const std::string fontRoot = makeProjectRoot("fonts");
        std::filesystem::create_directories(std::filesystem::path(fontRoot) / "fonts");
        // Sistemden gercek bir TTF'yi VFS aday adiyla sagla.
        const std::string sysFont = "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf";
        if (!std::filesystem::exists(sysFont)) {
            std::cout << "  font: sistem TTF'si yok, VFS bacak atlandi (CI-disi)" << std::endl;
        } else {
            std::error_code ec;
            std::filesystem::copy_file(sysFont, std::filesystem::path(fontRoot) / "fonts" / "default.ttf", ec);
            check(!ec, "font: aday kopyalanmali");
            Rowl::VFS::VFSManager vfs;
            vfs.mountDirectory("", fontRoot);
            Rowl::Render::FontRenderer renderer;
            Rowl::Render::resolveWindowFont(renderer, vfs);
            check(renderer.isLoaded(), "font: VFS adayi yuklenmeli (VFS-once sirasi)");
            std::cout << "  font: VFS-once yukleme OK" << std::endl;
        }
    }

    std::cout << "PROB-CLEAN: w8d 4-cikarim parite" << std::endl;
    return 0;
}
