// menu_layout.cpp — T2b: ui_theme.json `screens.<page>.layout` -> checked MenuLayout per page.
//
// The page code (engine hit-tests, window drawing) never reads the theme's raw layout; it
// reads the MenuLayout built here, so a refused layout can only ever leave a page on the
// built-in one. Pure functions, no engine state.

#include "rowl/core/menu_layout.hpp"

#include <algorithm>
#include <cmath>

namespace Rowl::Core {

namespace {

using Rowl::State::UiScreenLayout;
using Rowl::State::UiTheme;

constexpr float kCanvasW = 1920.0f;
constexpr float kCanvasH = 1080.0f;

template <typename T>
void put(const std::optional<T>& value, T& target) {
    if (value) target = *value;
}

MenuAlign parseAlign(const std::string& text, MenuAlign fallback) {
    if (text == "center") return MenuAlign::Center;
    if (text == "right") return MenuAlign::Right;
    if (text == "left") return MenuAlign::Left;
    return fallback;
}

bool anyNamed(const UiScreenLayout& l) { return !(l == UiScreenLayout{}); }

// The page's layout from the theme; unnamed derived places follow what moved.
MenuLayout build(const UiScreenLayout& l, bool cards) {
    MenuLayout m;
    m.cards = cards;
    put(l.panelX, m.panelX);
    put(l.panelY, m.panelY);
    put(l.panelW, m.panelW);
    put(l.panelH, m.panelH);
    // Offsets of the built-in design from its panel; they keep every part in the same
    // place relative to the panel when only the panel is moved / resized.
    constexpr float kRowInsetX = PauseMenuLayout::kRowX - PauseMenuLayout::kPanelX;     // 60
    constexpr float kRowInsetW = PauseMenuLayout::kPanelW - PauseMenuLayout::kRowW;     // 120
    const float dy = m.panelY - PauseMenuLayout::kPanelY;
    m.rowX = l.rowX ? *l.rowX : m.panelX + kRowInsetX;
    m.rowW = l.rowW ? *l.rowW : m.panelW - kRowInsetW;
    const float rowScale = m.rowW / PauseMenuLayout::kRowW;
    m.rowY0 = l.rowY ? *l.rowY : PauseMenuLayout::kRowY0 + dy;
    put(l.rowH, m.rowH);
    put(l.rowGap, m.rowGap);
    m.compactRowY0 = l.compactY ? *l.compactY : PauseMenuLayout::kCompactRowY0 + dy;
    put(l.compactH, m.compactRowH);
    put(l.compactGap, m.compactRowGap);
    if (l.comfortRows) m.comfortRows = static_cast<int>(std::lround(*l.comfortRows));
    m.titleAlign = parseAlign(l.titleAlign, m.titleAlign);
    m.labelAlign = parseAlign(l.labelAlign, m.labelAlign);
    m.valueAlign = parseAlign(l.valueAlign, m.valueAlign);
    m.hintAlign = parseAlign(l.hintAlign, m.hintAlign);
    put(l.labelWidth, m.labelWidth);
    put(l.valueX, m.valueX);
    put(l.valueWidth, m.valueWidth);
    put(l.rowPadX, m.rowPadX);
    m.adjustMinusW = (PauseMenuLayout::kMinusX1 - PauseMenuLayout::kRowX) * rowScale;
    m.adjustPlusX = (PauseMenuLayout::kPlusX0 - PauseMenuLayout::kRowX) * rowScale;

    m.titleX = l.titleX ? *l.titleX : m.rowX;
    m.titleY = l.titleY ? *l.titleY : 170.0f + dy;
    m.titleW = l.titleW ? *l.titleW : m.rowW;
    m.hintX = l.hintX ? *l.hintX : m.rowX;
    m.hintY = l.hintY ? *l.hintY : m.panelY + m.panelH - 44.0f;
    m.hintW = l.hintW ? *l.hintW : m.rowW;
    m.counterW = l.counterW ? *l.counterW : 150.0f;
    m.counterX = l.counterX ? *l.counterX : m.rowX + m.rowW - m.counterW;
    m.counterY = l.counterY ? *l.counterY : 178.0f + dy;

    put(l.thumbW, m.thumbW);
    put(l.thumbH, m.thumbH);
    m.thumbX = l.thumbX ? *l.thumbX : m.panelX + m.panelW - 28.0f - m.thumbW;
    m.thumbY = l.thumbY ? *l.thumbY : m.panelY + 40.0f;
    m.thumbHidden = l.thumbHidden || cards;

    put(l.sliderStart, m.sliderStart);
    put(l.sliderEndInset, m.sliderEndInset);
    put(l.sliderValueInset, m.sliderValueInset);

    const float areaTop = m.panelY + 140.0f;
    const float areaBottom = m.panelY + 790.0f;
    m.backlogY0 = l.historyY ? *l.historyY : areaTop;
    m.backlogY1 = l.historyBottom ? *l.historyBottom : areaBottom;
    put(l.historyPadX, m.backlogPadX);
    put(l.historyGap, m.backlogGap);

    m.galleryY0 = l.galleryY ? *l.galleryY : areaTop;
    m.galleryY1 = l.galleryBottom ? *l.galleryBottom : areaBottom;
    put(l.galleryGapX, m.galleryGapX);
    put(l.galleryGapY, m.galleryGapY);
    put(l.galleryCaptionH, m.galleryCaptionH);
    if (l.galleryColumns) m.galleryColumns = static_cast<int>(std::lround(*l.galleryColumns));
    const float cols = static_cast<float>(std::max(1, m.galleryColumns));
    m.galleryCellW = l.galleryCellW ? *l.galleryCellW : (m.rowW - (cols - 1.0f) * m.galleryGapX) / cols;
    // 16:9 thumbnail of the cell unless named; the built-in 4 columns keep the 100 px of old.
    const bool builtinGrid = !l.galleryColumns && !l.galleryCellW && !l.rowW && !l.panelW && !l.galleryGapX;
    m.galleryThumbH = l.galleryThumbH ? *l.galleryThumbH
                                      : (builtinGrid ? PauseMenuLayout::kGalleryThumbH
                                                     : std::round(m.galleryCellW * 9.0f / 16.0f));

    m.achY0 = l.achY ? *l.achY : areaTop;
    m.achY1 = l.achBottom ? *l.achBottom : areaBottom;
    put(l.achRowH, m.achRowH);
    put(l.achGap, m.achRowGap);

    if (l.slotsPerPage) m.slotsPerPage = static_cast<int>(std::lround(*l.slotsPerPage));
    m.pagerX = l.pagerX ? *l.pagerX : m.rowX;
    m.pagerW = l.pagerW ? *l.pagerW : m.rowW;
    put(l.pagerH, m.pagerH);
    m.pagerY = l.pagerY ? *l.pagerY : m.hintY - m.pagerH - 8.0f;

    if (l.cardColumns) m.cardColumns = static_cast<int>(std::lround(*l.cardColumns));
    put(l.cardH, m.cardH);
    put(l.cardGapX, m.cardGapX);
    put(l.cardGapY, m.cardGapY);
    m.cardY0 = l.cardY ? *l.cardY : areaTop;
    m.cardY1 = l.cardBottom ? *l.cardBottom : m.pagerY - 8.0f;
    return m;
}

bool finite(float v) { return std::isfinite(v); }

bool insideCanvas(float x, float y, float w, float h) {
    return finite(x) && finite(y) && finite(w) && finite(h) && w >= 0.0f && h >= 0.0f && x >= 0.0f && y >= 0.0f &&
           x + w <= kCanvasW + 0.01f && y + h <= kCanvasH + 0.01f;
}

// Empty string when fine, else the reason.
std::string check(const MenuLayout& m, int screenIndex, bool panelDrawn) {
    const auto bad = [](const char* what) { return std::string(what); };
    const float all[] = {m.panelX, m.panelY, m.panelW, m.panelH, m.titleX, m.titleY, m.titleW, m.rowX, m.rowW,
                         m.rowY0, m.rowH, m.rowGap, m.compactRowY0, m.compactRowH, m.compactRowGap, m.labelWidth,
                         m.valueX, m.valueWidth, m.rowPadX, m.hintX, m.hintY, m.hintW, m.counterX, m.counterY,
                         m.counterW, m.thumbX, m.thumbY, m.thumbW, m.thumbH, m.sliderStart, m.sliderEndInset,
                         m.sliderValueInset, m.backlogY0, m.backlogY1, m.backlogPadX, m.backlogGap, m.galleryY0,
                         m.galleryY1, m.galleryGapX, m.galleryGapY, m.galleryCellW, m.galleryThumbH,
                         m.galleryCaptionH, m.achY0, m.achY1, m.achRowH, m.achRowGap, m.pagerX, m.pagerY, m.pagerW,
                         m.pagerH, m.cardH, m.cardGapX, m.cardGapY, m.cardY0, m.cardY1};
    for (const float v : all) {
        if (!finite(v)) return bad("a value is not a finite number");
    }
    if (panelDrawn) {
        if (m.panelW <= 0.0f || m.panelH <= 0.0f) return bad("panel has no size");
        if (!insideCanvas(m.panelX, m.panelY, m.panelW, m.panelH)) return bad("panel is outside the 1920x1080 canvas");
    }
    if (m.rowW <= 0.0f || m.rowH <= 0.0f || m.compactRowH <= 0.0f) return bad("rows have no size");
    if (m.comfortRows < 1) return bad("comfort_rows must be at least 1");
    const float boundX = panelDrawn ? m.panelX : 0.0f;
    const float boundY = panelDrawn ? m.panelY : 0.0f;
    const float boundR = panelDrawn ? m.panelX + m.panelW : kCanvasW;
    const float boundB = panelDrawn ? m.panelY + m.panelH : kCanvasH;
    if (m.rowX < boundX - 0.01f || m.rowX + m.rowW > boundR + 0.01f) {
        return bad(panelDrawn ? "rows do not fit the panel horizontally" : "rows are outside the canvas");
    }
    const int rows = menuLayoutNominalRows(screenIndex, m);
    if (rows > 0) {
        const float last = m.rowTop(rows - 1, rows) + m.rowHOf(rows);
        if (m.rowTop(0, rows) < boundY - 0.01f || last > boundB + 0.01f) {
            return bad(panelDrawn ? "rows do not fit the panel" : "rows do not fit the canvas");
        }
    }
    if (m.labelWidth <= 0.0f || m.valueWidth <= 0.0f) return bad("label_width / value_width must be above 0");
    if (!insideCanvas(m.titleX, m.titleY, m.titleW, m.titleH)) return bad("title box is outside the canvas");
    if (!insideCanvas(m.hintX, m.hintY, m.hintW, 30.0f)) return bad("hint box is outside the canvas");
    if (m.sliderX1() <= m.sliderX0()) return bad("slider track has no length");
    const bool slotPage = screenIndex == 2 || screenIndex == 3;
    if (slotPage) {
        if (m.slotsPerPage < 1) return bad("slots.per_page must be at least 1");
        if (!m.thumbHidden && (m.thumbW <= 0.0f || m.thumbH <= 0.0f ||
                               !insideCanvas(m.thumbX, m.thumbY, m.thumbW, m.thumbH))) {
            return bad("thumbnail box is outside the canvas or has no size");
        }
        if (m.pagerW <= 0.0f || m.pagerH <= 0.0f || !insideCanvas(m.pagerX, m.pagerY, m.pagerW, m.pagerH)) {
            return bad("pager strip is outside the canvas or has no size");
        }
        if (m.cards) {
            if (m.cardColumns < 1 || m.cardColumns > 6) return bad("cards.columns must be 1..6");
            if (m.cardW() < 80.0f || m.cardH < 40.0f) return bad("cards are too small");
            if (m.cardY1 - m.cardY0 < m.cardH) return bad("the card area is shorter than one card");
            if (m.rowX + m.rowW > kCanvasW + 0.01f) return bad("cards leave the canvas");
            if (m.cardY0 < boundY - 0.01f || m.cardY1 > boundB + 0.01f) return bad("cards do not fit the panel");
        }
    }
    if (screenIndex == 5) {
        if (m.backlogY1 - m.backlogY0 < 60.0f) return bad("history area is too short");
        if (m.backlogY0 < boundY - 0.01f || m.backlogY1 > boundB + 0.01f) return bad("history area does not fit the panel");
        if (m.backlogTextW() < 100.0f) return bad("history rows are too narrow");
    }
    if (screenIndex == 7) {
        if (m.galleryColumns < 1 || m.galleryColumns > 12) return bad("gallery.columns must be 1..12");
        if (m.galleryCellW < 24.0f || m.galleryThumbH < 12.0f) return bad("gallery cells are too small");
        const float gridW = static_cast<float>(m.galleryColumns) * m.galleryCellW +
                            static_cast<float>(m.galleryColumns - 1) * m.galleryGapX;
        if (m.rowX + gridW > kCanvasW + 0.01f) return bad("the gallery grid leaves the canvas");
        if (m.galleryY1 - m.galleryY0 < m.galleryCellH()) return bad("the gallery area is shorter than one cell");
        if (m.galleryY0 < boundY - 0.01f || m.galleryY1 > boundB + 0.01f) return bad("the gallery area does not fit the panel");
    }
    if (screenIndex == 9) {
        if (m.achRowH < 40.0f) return bad("achievement rows are too short");
        if (m.achY1 - m.achY0 < m.achRowH) return bad("the achievement area is shorter than one row");
        if (m.achY0 < boundY - 0.01f || m.achY1 > boundB + 0.01f) return bad("the achievement area does not fit the panel");
    }
    return std::string();
}

}  // namespace

int menuLayoutNominalRows(int screenIndex, const MenuLayout& layout) {
    switch (screenIndex) {
        case 0: return PauseMenuLayout::kTitleRowsWithExtras;       // title
        case 1: return PauseMenuLayout::kMainRows;                  // pause
        case 2:
        case 3: return layout.cards ? 0 : layout.slotsPerPage;      // save / load
        case 4: return PauseMenuLayout::kPreferenceRows;            // preferences
        case 6: return PauseMenuLayout::kExtrasRootRows;            // extras root
        case 8: return layout.comfortRows;                          // music room list
        case 10: return PauseMenuLayout::kMainRows;                 // confirm
        default: return 0;                                          // history / gallery / achievements: own areas
    }
}

ResolvedMenuLayouts resolveMenuLayouts(const UiTheme& theme) {
    ResolvedMenuLayouts out;
    const bool cards = theme.slots.layout == "cards";
    for (std::size_t i = 0; i < Rowl::State::kUiScreenCount; ++i) {
        const auto& screen = theme.screens[i];
        const bool slotPage = i == 2 || i == 3;
        const bool pageCards = cards && slotPage;
        MenuLayout builtin;
        builtin.cards = pageCards;
        builtin.thumbHidden = pageCards;
        out.declared[i] = anyNamed(screen.layout);
        if (!out.declared[i]) {
            // Cards need their own sizes even without a layout block: nothing to check, built-in numbers.
            out.pages[i] = builtin;
            continue;
        }
        const bool panelDrawn = !(screen.panel ? screen.panel->none : theme.menu.panelSkin.none);
        MenuLayout built = build(screen.layout, pageCards);
        const std::string why = check(built, static_cast<int>(i), panelDrawn);
        if (why.empty()) {
            out.pages[i] = built;
        } else {
            out.pages[i] = builtin;
            out.diagnostics.push_back(std::string("ui_theme.json: screens.") + Rowl::State::kUiScreenNames[i] +
                                      ".layout: " + why + "; the built-in layout is used");
        }
    }
    return out;
}

MenuLayout menuLayoutForTextScale(const MenuLayout& layout, float textScale, bool slotPage) {
    MenuLayout m = layout;
    const float scale = std::isfinite(textScale) ? std::clamp(textScale, 1.0f, 2.0f) : 1.0f;
    m.textScale = scale;
    if (!slotPage || scale <= 1.0f) return m;
    if (m.cards) {
        m.cardH = layout.cardH * scale;
        return m;
    }
    // List rows carry a detail line: the row grows with the text so the label, the detail line
    // and the neighbour never touch; fewer slots then fit the page (the pager strip pages the rest).
    m.rowH = layout.rowH * scale;
    const float limit = layout.pagerY > layout.rowY0 ? layout.pagerY - 8.0f : layout.hintY - 16.0f;
    const int fit = std::max(1, static_cast<int>((limit - layout.rowY0 + layout.rowGap) / (m.rowH + layout.rowGap)));
    m.slotsPerPage = std::min(layout.slotsPerPage, fit);
    return m;
}

}  // namespace Rowl::Core
