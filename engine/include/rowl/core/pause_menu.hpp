#pragma once

#include "rowl/core/player_ui_strings.hpp"

#include <algorithm>
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
    Backlog = 5,
    /// D07-2: title-screen "Ekstralar" pages. Extras = the root page (Galeri /
    /// Muzik Odasi / Basarimlar / Geri), then one mode per sub-page. Reached
    /// from the title only; Back returns one level up.
    Extras = 6,
    Gallery = 7,
    MusicRoom = 8,
    Achievements = 9
};

inline constexpr bool isExtrasMode(PauseMenuMode mode) {
    return mode == PauseMenuMode::Extras || mode == PauseMenuMode::Gallery ||
           mode == PauseMenuMode::MusicRoom || mode == PauseMenuMode::Achievements;
}

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
    /// T2: slot pages - 0 not a slot row, 1 empty, 2 filled, 3 corrupt (row skins).
    int slotState = 0;
    /// T2: Preferences value rows - position 0..1 of the value in its range
    /// (slider style); -1 = not a slider row.
    float fraction = -1.0f;
    /// T2: the pointer is dragging this row's slider (knob "pressed" skin).
    bool dragging = false;
};

/// D02: per-slot metadata memo shared by the slot pages and the title screen.
struct PauseSlotInfo {
    /// F2: Incompatible = a well-formed save this build cannot apply (newer format
    /// version, a graph it was not recorded from, a node the graph lacks): shown as
    /// "uyumsuz" instead of "bozuk", so an author who changed the story is not told
    /// the player's saves are broken.
    enum class State : uint8_t { Empty = 0, Valid = 1, Corrupt = 2, Incompatible = 3 };
    State state = State::Empty;
    std::string savedAt;       // ISO-8601 UTC ("saved_at")
    std::string chapterTitle;
    std::string summary;
    double playtimeSeconds = 0.0;
    /// F2 (B-2): the list keeps metadata only; the PNG is read lazily for the selected
    /// row (Engine::slotThumbnail). `thumbnailStamp` = saved_at + file size + mtime:
    /// two saves inside one second still get different textures.
    bool hasThumbnail = false;
    std::string thumbnailStamp;
};

/// D07-2: row builder that names every field (aggregate init of the older rows
/// leaves `detail` out and trips -Wmissing-field-initializers).
inline PauseMenuRow makePauseRow(std::string label, std::string value = std::string(),
                                 bool isValue = false, std::string detail = std::string(),
                                 bool enabled = true) {
    PauseMenuRow row;
    row.label = std::move(label);
    row.value = std::move(value);
    row.isValue = isValue;
    row.detail = std::move(detail);
    row.enabled = enabled;
    return row;
}

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
    bool choice = false;      // F1: the player's own answer row ("> option")
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

/// D07-2: one gallery grid cell. x/y/w/h are virtual-canvas pixels of the whole
/// cell (thumbnail on top, caption below). A locked cell carries NO image path
/// (nothing of it can reach the renderer) and the masked caption "???".
struct PauseGalleryCellView {
    int index = 0;            // catalog index
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool unlocked = false;
    bool selected = false;
    std::string title;        // "???" while locked
    std::string thumbnail;    // VFS path (thumbnail or image); empty while locked
};

/// D07-2: a music-room / achievement list item (visible window only).
struct PauseExtrasItemView {
    int index = 0;            // catalog index
    std::string id;
    bool unlocked = false;
    bool selected = false;
    bool playing = false;     // music room: this track is the one playing
    std::string title;        // masked while locked/hidden
    std::string detail;       // achievements: description ("???" while locked)
    std::string at;           // achievements: unlock date "YYYY-MM-DD" (unlocked only)
    std::string icon;         // T2: achievements: VFS image of the icon (unlocked only; never for a locked one)
    float y = 0.0f;           // achievements: row box top / height in virtual pixels
    float h = 0.0f;
};

