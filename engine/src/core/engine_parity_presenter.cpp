// engine_parity_presenter.cpp — Dilim-7 parity (P1-C) salt-okunur sunum.
//
// parity_presenter.hpp'teki sozlesmenin implementasyonu. Saf TU: Engine,
// Window, VFS, dosya IO ve C ABI'ye girmez; pause/slot/F5-F9/rewind
// yollarina dokunmaz. Kilit almaz, throw atmaz (JSON bozuklugu dahil her
// yabanci girdi toleransli varsayilana duser).

#include "rowl/core/parity_presenter.hpp"

#include <cmath>
#include <cstdio>

#include "nlohmann/json.hpp"
#include "rowl/text/utf8.hpp"

namespace Rowl::Parity {
namespace {

// UTF-8 skaler sayimi (paylasimli strict cozucu; bozuk dizi U+FFFD + >=1
// bayt tuketimle ilerler, dongu asla takilmaz).
std::size_t countScalars(const std::string& text) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const Rowl::Text::Utf8Scalar scalar = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (scalar.length == 0) break;
        i += scalar.length;
        ++count;
    }
    return count;
}

// En fazla maxChars skaleri skaler sinirinda tutar; kesildiyse true doner.
std::string truncateScalars(const std::string& text, std::size_t maxChars, bool& cut) {
    cut = false;
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const Rowl::Text::Utf8Scalar scalar = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (scalar.length == 0) break;
        if (count >= maxChars) {
            cut = true;
            break;
        }
        i += scalar.length;
        ++count;
    }
    if (i >= text.size()) return text;
    cut = true;
    return text.substr(0, i);
}

std::string readString(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it != object.end() && it->is_string()) return it->get<std::string>();
    return {};
}

bool readBool(const nlohmann::json& object, const char* key, bool fallback) {
    const auto it = object.find(key);
    if (it != object.end() && it->is_boolean()) return it->get<bool>();
    return fallback;
}

uint64_t readNodeId(const nlohmann::json& object) {
    const auto it = object.find("node_id");
    if (it != object.end() && it->is_number_unsigned()) return it->get<uint64_t>();
    if (it != object.end() && it->is_number_integer() && it->get<int64_t>() >= 0)
        return static_cast<uint64_t>(it->get<int64_t>());
    return 0;
}

double sanitizePlaytime(double value) {
    if (!std::isfinite(value) || value < 0.0) return 0.0;
    return value;
}

}  // namespace

std::vector<BacklogDisplayLine> BuildBacklogView(
    const std::vector<BacklogSourceLine>& entries,
    std::size_t maxLines,
    std::size_t maxCharsPerLine) {
    std::vector<BacklogDisplayLine> out;
    if (entries.empty() || maxLines == 0) return out;
    const std::size_t start = entries.size() > maxLines ? entries.size() - maxLines : 0;
    out.reserve(entries.size() - start);
    for (std::size_t k = start; k < entries.size(); ++k) {
        const BacklogSourceLine& source = entries[k];
        BacklogDisplayLine line;
        line.nodeId = source.nodeId;
        line.speaker = source.speaker.empty() ? "—" : source.speaker;
        line.read = source.read;
        line.contentId = source.contentId;
        bool cut = false;
        if (countScalars(source.dialogue) > maxCharsPerLine) {
            line.dialogue = truncateScalars(source.dialogue, maxCharsPerLine, cut) + "…";
            line.truncated = true;
        } else {
            line.dialogue = source.dialogue;
            line.truncated = false;
        }
        (void)cut;
        out.push_back(std::move(line));
    }
    return out;
}

std::vector<BacklogSourceLine> ParseHistoryJsonForDisplay(const std::string& json) {
    std::vector<BacklogSourceLine> out;
    nlohmann::json parsed = nlohmann::json::array();
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return out;
    }
    if (!parsed.is_array()) return out;
    for (const auto& item : parsed) {
        if (!item.is_object()) continue;
        BacklogSourceLine line;
        line.nodeId = readNodeId(item);
        line.speaker = readString(item, "speaker");
        line.dialogue = readString(item, "dialogue");
        line.read = readBool(item, "read", true);
        line.contentId = readString(item, "content_id");
        // Native decode kuralinin aynasi: asiri content_id fail-closed "".
        if (line.contentId.size() > 1024) line.contentId.clear();
        out.push_back(std::move(line));
    }
    return out;
}

