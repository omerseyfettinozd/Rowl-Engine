/**
 * test_cache_hygiene.cpp — A3-tur6 kilidi: negatif-önbellek hijyeni.
 *
 * Kilitlenen davranışlar:
 *  1. Kayıp doku negatif-kümede (tekrarlı miss büyütmez, 2. çağrı da null).
 *  2. Remount-unpoison: dosya mount'landıktan SONRA bile miss zehirli kalır
 *     (invalidateMissingCaches öncesi null) — invalidate sonrası yüklenir.
 *     Bu sıra, review-bulgusu #2'nin (remount'ta temizlenmeyen missing-cache)
 *     davranış-kanıtıdır.
 *  3. Bozuk buton-fontu negatif-kümede: iki kare de fail-closed (default font),
 *     küme 1'de sabitlenir (kare-başı VFS+parse tekrarı yok).
 *  4. invalidateMissingCaches iki kinen kümeyi de sıfırlar (pozitiflere
 *     dokunmaz — pozitif doku aynı testte sağ kalır).
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/window.hpp"

namespace {

using Rowl::Render::ChoiceButtonRenderData;
using Rowl::Render::Window;

const std::vector<Rowl::Render::CharacterRenderData> kNoCharacters;
const std::vector<Rowl::Render::DialogueRenderData> kNoDialogues;

// 2x2 24-bit BMP (54 bayt başlık + 16 bayt piksel; satırlar 4'e dolgulu).
// stb_image BMP çözer; el-yapımı fixture, dosyaya bağımlılık yok.
std::vector<uint8_t> makeProbeBmp() {
    std::vector<uint8_t> bmp(54, 0);
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = 70;
    bmp[10] = 54;   // piksel-ofseti
    bmp[14] = 40;   // DIB başlık boyu
    bmp[18] = 2; bmp[19] = 0;   // genişlik
    bmp[22] = 2; bmp[23] = 0;   // yükseklik
    bmp[26] = 1;                // düzlem
    bmp[28] = 24;               // bit-derinliği
    for (int i = 0; i < 16; ++i) bmp.push_back(static_cast<uint8_t>(0x80 + i));
    return bmp;
}

void renderChoices(Window& window, const std::vector<ChoiceButtonRenderData>& choices) {
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, kNoDialogues, choices);
    window.endFrame();
}

ChoiceButtonRenderData makeChoice(const std::string& fontFamily) {
    ChoiceButtonRenderData choice;
    choice.optionId = "opt_hygiene";
    choice.text = "Hijyen";
    choice.fontFamily = fontFamily;
    choice.x = 100.0f; choice.y = 900.0f;
    choice.width = 400.0f; choice.height = 80.0f;
    return choice;
}

} // namespace

void test_cache_hygiene() {
    TEST_SECTION("Negative-Cache Hygiene Lock (A3-tur6)");

    Window window;
    if (!window.initializeOffscreen(320, 180)) {
        rowlLockFail("Cache hygiene", "could not initialize offscreen window");
    }

    // 1. Kayıp doku: null + negatif-küme 1; tekrar büyütmez.
    if (window.loadTexture("nope/missing_tur6_probe.png") != nullptr) {
        rowlLockFail("Cache hygiene", "missing texture unexpectedly loaded");
    }
    if (window.getNegativeTextureCacheSize() != 1) {
        rowlLockFail("Cache hygiene", "missing texture was not remembered");
    }
    if (window.loadTexture("nope/missing_tur6_probe.png") != nullptr ||
        window.getNegativeTextureCacheSize() != 1) {
        rowlLockFail("Cache hygiene", "repeated miss grew the negative set");
    }
    TEST_PASS("Missing texture is remembered (no per-frame retry state growth)");

    // 2. Remount-unpoison: dosya belirir ama hüküm bayat — invalidate çözer.
    const auto probeDir =
        std::filesystem::temp_directory_path() / "rowl_tur6_hygiene";
    std::error_code ec;
    std::filesystem::remove_all(probeDir, ec);
    std::filesystem::create_directories(probeDir / "images", ec);
    if (ec) {
        rowlLockFail("Cache hygiene", "could not stage probe directory");
    }
    {
        std::ofstream out(probeDir / "images" / "probe_tur6.bmp", std::ios::binary);
        const auto bmp = makeProbeBmp();
        out.write(reinterpret_cast<const char*>(bmp.data()),
                  static_cast<std::streamsize>(bmp.size()));
    }
    if (window.loadTexture("images/probe_tur6.bmp") != nullptr) {
        rowlLockFail("Cache hygiene", "unstaged probe unexpectedly loaded");
    }
    const size_t poisonedSize = window.getNegativeTextureCacheSize();
    window.getVfs()->mountDirectory("images", (probeDir / "images").string());
    // Mount sonrası ama invalidate öncesi: hâlâ null (zehir kanıtı).
    if (window.loadTexture("images/probe_tur6.bmp") != nullptr) {
        rowlLockFail("Cache hygiene", "stale negative verdict did not hold after mount");
    }
    if (window.getNegativeTextureCacheSize() != poisonedSize) {
        rowlLockFail("Cache hygiene", "mounted-but-stale load mutated the negative set");
    }
    window.invalidateMissingCaches();
    if (window.getNegativeTextureCacheSize() != 0) {
        rowlLockFail("Cache hygiene", "invalidateMissingCaches did not clear the texture set");
    }
    SDL_Texture* probe = window.loadTexture("images/probe_tur6.bmp");
    if (probe == nullptr) {
        rowlLockFail("Cache hygiene", "probe did not load after invalidation");
    }
    TEST_PASS("Remount-unpoison: stale miss holds, invalidateMissingCaches heals");

    // 3. Bozuk font: iki kare de default'la çizilir, küme 1'de sabit.
    renderChoices(window, {makeChoice("nope/missing_font_tur6.ttf")});
    if (window.getMissingFontCacheSize() != 1) {
        rowlLockFail("Cache hygiene", "missing font was not remembered");
    }
    renderChoices(window, {makeChoice("nope/missing_font_tur6.ttf")});
    if (window.getMissingFontCacheSize() != 1) {
        rowlLockFail("Cache hygiene", "repeated font miss grew the negative set");
    }
    TEST_PASS("Missing button font fails closed to default (negative set stable)");

    // 4. invalidateMissingCaches: negatifler sıfır, pozitif doku sağ.
    window.invalidateMissingCaches();
    if (window.getMissingFontCacheSize() != 0) {
        rowlLockFail("Cache hygiene", "invalidateMissingCaches did not clear the font set");
    }
    if (window.loadTexture("images/probe_tur6.bmp") != probe) {
        rowlLockFail("Cache hygiene", "invalidateMissingCaches disturbed the positive cache");
    }
    TEST_PASS("invalidateMissingCaches clears negatives only (positive survives)");

    std::filesystem::remove_all(probeDir, ec);
}