/// D07-2: Ekstralar page state (active only in an Extras* mode).
struct PauseExtrasView {
    bool active = false;
    std::string page;         // "root" | "gallery" | "music" | "achievements"
    int total = 0;            // items in the page's catalog list
    int unlocked = 0;
    int selected = -1;        // catalog index (root: row)
    int scroll = 0;           // first visible row (gallery: grid row; lists: item)
    std::string counter;      // "3 / 10"
    // Gallery
    int columns = 0;
    bool viewing = false;     // full-screen viewer open
    int viewIndex = -1;
    std::string viewImage;    // VFS path of the viewed (unlocked) image
    std::string viewTitle;
    std::vector<PauseGalleryCellView> cells;
    // Music room / achievements
    std::string playingId;    // music room: id of the playing track, "" none
    std::vector<PauseExtrasItemView> items;
};

/// D07-2: the `extras` object of the pause-menu JSON (engine_extras_menu.cpp).
std::string pauseExtrasViewJson(const PauseExtrasView& view);

/// "55%" / "1.25x" row values (engine_pause_menu.cpp), shared with D02 pages.
std::string pauseMenuPercent(float volume);
std::string pauseMenuSpeedText(float multiplier);
/// D04-2: "2.0 sn" / "2.0 s" (one decimal) auto-advance delay row value.
std::string pauseMenuDelayText(float seconds, UiLang lang);
/// D02: "<YYYY-MM-DD HH:MM>  <chapter>  <summary ~48 bytes>" of a valid slot,
/// empty otherwise (engine_title_menu.cpp).
std::string pauseSlotDetail(const PauseSlotInfo& info);
/// D-K2: ISO-8601 UTC stamp ("...Z", seconds/fraction optional) -> player-local
/// "YYYY-MM-DD HH:MM" or, with dateOnly, "YYYY-MM-DD". Unparsable / "Z"-less
/// stamps are shown truncated as stored (engine_title_menu.cpp).
std::string utcIsoToLocalText(const std::string& iso, bool dateOnly);
/// D02: "2026-10-24T12:34:56Z" -> local "2026-10-24 12:34" (D-K2: local time).
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
    /// D07-2: with a non-empty extras catalog the title gains "Ekstralar" at row
    /// 3 (before Tercihler); Tercihler and Cikis shift down by one.
    static constexpr int kTitleRowsWithExtras = 6;
    static constexpr int kTitleExtrasRow = 3;
    /// D07-2: Ekstralar root page rows: Galeri 0, Muzik Odasi 1, Basarimlar 2, Geri 3.
    static constexpr int kExtrasRootRows = 4;
    static constexpr int kExtrasGalleryRow = 0;
    static constexpr int kExtrasMusicRow = 1;
    static constexpr int kExtrasAchievementsRow = 2;
    static constexpr int kExtrasBackRow = 3;
    /// D07-2: music-room / achievement lists show at most kComfortRows rows at a
    /// time (the comfortable pitch) and scroll beyond that.
    static constexpr int kExtrasListRows = 11;
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

    /// D07-2 gallery grid (virtual canvas): kGalleryColumns cells across the row
    /// width, each a 16:9 thumbnail with a caption line below; whole grid rows
    /// scroll inside [kGalleryY0, kGalleryY1].
    static constexpr int kGalleryColumns = 4;
    static constexpr float kGalleryY0 = 250.0f;
    static constexpr float kGalleryY1 = 900.0f;
    static constexpr float kGalleryGapX = 16.0f;
    static constexpr float kGalleryGapY = 14.0f;
    static constexpr float kGalleryCellW =
        (kRowW - (kGalleryColumns - 1) * kGalleryGapX) / kGalleryColumns;
    static constexpr float kGalleryThumbH = 100.0f;  // ~16:9 of kGalleryCellW (178)
    static constexpr float kGalleryCaptionH = 30.0f;
    static constexpr float kGalleryCellH = kGalleryThumbH + kGalleryCaptionH;
    static constexpr float kGalleryPitchY = kGalleryCellH + kGalleryGapY;
    static constexpr int kGalleryVisibleRows =
        static_cast<int>((kGalleryY1 - kGalleryY0 + kGalleryGapY) / kGalleryPitchY);
    static inline float galleryCellX(int column) {
        return kRowX + static_cast<float>(column) * (kGalleryCellW + kGalleryGapX);
    }
    static inline float galleryCellY(int visibleRow) {
        return kGalleryY0 + static_cast<float>(visibleRow) * kGalleryPitchY;
    }
    /// Visible (column, row) under a virtual point, or false for gaps/outside.
    static inline bool galleryCellAt(float vx, float vy, int& column, int& visibleRow) {
        if (vx < kRowX || vy < kGalleryY0) return false;
        const float fx = vx - kRowX;
        column = static_cast<int>(fx / (kGalleryCellW + kGalleryGapX));
        visibleRow = static_cast<int>((vy - kGalleryY0) / kGalleryPitchY);
        if (column < 0 || column >= kGalleryColumns || visibleRow < 0 ||
            visibleRow >= kGalleryVisibleRows) return false;
        if (fx - column * (kGalleryCellW + kGalleryGapX) > kGalleryCellW) return false;
        return (vy - kGalleryY0) - visibleRow * kGalleryPitchY <= kGalleryCellH;
    }

    /// D07-2 achievements page: its own two-line rows (title on top, description
    /// below with a clear gap, date at the right edge on the title line) that
    /// the generic one-line row pitch cannot hold. A row box is kAchRowH tall
    /// (the highlight covers both lines); kAchVisibleRows rows fit the list area
    /// and the list scrolls beyond that.
    static constexpr float kAchY0 = 250.0f;
    static constexpr float kAchY1 = 900.0f;
    static constexpr float kAchRowH = 96.0f;
    static constexpr float kAchRowGap = 8.0f;
    static constexpr float kAchPitch = kAchRowH + kAchRowGap;
    static constexpr int kAchVisibleRows =
        static_cast<int>((kAchY1 - kAchY0 + kAchRowGap) / kAchPitch);
    static constexpr float kAchPadX = 18.0f;
    static constexpr float kAchTitlePx = 26.0f;
    static constexpr float kAchDescPx = 19.0f;
    static constexpr float kAchDatePx = 20.0f;
    /// Offsets of the text boxes from the row top. A line's ink spans roughly
    /// [top + 0.15 px, top + 1.2 px]: the title ends near +50, the description
    /// begins near +60, so >= 6 px of clear band separate them.
    static constexpr float kAchTitleTop = 12.0f;
    static constexpr float kAchDescTop = 58.0f;
    static inline float achRowTop(int visibleIndex) {
        return kAchY0 + static_cast<float>(visibleIndex) * kAchPitch;
    }
    /// Visible row index under a virtual point, or -1 (gaps / outside).
    static inline int achRowAt(float vx, float vy, int visibleCount) {
        if (vx < kRowX || vx > kRowX + kRowW || vy < kAchY0) return -1;
        const int index = static_cast<int>((vy - kAchY0) / kAchPitch);
        if (index < 0 || index >= visibleCount || index >= kAchVisibleRows) return -1;
        return (vy - kAchY0) - static_cast<float>(index) * kAchPitch <= kAchRowH ? index : -1;
    }

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

    /// T2: slider style of the Preferences value rows (virtual canvas). The track
    /// runs between these x values; a press within kSliderHitSlack of it sets the value.
    static constexpr float kSliderX0 = kRowX + kRowW * 0.52f;
    static constexpr float kSliderX1 = kRowX + kRowW - 120.0f;
    static constexpr float kSliderValueX = kRowX + kRowW - 104.0f;
    static constexpr float kSliderHitSlack = 16.0f;
    static inline bool inSliderZone(float vx) {
        return vx >= kSliderX0 - kSliderHitSlack && vx <= kSliderX1 + kSliderHitSlack;
    }
    /// Position 0..1 along the slider track for a virtual x.
    static inline float sliderFractionAt(float vx) {
        const float f = (vx - kSliderX0) / (kSliderX1 - kSliderX0);
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }

    /// -1 = minus zone, +1 = plus zone, 0 = middle (select only).
    static inline int adjustDirection(float vx) {
        if (vx < kMinusX1) return -1;
        if (vx > kPlusX0) return +1;
        return 0;
    }
};

