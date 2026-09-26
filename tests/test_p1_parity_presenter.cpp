// test_p1_parity_presenter.cpp — Dilim-7 parity (P1-C) saf sunum kilidi.
//
// Bagimsiz ikili: rowl_p1_parity_presenter (ctest -R p1_parity_presenter).
// Yalnizca public parity_presenter yili + C ABI JSON sekillerini kullanir;
// Engine/handle/pencere acmaz, pause/slot/rewind yollarina girmez.

#include "rowl/core/parity_presenter.hpp"

#include "rowl_test_harness.hpp"

#include <string>
#include <vector>

namespace {

using Rowl::Parity::BacklogSourceLine;
using Rowl::Parity::MapParityKey;
using Rowl::Parity::ParityInputCommand;
using Rowl::Parity::ParityKey;

[[noreturn]] void parityFail(const std::string& message) {
    rowlLockFail("p1-parity-presenter", message);
}

void expect(bool condition, const std::string& message) {
    if (!condition) parityFail(message);
}

}  // namespace

int main() {
    TEST_SECTION("P1-C parity presenter (slot/backlog/semantic, read-only)");

    // ── History JSON toleransli okuma (mevcut C ABI anahtarlari) ──
    {
        const std::string json = R"([
            {"node_id":101,"speaker":"Evelyn","dialogue":"Merhaba.","read":true,
             "content_id":"11111111-2222-3333-4444-555555555555"},
            {"node_id":102,"dialogue":"Secim bekliyor."},
            "yabanci-satir",
            {"node_id":-5,"speaker":"X","dialogue":"negatif-id","read":false,
             "content_id":"   "}
        ])";
        const auto lines = Rowl::Parity::ParseHistoryJsonForDisplay(json);
        expect(lines.size() == 3, "history: 3 obje satir okunmali (yabanci atilir)");
        expect(lines[0].nodeId == 101 && lines[0].speaker == "Evelyn", "history: satir-0 alanlari");
        expect(lines[0].read, "history: satir-0 read=true");
        expect(lines[0].contentId == "11111111-2222-3333-4444-555555555555",
               "history: satir-0 content_id aynen");
        expect(lines[1].speaker.empty() && lines[1].read, "history: eksik anahtar varsayilana duser");
        expect(lines[2].nodeId == 0 && !lines[2].read, "history: negatif id 0'lanir, read korunur");

        // Asiri content_id fail-closed "" (native decode aynasi, >1024 B).
        const std::string big(2000, 'a');
        const auto over = Rowl::Parity::ParseHistoryJsonForDisplay(
            "[{\"node_id\":1,\"speaker\":\"S\",\"dialogue\":\"D\",\"content_id\":\"" + big + "\"}]");
        expect(over.size() == 1 && over[0].contentId.empty(), "history: asiri content_id bosalir");

        expect(Rowl::Parity::ParseHistoryJsonForDisplay("bozuk-json").empty(),
               "history: bozuk JSON bos doner");
        expect(Rowl::Parity::ParseHistoryJsonForDisplay("{\"obje\":true}").empty(),
               "history: dizi-olmayan JSON bos doner");
    }
    TEST_PASS("History JSON toleransli okuma + fail-closed kurallari");

    // ── Backlog gorunumu: sinir + sira + kisaltma + konusmacı geri donusu ──
    {
        std::vector<BacklogSourceLine> entries;
        for (uint64_t i = 1; i <= 5; ++i)
            entries.push_back({i, "S" + std::to_string(i), "satir-" + std::to_string(i), true, ""});
        const auto view = Rowl::Parity::BuildBacklogView(entries, 3, 280);
        expect(view.size() == 3, "backlog: son maxLines satir alinir");
        expect(view[0].nodeId == 3 && view[2].nodeId == 5, "backlog: sira korunur");
        expect(!view[0].truncated, "backlog: kisa satir kesilmez");

        // Bos konusmacı + uzun satir (UTF-8 skaler sinirinda kesim).
        std::string emojiLine;
        for (int i = 0; i < 300; ++i) emojiLine += "ü";  // 2 bayt/skaler
        const auto single = Rowl::Parity::BuildBacklogView(
            {{7, "", emojiLine, false, "cid"}}, 100, 280);
        expect(single.size() == 1, "backlog: tek satir");
        expect(single[0].speaker == "—", "backlog: bos konusmacı geri donusu");
        expect(single[0].truncated, "backlog: uzun satir kesilme bayragi");
        expect(single[0].dialogue.size() == 280 * 2 + 3, "backlog: skaler sinirinda kesim + …");
        expect(!single[0].read && single[0].contentId == "cid", "backlog: read/content_id korunur");

        expect(Rowl::Parity::BuildBacklogView(entries, 0, 280).empty(), "backlog: maxLines=0 bos");
        expect(Rowl::Parity::BuildBacklogView({}, 100, 280).empty(), "backlog: bos girdi bos cikti");
    }
    TEST_PASS("Backlog gorunumu sinir/sira/kisaltma");

    // ── Slot metadata sunumu (mevcut C ABI anahtarlari, dosya IO YOK) ──
    {
        const std::string json = R"({"slot":2,"saved_at":"2026-09-26T10:00:00Z",)"
                                 R"("playtime_seconds":754,"chapter_id":"ch1",)"
                                 R"("chapter_title":"Ilk Isik","summary":"Evelyn sahilde.",)"
                                 R"("has_thumbnail":true})";
        const auto fields = Rowl::Parity::ParseSlotMetadataJsonForDisplay(json, 2);
        expect(fields.slot == 2, "slot: numarasi cagridan gelir");
        expect(fields.savedAt == "2026-09-26T10:00:00Z", "slot: saved_at");
        expect(fields.playtimeSeconds == 754.0, "slot: playtime");
        expect(Rowl::Parity::FormatSlotDetail(fields) == "Ilk Isik", "slot: chapter oncelikli detay");

        const auto noChapter = Rowl::Parity::ParseSlotMetadataJsonForDisplay(
            R"({"summary":"Yalnizca ozet."})", 4);
        expect(Rowl::Parity::FormatSlotDetail(noChapter) == "Yalnizca ozet.",
               "slot: chaptersiz detay summary'ye duser");
        const auto empty = Rowl::Parity::ParseSlotMetadataJsonForDisplay("bozuk", 0);
        expect(Rowl::Parity::FormatSlotDetail(empty) == "Slot 1", "slot: bos detay Slot N olur");
        expect(empty.playtimeSeconds == 0.0 && !empty.hasThumbnail, "slot: bozuk JSON varsayilanlar");

        const auto hostile = Rowl::Parity::ParseSlotMetadataJsonForDisplay(
            R"({"playtime_seconds":-30})", 1);
        expect(hostile.playtimeSeconds == 0.0, "slot: negatif playtime sifirlanir");

        expect(Rowl::Parity::FormatPlaytime(0) == "00:00", "playtime: sifir");
        expect(Rowl::Parity::FormatPlaytime(754) == "12:34", "playtime: MM:SS");
        expect(Rowl::Parity::FormatPlaytime(3723) == "1:02:03", "playtime: H:MM:SS");
        expect(Rowl::Parity::FormatPlaytime(-5) == "00:00", "playtime: negatif fail-closed");
    }
    TEST_PASS("Slot metadata sunumu + playtime formati");

    // ── Semantik girdi aynasi (yonetilen varsayilanlarla birebir) ──
    {
        expect(MapParityKey(ParityKey::Space)->command == ParityInputCommand::Advance,
               "input: Space=Advance");
        expect(MapParityKey(ParityKey::Enter)->command == ParityInputCommand::Advance,
               "input: Enter=Advance");
        expect(MapParityKey(ParityKey::Escape)->command == ParityInputCommand::TogglePause,
               "input: Esc=Pause");
        expect(MapParityKey(ParityKey::KeyP)->command == ParityInputCommand::TogglePause,
               "input: P=Pause");
        expect(MapParityKey(ParityKey::KeyB)->command == ParityInputCommand::OpenBacklog,
               "input: B=Backlog");
        expect(MapParityKey(ParityKey::KeyF5)->command == ParityInputCommand::QuickSave,
               "input: F5=QuickSave");
        expect(MapParityKey(ParityKey::KeyF9)->command == ParityInputCommand::QuickLoad,
               "input: F9=QuickLoad");
        expect(MapParityKey(ParityKey::Backspace)->command == ParityInputCommand::RewindStep,
               "input: Backspace=Rewind");
        expect(MapParityKey(ParityKey::KeyZ)->command == ParityInputCommand::RewindStep,
               "input: Z=Rewind (native-only aynasi)");
        expect(MapParityKey(ParityKey::ArrowUp)->command == ParityInputCommand::MenuUp,
               "input: oklar menu-nav");
        const auto slot = MapParityKey(ParityKey::Digit7);
        expect(slot && slot->command == ParityInputCommand::SelectSlot && slot->slot == 7,
               "input: 7=SelectSlot(7)");
        expect(!MapParityKey(ParityKey::Unknown).has_value(), "input: bilinmeyen eslesmez");
        // A/S/T/H icin ParityKey yoktur (genel acma YOK) — harita bunlari
        // uretemez; host eski daldan akar.
    }
    TEST_PASS("Semantik girdi varsayilan eslemesi");

    std::cout << "P1-C parity presenter probe: GREEN" << std::endl;
    return 0;
}
