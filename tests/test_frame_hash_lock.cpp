/**
 * test_frame_hash_lock.cpp — A3 K2 kilidi: identical-frame skip kararının
 * paketlenmiş-içerik çekirdeği (Window::hashPackedFrameContent) golden-hash +
 * alan-duyarlılığıyla sabitlenir. Bölünme / render_data.hpp çıkarımı bu
 * kilidi kırmadan ilerleyemez (kör skip-davranış değişimi YOK).
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/window.hpp"

namespace {

using Rowl::Render::CharacterRenderData;
using Rowl::Render::ChoiceButtonRenderData;
using Rowl::Render::DialogueRenderData;
using Rowl::Render::Window;

struct CanonicalFrame {
    bool hasBackground = true;
    std::string background = "bg_beach_sunset.png";
    float bgX = 0.0f, bgY = 0.0f, bgW = 1920.0f, bgH = 1080.0f;
    std::vector<CharacterRenderData> characters;
    std::vector<DialogueRenderData> dialogues;
    std::vector<ChoiceButtonRenderData> choices;
    float bgRotation = 0.0f, bgParallaxX = 1.0f, bgParallaxY = 1.0f, bgOpacity = 1.0f;
};

CanonicalFrame makeCanonicalFrame() {
    CanonicalFrame frame;
    CharacterRenderData ch;
    ch.sprite = "hero_neutral.png";
    ch.x = 1440.0f; ch.y = 340.0f;
    frame.characters.push_back(ch);
    DialogueRenderData dlg;
    dlg.speaker = "Işıklı";
    dlg.dialogue = "Merhaba dünya — ğşı test ✓";
    dlg.typewriterEnabled = true;
    dlg.elapsedTypewriterTime = 0.5f;
    frame.dialogues.push_back(dlg);
    ChoiceButtonRenderData choice;
    choice.optionId = "opt_1";
    choice.text = "Devam et";
    frame.choices.push_back(choice);
    return frame;
}

uint64_t hashOf(const CanonicalFrame& frame) {
    return Window::hashPackedFrameContent(
        frame.hasBackground, frame.background,
        frame.bgX, frame.bgY, frame.bgW, frame.bgH,
        frame.characters, frame.dialogues, frame.choices,
        frame.bgRotation, frame.bgParallaxX, frame.bgParallaxY, frame.bgOpacity);
}

[[noreturn]] void frameHashFail(const std::string& message, uint64_t got, uint64_t want) {
    std::cerr << "Frame hash lock failure: " << message
              << " (got 0x" << std::hex << got
              << ", want 0x" << want << std::dec << ")" << std::endl;
    std::exit(1);
}

} // namespace

void test_frame_hash_lock() {
    TEST_SECTION("Identical-Frame Hash Lock (K2)");

    const CanonicalFrame frame = makeCanonicalFrame();
    const uint64_t first = hashOf(frame);
    if (hashOf(frame) != first) {
        frameHashFail("hash is not deterministic", hashOf(frame), first);
    }
    TEST_PASS("Frame hash is deterministic");

    // A3-otonomi notu: bu altın değer ilk yeşil koşudan alınır; değişimi
    // skip-davranış değişimi demektir ve K2 onayı gerektirir (kör güncelleme YOK).
    // Köken: 2026-09-18, hashPackedFrameContent saf-çekirdek ayrımı sonrası
    // ilk koşu (got 0xabd1694c10a96ef); kanonik frame: bg_beach_sunset +
    // hero_neutral(1440,340) + "Işıklı"/"Merhaba dünya — ğşı test ✓"
    // (typewriter 0.5sn) + opt_1/"Devam et".
    constexpr uint64_t kCanonicalGolden = 0xabd1694c10a96efULL;
    if (first != kCanonicalGolden) {
        frameHashFail("canonical golden mismatch", first, kCanonicalGolden);
    }
    TEST_PASS("Canonical frame golden hash pinned");

    auto expectFlip = [&](const char* field, CanonicalFrame mutated) {
        if (hashOf(mutated) == first) {
            frameHashFail(field, hashOf(mutated), first ^ 0xFFFFFFFFFFFFFFFFULL);
        }
    };
    {
        CanonicalFrame m = frame; m.background = "bg_night.png";
        expectFlip("background", m);
    }
    {
        CanonicalFrame m = frame; m.characters[0].x += 1.0f;
        expectFlip("character.x", m);
    }
    {
        CanonicalFrame m = frame; m.dialogues[0].dialogue = "Başka metin";
        expectFlip("dialogue text", m);
    }
    {
        CanonicalFrame m = frame; m.dialogues[0].elapsedTypewriterTime = 0.75f;
        expectFlip("typewriter reveal progress", m);
    }
    {
        CanonicalFrame m = frame; m.dialogues[0].typewriterEnabled = false;
        expectFlip("typewriterEnabled", m);
    }
    {
        CanonicalFrame m = frame; m.choices[0].text = "Bekle";
        expectFlip("choice text", m);
    }
    {
        CanonicalFrame m = frame; m.bgRotation = 15.0f;
        expectFlip("bgRotation", m);
    }
    {
        CanonicalFrame m = frame; m.hasBackground = false;
        expectFlip("hasBackground", m);
    }
    TEST_PASS("Every hashed field flips the digest (8/8)");
}