/// T2b: horizontal alignment of a layout text box.
enum class MenuAlign : uint8_t { Left = 0, Center = 1, Right = 2 };
inline const char* menuAlignName(MenuAlign align) {
    return align == MenuAlign::Center ? "Center" : align == MenuAlign::Right ? "Right" : "Left";
}

/// T2b: the RUNTIME layout of one menu page (virtual 1920x1080 canvas). Every
/// field defaults to the matching PauseMenuLayout constant, so a project without
/// `screens.<page>.layout` in its ui_theme.json lays the page out exactly as
/// before. The engine resolves one per page (theme + validation, see
/// resolveMenuLayout), hit-tests clicks / wheel / drags with it and hands the
/// same copy to the window in PauseMenuView::layout, so what is drawn and what is
/// hit share ONE source. The PauseMenuLayout statics stay as the built-in
/// defaults and as the row-count / row-index contract.
struct MenuLayout {
    // Panel behind the page.
    float panelX = PauseMenuLayout::kPanelX;
    float panelY = PauseMenuLayout::kPanelY;
    float panelW = PauseMenuLayout::kPanelW;
    float panelH = PauseMenuLayout::kPanelH;
    // Page title text box.
    float titleX = PauseMenuLayout::kRowX;
    float titleY = 170.0f;
    float titleW = PauseMenuLayout::kRowW;
    float titleH = 60.0f;
    MenuAlign titleAlign = MenuAlign::Left;
    // Rows: comfortable pitch up to comfortRows rows, the compact one beyond.
    float rowX = PauseMenuLayout::kRowX;
    float rowW = PauseMenuLayout::kRowW;
    float rowY0 = PauseMenuLayout::kRowY0;
    float rowH = PauseMenuLayout::kRowH;
    float rowGap = PauseMenuLayout::kRowGap;
    int comfortRows = PauseMenuLayout::kComfortRows;
    float compactRowY0 = PauseMenuLayout::kCompactRowY0;
    float compactRowH = PauseMenuLayout::kCompactRowH;
    float compactRowGap = PauseMenuLayout::kCompactRowGap;
    /// Row text: the label column and the value column (fractions of rowW) with their alignment.
    MenuAlign labelAlign = MenuAlign::Left;
    MenuAlign valueAlign = MenuAlign::Left;
    float labelWidth = 0.55f;
    float valueX = 0.55f;
    float valueWidth = 0.4f;
    float rowPadX = 18.0f;  ///< label inset when the row skin gives no padding
    /// Value rows: px from rowX of the end of the minus zone / start of the plus zone.
    float adjustMinusW = PauseMenuLayout::kMinusX1 - PauseMenuLayout::kRowX;
    float adjustPlusX = PauseMenuLayout::kPlusX0 - PauseMenuLayout::kRowX;
    // Hint line, "3 / 10" counter, slot thumbnail box.
    float hintX = PauseMenuLayout::kRowX;
    float hintY = PauseMenuLayout::kPanelY + PauseMenuLayout::kPanelH - 44.0f;
    float hintW = PauseMenuLayout::kRowW;
    MenuAlign hintAlign = MenuAlign::Left;
    float counterX = PauseMenuLayout::kRowX + PauseMenuLayout::kRowW - 150.0f;
    float counterY = 178.0f;
    float counterW = 150.0f;
    float thumbX = PauseMenuLayout::kThumbX;
    float thumbY = PauseMenuLayout::kThumbY;
    float thumbW = PauseMenuLayout::kThumbW;
    float thumbH = PauseMenuLayout::kThumbH;
    bool thumbHidden = false;
    // Preferences slider.
    float sliderStart = 0.52f;      ///< track start, fraction of rowW
    float sliderEndInset = 120.0f;  ///< track end, px left of the row's right edge
    float sliderValueInset = 104.0f;  ///< the "55%" text, px left of the row's right edge
    float sliderHitSlack = PauseMenuLayout::kSliderHitSlack;
    // History list area.
    float backlogY0 = PauseMenuLayout::kBacklogY0;
    float backlogY1 = PauseMenuLayout::kBacklogY1;
    float backlogPadX = PauseMenuLayout::kBacklogPadX;
    float backlogGap = PauseMenuLayout::kBacklogGap;
    // Gallery grid.
    int galleryColumns = PauseMenuLayout::kGalleryColumns;
    float galleryY0 = PauseMenuLayout::kGalleryY0;
    float galleryY1 = PauseMenuLayout::kGalleryY1;
    float galleryGapX = PauseMenuLayout::kGalleryGapX;
    float galleryGapY = PauseMenuLayout::kGalleryGapY;
    float galleryCellW = PauseMenuLayout::kGalleryCellW;
    float galleryThumbH = PauseMenuLayout::kGalleryThumbH;
    float galleryCaptionH = PauseMenuLayout::kGalleryCaptionH;
    // Achievements list.
    float achY0 = PauseMenuLayout::kAchY0;
    float achY1 = PauseMenuLayout::kAchY1;
    float achRowH = PauseMenuLayout::kAchRowH;
    float achRowGap = PauseMenuLayout::kAchRowGap;
    // Slot pages: how many slots a page shows, the "< Page 1 / 3 >" strip and the card grid.
    int slotsPerPage = PauseMenuLayout::kSlotRows;
    float pagerX = PauseMenuLayout::kRowX;
    float pagerY = PauseMenuLayout::kPanelY + PauseMenuLayout::kPanelH - 44.0f - 52.0f;
    float pagerW = PauseMenuLayout::kRowW;
    float pagerH = 44.0f;
    bool cards = false;  ///< slot pages as a card grid (theme `slots.layout`: "cards")
    int cardColumns = 2;
    float cardH = 140.0f;
    float cardGapX = 16.0f;
    float cardGapY = 14.0f;
    float cardY0 = 250.0f;
    float cardY1 = 866.0f;
    /// Accessibility text scale the row heights were adapted to (1 = design size).
    float textScale = 1.0f;

