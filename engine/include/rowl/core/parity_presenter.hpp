/**
 * rowl/core/parity_presenter.hpp
 *
 * Dilim-7 parity (P1-C): slot metadata + semantic-input + backlog icin
 * salt-okunur native sunum yardimcilari.
 *
 * Kapsam ve sinirlar (baglayici):
 * - Yalnizca SUNUM: mevcut C ABI'nin urettigi veriyi (GetDialogueHistoryJson
 *   anahtarlari: node_id/speaker/dialogue/read/content_id; ve
 *   GetSaveSlotMetadataJson anahtarlari: saved_at/playtime_seconds/
 *   chapter_title/summary/has_thumbnail) ekran satirina cevirir. Canli
 *   hikayeye DOKUNMAZ, dosya okumaz/yazmaz, side-effect uretmez (ozellikle
 *   history replay YOK — kayit yan etkileri asla yeniden calismaz).
 * - Mevcut pause/slot/F5-F9/rewind yollari TEKRAR YAZILMAZ: bu TU
 *   handleRuntimeInput/engine_pause_menu/c_api_state'e girmez; semantic map
 *   yalnizca saf esleme tablosudur, cagri noktasina baglanmaz (baglama
 *   sonraki dilimin host isidir).
 * - C ABI degisikligi YOK: yeni RowlEngine_ sembolu yok, capability biti yok.
 * - Tum yordamlar saf + noexcept-dostu (throw yok; bozuk JSON -> bos VE
 *   toleransli varsayilan). Kilit almaz.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Rowl::Parity {

// ── Backlog sunumu (GetDialogueHistoryJson uzerine) ─────────────────────────

// Sunum katmanina giren bir satir: C ABI History JSON'unun toleransli
// okunmus hali (eksik anahtar -> varsayilan; asiri content_id (>1024 B) ->
// "" — native decode kuralinin aynasi).
struct BacklogSourceLine {
    uint64_t nodeId = 0;
    std::string speaker;
    std::string dialogue;
    bool read = true;
    std::string contentId;
};

// Ekrana cikan satir: kisaltilmis metin + kesilme bayragi. Sira korunur;
// yalnizca son maxLines satir alinir (sinirli liste).
struct BacklogDisplayLine {
    uint64_t nodeId = 0;
    std::string speaker;
    std::string dialogue;
    bool read = true;
    bool truncated = false;
    std::string contentId;
};

/// Son maxLines satiri ekran formuna cevirir (sira korunur). Bos konusmacı
/// "—" olur; dialogue maxCharsPerLine skaleri asarsa "…" ile kisalir
/// (UTF-8 skaler sinirinda kesilir, ham bayt ortasinda degil).
std::vector<BacklogDisplayLine> BuildBacklogView(
    const std::vector<BacklogSourceLine>& entries,
    std::size_t maxLines = 100,
    std::size_t maxCharsPerLine = 280);

/// Mevcut C ABI History JSON'unu toleransli okur: dizi degilse/bozuksa bos
/// doner; satir basina eksik anahtar varsayilana duser; asiri content_id
/// fail-closed "" olur. Kaynak JSON'a dokunmaz.
std::vector<BacklogSourceLine> ParseHistoryJsonForDisplay(const std::string& json);

// ── Slot metadata sunumu (GetSaveSlotMetadataJson uzerine) ─────────────────

struct SlotMetadataFields {
    int32_t slot = 0;
    std::string savedAt;
    double playtimeSeconds = 0.0;
    std::string chapterId;
    std::string chapterTitle;
    std::string summary;
    bool hasThumbnail = false;
};

/// Mevcut C ABI slot-metadata JSON'unu toleransli okur (eksik anahtar ->
/// varsayilan; negatif/non-finite playtime -> 0; bozuk JSON -> varsayilan
/// alanlar + cagrilan slot numarasi). Dosya/slot IO YOK.
SlotMetadataFields ParseSlotMetadataJsonForDisplay(const std::string& json, int32_t slot);

/// Slot liste satiri: chapterTitle doluysa o, yoksa summary, ikisi de bossa
/// "Slot N". Salt metin; thumbnail cozumu view katmaninindir.
std::string FormatSlotDetail(const SlotMetadataFields& fields);

/// Oynanis suresi: 1 saat alti "MM:SS", ustu "H:MM:SS". Negatif/non-finite
/// -> "00:00".
std::string FormatPlaytime(double playtimeSeconds);

// ── Semantik girdi aynasi (PlayerInputMapper varsayilanlarinin native izdusu)
//
// Yoneticideki (managed) PlayerInputMapper::Reset tablosunun native aynasi.
// Saf esleme: eslesmeyen girdi nullopt doner (host eski daldan akar).
// BU DILIMDE BAGLANTI YOK: handleRuntimeInput aynen calisir; bu map'i
// cagiran host kodu sonraki dilimin isidir. Auto/skip global surucu bu
// dilimde native'de YOKTUR (tek kaynak: yonetilen gate'li akis) — KeyA/KeyS
// bilerek eslesmez (genel acma YOK).

enum class ParityKey {
    Space,
    Enter,
    Escape,
    KeyP,
    KeyB,
    KeyF5,
    KeyF9,
    Backspace,
    KeyZ,
    ArrowUp,
    ArrowDown,
    ArrowLeft,
    ArrowRight,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Unknown,
};

enum class ParityInputCommand {
    Advance,
    TogglePause,
    OpenBacklog,
    QuickSave,
    QuickLoad,
    RewindStep,
    SelectSlot,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
};

struct ParityInput {
    ParityInputCommand command = ParityInputCommand::Advance;
    // Yalnizca SelectSlot'ta anlamli (0-9); diger komutlarda -1.
    int32_t slot = -1;
};

/// Varsayilan esleme (yonetilen tabloyla birebir):
/// Space/Enter=Advance, Escape/P=TogglePause, B=OpenBacklog (duraklatilmis
/// degilse host karar verir), F5=QuickSave, F9=QuickLoad,
/// Backspace/Z=RewindStep (native-only geri sarim aynasi), 0-9=SelectSlot,
/// oklar=Menu nav. Unknown -> nullopt (host eski daldan akar). A/S/T/H
/// icin ParityKey degeri YOKTUR: native global auto/skip surucusu bu
/// dilimde kapali tutuldugu icin (genel acma YOK) bu tuslar bilerek
/// eslesemez; tek kaynak yonetilen gate'li akistir.
std::optional<ParityInput> MapParityKey(ParityKey key);

}  // namespace Rowl::Parity