SlotMetadataFields ParseSlotMetadataJsonForDisplay(const std::string& json, int32_t slot) {
    SlotMetadataFields fields;
    fields.slot = slot;
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(json);
    } catch (...) {
        return fields;
    }
    if (!parsed.is_object()) return fields;
    fields.savedAt = readString(parsed, "saved_at");
    fields.chapterId = readString(parsed, "chapter_id");
    fields.chapterTitle = readString(parsed, "chapter_title");
    fields.summary = readString(parsed, "summary");
    const auto playIt = parsed.find("playtime_seconds");
    if (playIt != parsed.end() && playIt->is_number())
        fields.playtimeSeconds = sanitizePlaytime(playIt->get<double>());
    fields.hasThumbnail = readBool(parsed, "has_thumbnail", false);
    return fields;
}

std::string FormatSlotDetail(const SlotMetadataFields& fields) {
    if (!fields.chapterTitle.empty()) return fields.chapterTitle;
    if (!fields.summary.empty()) return fields.summary;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Slot %d", static_cast<int>(fields.slot) + 1);
    return {buf};
}

std::string FormatPlaytime(double playtimeSeconds) {
    const double clean = sanitizePlaytime(playtimeSeconds);
    const long long total = static_cast<long long>(clean);
    const long long hours = total / 3600;
    const long long minutes = (total % 3600) / 60;
    const long long seconds = total % 60;
    char buf[32];
    if (hours > 0)
        std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", hours, minutes, seconds);
    else
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld", minutes, seconds);
    return {buf};
}

std::optional<ParityInput> MapParityKey(ParityKey key) {
    switch (key) {
        case ParityKey::Space:
        case ParityKey::Enter:
            return ParityInput{ParityInputCommand::Advance, -1};
        case ParityKey::Escape:
        case ParityKey::KeyP:
            return ParityInput{ParityInputCommand::TogglePause, -1};
        case ParityKey::KeyB:
            return ParityInput{ParityInputCommand::OpenBacklog, -1};
        case ParityKey::KeyF5:
            return ParityInput{ParityInputCommand::QuickSave, -1};
        case ParityKey::KeyF9:
            return ParityInput{ParityInputCommand::QuickLoad, -1};
        case ParityKey::Backspace:
        case ParityKey::KeyZ:
            return ParityInput{ParityInputCommand::RewindStep, -1};
        case ParityKey::ArrowUp:
            return ParityInput{ParityInputCommand::MenuUp, -1};
        case ParityKey::ArrowDown:
            return ParityInput{ParityInputCommand::MenuDown, -1};
        case ParityKey::ArrowLeft:
            return ParityInput{ParityInputCommand::MenuLeft, -1};
        case ParityKey::ArrowRight:
            return ParityInput{ParityInputCommand::MenuRight, -1};
        case ParityKey::Digit0:
        case ParityKey::Digit1:
        case ParityKey::Digit2:
        case ParityKey::Digit3:
        case ParityKey::Digit4:
        case ParityKey::Digit5:
        case ParityKey::Digit6:
        case ParityKey::Digit7:
        case ParityKey::Digit8:
        case ParityKey::Digit9:
            return ParityInput{
                ParityInputCommand::SelectSlot,
                static_cast<int32_t>(key) - static_cast<int32_t>(ParityKey::Digit0)};
        case ParityKey::Unknown:
            return std::nullopt;
    }
    // Tum enum degerleri yukarida karsilandi; burasi yalnizca derleyici
    // "kontrol sonuna ulasti" uyarisina karsi fail-closed bekcidir.
    // NOT: A/S/T/H icin enum degeri YOKTUR — native global auto/skip
    // surucusu bu dilimde kapali tutuldugu icin (genel acma YOK) bu tuslar
    // bilerek eslesmez; host eski daldan akar.
    return std::nullopt;
}

}  // namespace Rowl::Parity
