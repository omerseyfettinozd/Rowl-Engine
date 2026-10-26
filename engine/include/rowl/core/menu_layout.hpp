#pragma once

#include "rowl/core/pause_menu.hpp"
#include "rowl/state/ui_theme.hpp"

#include <array>
#include <string>
#include <vector>

namespace Rowl::Core {

/// T2b: the checked per-page layouts of a theme (index = kUiScreenNames order).
/// `declared[i]` = the theme named at least one `screens.<page>.layout` key.
struct ResolvedMenuLayouts {
    std::array<MenuLayout, Rowl::State::kUiScreenCount> pages{};
    std::array<bool, Rowl::State::kUiScreenCount> declared{};
    /// One line per page whose layout was refused (that page keeps the built-in layout).
    std::vector<std::string> diagnostics;
};

/// Builds the layout of every page from the theme: unnamed keys keep the built-in
/// place (title / hint / counter / thumbnail / list areas follow a moved panel and
/// row block), then each page is checked (finite, positive sizes, on the canvas,
/// rows inside the panel, grids with at least one visible row). A page that fails
/// gets MenuLayout{} (plus the slot style) and a diagnostic. With an empty theme
/// every page equals MenuLayout{} - the pre-T2b pixels.
ResolvedMenuLayouts resolveMenuLayouts(const Rowl::State::UiTheme& theme);

/// The layout adapted to the accessibility text scale. On a slot page (`slotPage`) the list
/// rows (they carry a detail line) and the cards grow with the scale and the page shows
/// fewer slots; every other page is returned unchanged except for `textScale`.
MenuLayout menuLayoutForTextScale(const MenuLayout& layout, float textScale, bool slotPage);

/// Nominal row counts the layout check uses per page (built-in page contents).
int menuLayoutNominalRows(int screenIndex, const MenuLayout& layout);

}  // namespace Rowl::Core
