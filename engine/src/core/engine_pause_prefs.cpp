// engine_pause_prefs.cpp — D05-2: the Preferences page's language and
// accessibility rows (Dil / Metin Boyutu / Yuksek Kontrast / Hareketi Azalt).
//
// The accessibility state lives in the font renderers (scale, contrast) and the
// camera (reduced motion); RowlEngine_SetTextScale / SetHighContrast /
// SetReducedMotion (c_api_accessibility.cpp) and the menu rows both go through
// the Engine members below, so there is exactly one path. The language row uses
// the same steps as RowlEngine_SetLocale (trySetLocale + refreshActive-
// DialogueLocalization).
// TU-local sozlesme: bu yordamlar kilit ALMAZ. Yeni `RowlEngine_` sembolu YOK.

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/render/camera2d.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/window.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace Rowl::Core {

void Engine::setTextScale(float scale) {
    auto* win = getWindow();
    if (!win) return;
    win->forEachFontRenderer([&](Rowl::Render::FontRenderer& renderer) {
        renderer.setTextScale(scale);
    });
    // The static-frame cache keys on scene content, not on display settings: a
    // resting story frame would keep the old glyph sizes until something moved.
    win->invalidateDisplayCache();
}

float Engine::textScale() const {
    auto* win = getWindow();
    auto* renderer = win ? win->getFontRenderer() : nullptr;
    return renderer ? renderer->textScale() : 1.0f;
}

void Engine::setHighContrast(bool enabled) {
    auto* win = getWindow();
    if (!win) return;
    win->forEachFontRenderer([&](Rowl::Render::FontRenderer& renderer) {
        renderer.setHighContrast(enabled);
    });
    win->invalidateDisplayCache();  // see setTextScale
}

bool Engine::highContrast() const {
    auto* win = getWindow();
    auto* renderer = win ? win->getFontRenderer() : nullptr;
    return renderer && renderer->highContrast();
}

void Engine::setReducedMotion(bool enabled) {
    if (auto* camera = getCamera()) camera->setReducedMotion(enabled);
}

bool Engine::reducedMotion() const {
    const auto* camera = getCamera();
    return camera && camera->reducedMotion();
}

std::string Engine::preferenceLanguageText() const {
    // A project that declares no locales keeps its historical Turkish menus
    // (see uiLangForLocale); showing the "en" placeholder locale would
    // contradict the language the menu is drawn in.
    if (!m_localization.isLocaleDeclared()) {
        return localeDisplayName(getUiLanguage() == UiLang::English ? "en" : "tr");
    }
    return localeDisplayName(m_localization.getLocale());
}

void Engine::cyclePreferenceLanguage(int direction) {
    if (!m_localization.isLocaleDeclared() || direction == 0) return;
    using Rowl::I18n::LocalizationManager;
    const std::vector<std::string>& supported = m_localization.getSupportedLocales();
    const int count = static_cast<int>(supported.size());
    if (count == 0) return;

    const std::string active = LocalizationManager::normalizeLocale(m_localization.getLocale());
    int current = -1;
    for (int i = 0; i < count; ++i) {
        if (LocalizationManager::normalizeLocale(supported[static_cast<size_t>(i)]) == active) {
            current = i;
            break;
        }
    }
    const int step = direction > 0 ? 1 : -1;
    // An active locale outside the list starts from just before the first
    // (or after the last) entry; otherwise the walk visits every other entry
    // once and skips the ones whose catalog cannot be selected.
    const int base = current >= 0 ? current : (step > 0 ? -1 : count);
    for (int k = 1; k <= count; ++k) {
        const int index = (((base + step * k) % count) + count) % count;
        if (index == current) break;  // came all the way round: nothing else to pick
        const std::string& candidate = supported[static_cast<size_t>(index)];
        if (m_localization.trySetLocale(candidate) != LocalizationManager::LocaleSetResult::Ok) continue;
        refreshActiveDialogueLocalization();
        persistPauseMenuPreference(Rowl::Core::PauseMenuLayout::kPreferenceLanguageRow + 3);
        ROWL_LOG_INFO("[Player] Language changed to '" + m_localization.getLocale() + "' from preferences.");
        return;
    }
}

}  // namespace Rowl::Core
