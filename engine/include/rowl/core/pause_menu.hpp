#pragma once

#include "rowl/core/player_ui_strings.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Rowl::Core {

/// MS-6 pause-menu contract shared by the engine state machine (hit-testing),
/// the SDL window (overlay rendering), and the C API (tests/hosts).
/// All geometry is in the 1920x1080 virtual canvas; hosts map physical input
/// to virtual coordinates before hit-testing.

enum class PauseMenuCommand : int32_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    Back = 4,
    Confirm = 5
};

enum class PauseMenuMode : int32_t {
    Main = 0,
    SaveSlots = 1,
    LoadSlots = 2,
    /// D02: native title screen (root page) and its preferences sub-page.
    Title = 3,
    Preferences = 4,
    /// D03-2: dialogue backlog (Gecmis). Opened from the pause main page or
    /// straight from the story (B / wheel up); the game stays frozen.
    Backlog = 5
};

/// Player quick-save slots selectable at runtime (digits 0-9 / --slot N).
inline constexpr int32_t kPauseMenuQuickSlotMin = 0;
inline constexpr int32_t kPauseMenuQuickSlotMax = 9;

struct PauseMenuRow {
    std::string label;
    std::string value;
    bool isValue = false; // value rows adjust with Left/Right
    /// D02: small dim second line (slot date / chapter / summary); empty keeps
    /// the row drawing byte-identical to the pre-D02 menu.
    std::string detail;
    /// D02: false draws the row dim and makes Confirm on it a no-op.
    bool enabled = true;
};

/// D02: per-slot metadata memo shared by the slot pages and the title screen.
struct PauseSlotInfo {
    enum class State : uint8_t { Empty = 0, Valid = 1, Corrupt = 2 };
    State state = State::Empty;
    std::string savedAt;       // ISO-8601 UTC ("saved_at")
    std::string chapterTitle;
    std::string summary;
    double playtimeSeconds = 0.0;
    std::shared_ptr<const std::string> thumbnailPng;
};

/// D03-2: one visible backlog entry. `y`/`h` are virtual-canvas pixels; `lines`
/// are the dialogue text already wrapped (and capped) by the engine, so
/// hit-testing and drawing agree on the geometry.
struct PauseBacklogEntryView {
    int index = 0;            // 0 = oldest history row
    float y = 0.0f;
    float h = 0.0f;
    std::string speaker;
    std::string dialogue;     // full text (JSON); `lines` is what is drawn
    std::vector<std::string> lines;
    bool hasVoice = false;
    bool selected = false;
};

struct PauseBacklogView {
    bool active = false;
    int total = 0;            // history length
    int selected = -1;        // history index, -1 when empty
    int scroll = 0;           // history index of the first visible entry
    std::string counter;      // "12 / 40"
    std::string voiceMark;    // "(ses)" / "(voice)": entries that carry a voice line
    std::vector<PauseBacklogEntryView> entries;
};

struct PauseMenuView {
    bool open = false;
    std::string title;
    std::vector<PauseMenuRow> rows;
    int selected = 0;
    std::string hint;
    /// D02: title screen active / newest valid slot for Continue (-1 none).
    bool titleActive = false;
    int continueSlot = -1;
    /// D02: thumbnail of the selected slot on a slot page (null = none);
    /// `thumbnailKey` ("slot|saved_at") identifies it for texture caching.
    std::shared_ptr<const std::string> thumbnailPng;
    std::string thumbnailKey;
    /// D03-2: backlog page content (active only in PauseMenuMode::Backlog).
    PauseBacklogView backlog;
};

/// "55%" / "1.25x" row values (engine_pause_menu.cpp), shared with D02 pages.
std::string pauseMenuPercent(float volume);
std::string pauseMenuSpeedText(float multiplier);
/// D04-2: "2.0 sn" / "2.0 s" (one decimal) auto-advance delay row value.
std::string pauseMenuDelayText(float seconds, UiLang lang);
/// D02: "<YYYY-MM-DD HH:MM>  <chapter>  <summary ~48 bytes>" of a valid slot,
/// empty otherwise (engine_title_menu.cpp).
std::string pauseSlotDetail(const PauseSlotInfo& info);
/// D02: "2026-10-24T12:34:56Z" -> "2026-10-24 12:34".
std::string pauseSlotDateText(const std::string& savedAt);