    // ── rows ──
    float rowY0Of(int rowCount) const { return rowCount > comfortRows ? compactRowY0 : rowY0; }
    float rowHOf(int rowCount) const { return rowCount > comfortRows ? compactRowH : rowH; }
    float rowStride(int rowCount) const {
        return rowHOf(rowCount) + (rowCount > comfortRows ? compactRowGap : rowGap);
    }
    float rowTop(int index, int rowCount) const {
        return rowY0Of(rowCount) + static_cast<float>(index) * rowStride(rowCount);
    }
    /// Row index under a virtual point, or -1 for gaps/outside.
    int rowAt(float vx, float vy, int rowCount) const {
        const float y0 = rowY0Of(rowCount);
        if (vx < rowX || vx > rowX + rowW || vy < y0) return -1;
        const float stride = rowStride(rowCount);
        const int index = static_cast<int>((vy - y0) / stride);
        if (index < 0 || index >= rowCount) return -1;
        return (vy <= y0 + index * stride + rowHOf(rowCount)) ? index : -1;
    }
    // ── value rows / slider ──
    float sliderX0() const { return rowX + rowW * sliderStart; }
    float sliderX1() const { return rowX + rowW - sliderEndInset; }
    float sliderValuePos() const { return rowX + rowW - sliderValueInset; }
    bool inSliderZone(float vx) const {
        return vx >= sliderX0() - sliderHitSlack && vx <= sliderX1() + sliderHitSlack;
    }
    float sliderFractionAt(float vx) const {
        const float f = (vx - sliderX0()) / (sliderX1() - sliderX0());
        return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    }
    /// -1 = minus zone, +1 = plus zone, 0 = middle (select only).
    int adjustDirection(float vx) const {
        if (vx < rowX + adjustMinusW) return -1;
        if (vx > rowX + adjustPlusX) return +1;
        return 0;
    }
    // ── history ──
    float backlogTextW() const { return rowW - 2.0f * backlogPadX - 60.0f; }
    // ── gallery ──
    float galleryCellH() const { return galleryThumbH + galleryCaptionH; }
    float galleryPitchY() const { return galleryCellH() + galleryGapY; }
    int galleryVisibleRows() const {
        return std::max(1, static_cast<int>((galleryY1 - galleryY0 + galleryGapY) / galleryPitchY()));
    }
    float galleryCellX(int column) const {
        return rowX + static_cast<float>(column) * (galleryCellW + galleryGapX);
    }
    float galleryCellY(int visibleRow) const {
        return galleryY0 + static_cast<float>(visibleRow) * galleryPitchY();
    }
    bool galleryCellAt(float vx, float vy, int& column, int& visibleRow) const {
        if (vx < rowX || vy < galleryY0) return false;
        const float fx = vx - rowX;
        column = static_cast<int>(fx / (galleryCellW + galleryGapX));
        visibleRow = static_cast<int>((vy - galleryY0) / galleryPitchY());
        if (column < 0 || column >= galleryColumns || visibleRow < 0 ||
            visibleRow >= galleryVisibleRows()) return false;
        if (fx - column * (galleryCellW + galleryGapX) > galleryCellW) return false;
        return (vy - galleryY0) - visibleRow * galleryPitchY() <= galleryCellH();
    }
    // ── achievements ──
    float achPitch() const { return achRowH + achRowGap; }
    int achVisibleRows() const {
        return std::max(1, static_cast<int>((achY1 - achY0 + achRowGap) / achPitch()));
    }
    float achRowTop(int visibleIndex) const { return achY0 + static_cast<float>(visibleIndex) * achPitch(); }
    int achRowAt(float vx, float vy, int visibleCount) const {
        if (vx < rowX || vx > rowX + rowW || vy < achY0) return -1;
        const int index = static_cast<int>((vy - achY0) / achPitch());
        if (index < 0 || index >= visibleCount || index >= achVisibleRows()) return -1;
        return (vy - achY0) - static_cast<float>(index) * achPitch() <= achRowH ? index : -1;
    }
    // ── slot pages: card grid and pager ──
    float cardW() const {
        return (rowW - static_cast<float>(cardColumns - 1) * cardGapX) / static_cast<float>(cardColumns);
    }
    int cardVisibleRows() const {
        return std::max(1, static_cast<int>((cardY1 - cardY0 + cardGapY) / (cardH + cardGapY)));
    }
    float cardX(int row) const { return rowX + static_cast<float>(row % cardColumns) * (cardW() + cardGapX); }
    float cardY(int row) const {
        return cardY0 + static_cast<float>(row / cardColumns) * (cardH + cardGapY);
    }
    /// Card (page row index) under a virtual point among `count` cards, or -1.
    int cardAt(float vx, float vy, int count) const {
        if (vx < rowX || vy < cardY0) return -1;
        const float fx = vx - rowX;
        const int column = static_cast<int>(fx / (cardW() + cardGapX));
        const int line = static_cast<int>((vy - cardY0) / (cardH + cardGapY));
        if (column < 0 || column >= cardColumns || line < 0 || line >= cardVisibleRows()) return -1;
        if (fx - static_cast<float>(column) * (cardW() + cardGapX) > cardW()) return -1;
        if ((vy - cardY0) - static_cast<float>(line) * (cardH + cardGapY) > cardH) return -1;
        const int index = line * cardColumns + column;
        return index < count ? index : -1;
    }
    /// -1 = previous page, +1 = next page, 0 = miss, for a click on the pager strip.
    int pagerDirectionAt(float vx, float vy) const {
        if (vx < pagerX || vx > pagerX + pagerW || vy < pagerY || vy > pagerY + pagerH) return 0;
        if (vx < pagerX + pagerW * 0.33f) return -1;
        if (vx > pagerX + pagerW * 0.67f) return +1;
        return 0;
    }
};

