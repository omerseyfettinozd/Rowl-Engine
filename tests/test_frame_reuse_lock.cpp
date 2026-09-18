/**
 * test_frame_reuse_lock.cpp — A3-tur5 D2 kilidi: renderVisualNovelFrame
 * identical-frame reuse sözleşmesi headless karakterizasyonla sabitlenir.
 *
 * Kilitlenen davranışlar (window.cpp:1255-1274 kanonik + :1632/:1646 forward):
 *  1. İlk render reuse DEĞİL (m_frameCacheValid boş başlar), sayaçlar işler.
 *  2. Birebir-aynı 2. çağrı erken-dönüş + 4 sayaç sıfırlama + reused=true.
 *  3. Dar overload'lar kanonikle aynı contentHash'i üretir (reuse-bayrağıyla
 *     kanıtlı — :1632 tek-diyalog + :1646 legacy, boş-ve-dolu içerik).
 *  4. Legacy overload'ın zımni alanları (typewriter=false vb.) struct
 *     varsayılanlarıyla birebir — kasıtlıysa belgelidir, değişim kilidi kırar.
 *  5. Hash-koruması: tint kapıdan geçer (dynamics-değil) ama hash yakalar —
 *     yanlış-reuse imkânsız, doğru-reuse tint'li karede de çalışır.
 *  6. Dynamics-bypass: aktif transition varken birebir içerik YENİDEN çizilir.
 *
 * Bölünme kuralı: bu test YEŞİL kalmadan window.cpp'den frame-reuse yolu
 * taşınamaz (kör skip-davranış değişimi YOK).
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/window.hpp"

namespace {

using Rowl::Render::CharacterRenderData;
using Rowl::Render::DialogueRenderData;
using Rowl::Render::Window;

const std::vector<CharacterRenderData> kNoCharacters;
const std::vector<DialogueRenderData> kNoDialogues;

void renderCanonicalEmpty(Window& window) {
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, kNoDialogues);
    window.endFrame();
}

DialogueRenderData makeDialogue() {
    DialogueRenderData dlg;
    dlg.hasDialogueBox = true;
    dlg.speaker = "Işıklı";
    dlg.dialogue = "Merhaba dünya";
    dlg.x = 80.0f;
    dlg.y = 860.0f;
    dlg.width = 1760.0f;
    dlg.height = 180.0f;
    return dlg;
}

void renderCanonicalDialogue(Window& window, const DialogueRenderData& dlg) {
    const std::vector<DialogueRenderData> dlgs{dlg};
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, dlgs);
    window.endFrame();
}

[[noreturn]] void reuseFail(const std::string& message) {
    std::cerr << "Frame reuse lock failure: " << message << std::endl;
    std::exit(1);
}

void expectReused(Window& window, bool want, const char* step) {
    if (window.lastFrameReusedCache() != want) {
        reuseFail(std::string(step) + (want ? ": identical frame did not reuse cache"
                                            : ": frame wrongly reused cache"));
    }
    TEST_PASS(step);
}

} // namespace

void test_frame_reuse_lock() {
    TEST_SECTION("Identical-Frame Reuse Lock (D2)");

    Window window;
    if (!window.initializeOffscreen(320, 180)) {
        reuseFail("could not initialize offscreen window");
    }

    // 1. İlk render: reuse yok.
    renderCanonicalEmpty(window);
    expectReused(window, false, "First frame renders (no reuse)");

    // 2. Birebir-aynı 2. çağrı: erken-dönüş + sayaç sıfırlama.
    renderCanonicalEmpty(window);
    expectReused(window, true, "Identical second frame reuses cache");
    if (window.getLastFrameTextureLoadMilliseconds() != 0.0 ||
        window.getLastFrameTextRasterizationMilliseconds() != 0.0 ||
        window.getLastFrameRendererFlushMilliseconds() != 0.0 ||
        window.getLastFrameNonTextureRenderMilliseconds() != 0.0) {
        reuseFail("reuse frame did not zero all four profile counters");
    }
    TEST_PASS("Reuse frame zeroes all four profile counters");

    // 3a. Dar overload (:1632), boş diyalog — kanonik-boş ile aynı hash.
    {
        DialogueRenderData emptyDlg;
        emptyDlg.hasDialogueBox = false;
        window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                      kNoCharacters, emptyDlg);
        window.endFrame();
    }
    expectReused(window, true, "Single-dialogue forward (empty) matches canonical hash");

    // 3b. Legacy overload (:1646), hasDialogueBox=false — boş vektörle kanoniğe.
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, false, "", "",
                                  0.0f, 0.0f, 0.0f, 0.0f);
    window.endFrame();
    expectReused(window, true, "Legacy forward (no box) matches canonical empty hash");

    // 3c. Dolu içerik: kanonik 1-diyalog render, sonra dar overload aynı içerik.
    const DialogueRenderData dlg = makeDialogue();
    renderCanonicalDialogue(window, dlg);
    expectReused(window, false, "New dialogue content renders (no reuse)");
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, dlg);
    window.endFrame();
    expectReused(window, true, "Single-dialogue forward (payload) matches canonical hash");

    // 4. Legacy zımni varsayılanlar struct varsayılanlarıyla birebir
    //    (typewriter=false açıkça; geri kalanı struct-default — kasıtlı).
    window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                  kNoCharacters, true, dlg.speaker, dlg.dialogue,
                                  dlg.x, dlg.y, dlg.width, dlg.height);
    window.endFrame();
    expectReused(window, true, "Legacy forward defaults match struct defaults");

    // 5. Hash-koruması: tint reuse-kapısından geçer ama hash'i değiştirir.
    window.setScreenTint(255, 0, 0, 0.5f);
    renderCanonicalDialogue(window, dlg);
    expectReused(window, false, "Tint change defeats hash (no false reuse)");
    renderCanonicalDialogue(window, dlg);
    expectReused(window, true, "Tinted identical frame reuses cache");
    window.clearScreenTint();

    // 6. Dynamics-bypass: aktif transition birebir içeriği yeniden çizdirir.
    //    Önce tintsiz karede reuse yeniden kurulur ki retint-hash farkı
    //    bypass-kanıtını gölgelemesin. (En sonda: transition testten sonra
    //    da aktif kalır.)
    renderCanonicalDialogue(window, dlg);
    expectReused(window, false, "Untinted frame renders after tint cleared");
    renderCanonicalDialogue(window, dlg);
    expectReused(window, true, "Untinted identical frame reuses cache");
    window.startTransition("fade", 30.0f, "#000000");
    if (!window.isTransitionActive()) {
        reuseFail("transition did not activate for dynamics-bypass probe");
    }
    renderCanonicalDialogue(window, dlg);
    expectReused(window, false, "Active transition forces re-render (dynamics bypass)");
}