/// Single source of truth for overlay layout. Row i occupies
/// [kRowY0 + i*(kRowH+kRowGap), +kRowH) at full kRowX..kRowX+kRowW width.
/// Value rows split into a minus zone (x < kMinusX1) and a plus zone
/// (x > kPlusX0); the middle band only selects.
struct PauseMenuLayout {
    static constexpr float kPanelX = 520.0f;
    static constexpr float kPanelY = 110.0f;
    static constexpr float kPanelW = 880.0f;
    static constexpr float kPanelH = 860.0f;
    static constexpr float kRowX = 580.0f;
    static constexpr float kRowW = 760.0f;
    // D03-2: 11 rows must clear the hint line at panel bottom; the first row
    // still starts below the slot-page thumbnail box.
    static constexpr float kRowY0 = 270.0f;
    static constexpr float kRowH = 54.0f;
    static constexpr float kRowGap = 4.0f;
    /// D05-2: a page with more than kComfortRows rows (Preferences, 12) uses a
    /// compact pitch that starts higher (no thumbnail on those pages): the 12th
    /// row's bottom edge stays above the hint line (y = kPanelY + kPanelH - 44).
    static constexpr int kComfortRows = 11;
    static constexpr float kCompactRowY0 = 250.0f;
    static constexpr float kCompactRowH = 50.0f;
    static constexpr float kCompactRowGap = 4.0f;
    static constexpr float kMinusX1 = 880.0f;
    static constexpr float kPlusX0 = 1040.0f;
    /// D05-2: pause main page. Resume 0, Save 1, Load 2, Preferences 3 (opens
    /// the shared Preferences page; Back returns here), History 4, Main Menu 5,
    /// Quit 6. The mixer / speed rows live on the Preferences page only.
    static constexpr int kMainRows = 7;
    static constexpr int kMainPrefsRow = 3;
    /// D03-2: opens the backlog page (no confirmation).
    static constexpr int kMainBacklogRow = 4;
    /// Main page rows with a two-step confirmation: back to the title (D02)
    /// and exit (always the last row).
    static constexpr int kMainTitleRow = 5;
    static constexpr int kMainExitRow = 6;
    static constexpr int kSlotRows = 10;
    static constexpr int kTitleRows = 5;
    /// Preferences page: 5 mixer/speed rows, D04-2 "Otomatik Hizi" (5) and
    /// "Atlama" (6), D05-2 "Dil" (7), "Metin Boyutu" (8), "Yuksek Kontrast" (9)
    /// and "Hareketi Azalt" (10), then "Geri" (always the last row).
    static constexpr int kPreferenceRows = 12;
    static constexpr int kPreferenceAutoDelayRow = 5;
    static constexpr int kPreferenceSkipRow = 6;
    static constexpr int kPreferenceLanguageRow = 7;
    static constexpr int kPreferenceTextScaleRow = 8;
    static constexpr int kPreferenceContrastRow = 9;
    static constexpr int kPreferenceMotionRow = 10;
    /// D02: slot-page thumbnail box (virtual canvas), panel top-right.
    static constexpr float kThumbX = 1180.0f;
    static constexpr float kThumbY = 150.0f;
    static constexpr float kThumbW = 192.0f;
    static constexpr float kThumbH = 108.0f;

    /// D03-2 backlog list area (virtual canvas): entries stack from kBacklogY0
    /// to kBacklogY1 inside the panel, text wraps to kBacklogTextW.
    static constexpr float kBacklogY0 = 250.0f;
    static constexpr float kBacklogY1 = 900.0f;
    static constexpr float kBacklogPadX = 18.0f;
    static constexpr float kBacklogPadY = 8.0f;
    static constexpr float kBacklogGap = 6.0f;
    static constexpr float kBacklogLineH = 28.0f;
    static constexpr float kBacklogFontPx = 22.0f;
    static constexpr float kBacklogSmallPx = 16.0f;
    /// Wrap width: row width minus both paddings and a safety band (physical
    /// glyph widths differ slightly from the virtual-size measurement).
    static constexpr float kBacklogTextW = kRowW - 2.0f * kBacklogPadX - 60.0f;
    static constexpr int kBacklogMaxLines = 8;

    /// Row geometry of a page with `rowCount` rows (virtual canvas): the
    /// comfortable pitch up to kComfortRows rows, the compact one beyond.
    static inline float rowY0(int rowCount) {
        return rowCount > kComfortRows ? kCompactRowY0 : kRowY0;
    }
    static inline float rowH(int rowCount) {
        return rowCount > kComfortRows ? kCompactRowH : kRowH;
    }
    static inline float rowStride(int rowCount) {
        return rowH(rowCount) + (rowCount > kComfortRows ? kCompactRowGap : kRowGap);
    }
    static inline float rowTop(int index, int rowCount) {
        return rowY0(rowCount) + static_cast<float>(index) * rowStride(rowCount);
    }

    /// Row index under a virtual point, or -1 for gaps/outside.
    static inline int rowAt(float vx, float vy, int rowCount) {
        const float y0 = rowY0(rowCount);
        if (vx < kRowX || vx > kRowX + kRowW || vy < y0) return -1;
        const float stride = rowStride(rowCount);
        const int index = static_cast<int>((vy - y0) / stride);
        if (index < 0 || index >= rowCount) return -1;
        return (vy <= y0 + index * stride + rowH(rowCount)) ? index : -1;
    }

    /// -1 = minus zone, +1 = plus zone, 0 = middle (select only).
    static inline int adjustDirection(float vx) {
        if (vx < kMinusX1) return -1;
        if (vx > kPlusX0) return +1;
        return 0;
    }
};

} // namespace Rowl::Core