/// One slot card of the card layout (virtual canvas pixels).
struct PauseSlotCardView {
    int row = 0;            ///< index on the page
    int slot = 0;           ///< absolute slot number
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    int slotState = 0;      ///< 1 empty, 2 filled, 3 corrupt
    bool selected = false;
    std::string label;      ///< "Yuva 3"
    std::string value;      ///< "Dolu" / "Bos" / "Bozuk"
    std::string date;       ///< local "YYYY-MM-DD HH:MM" (filled slots)
    std::string summary;    ///< chapter + summary, one line
    std::shared_ptr<const std::string> thumbnailPng;
    std::string thumbnailKey;
};

/// "< Sayfa 2 / 3 >" strip of a slot page with more than one page.
struct PauseSlotPagerView {
    bool active = false;
    int page = 0;           ///< 0-based
    int pageCount = 1;
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct PauseMenuView {
    bool open = false;
    /// T2: which page this is - "title" | "pause" | "save" | "load" | "preferences" |
    /// "history" | "extras_root" | "gallery" | "music" | "achievements" | "confirm"
    /// (a page with an armed "are you sure" row). The theme dresses pages by this name.
    std::string page;
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
    /// D07-2: Ekstralar pages (active only in an Extras* mode).
    PauseExtrasView extras;
    /// T2b: the page's layout (theme + defaults), the one the engine hit-tests with.
    MenuLayout layout;
    /// T2b: slot pages - card grid (empty = list rows) and the pager strip.
    std::vector<PauseSlotCardView> cards;
    PauseSlotPagerView pager;
};

} // namespace Rowl::Core
