// engine_choice_focus.cpp — D-K3: secim menusunde klavye / fare odagi.
//
// Gercek pencerede (D-K1 kabul yolculugu sonrasi bulgu): secim dugumunde
// Space/Enter "waiting for a choice" diye yutuluyor, oklar yalniz duraklatilmis
// menude calisiyor, rakamlar hizli-kayit yuvasini degistiriyordu — klavyeyle
// oynayan oyuncu ilk secimde takiliyordu.
//
// Odak yalniz secenek kimligiyle tutulur (m_choiceFocusId); secim acilinca YOK:
// ilk Yukari son, ilk Asagi ilk secilebilir secenege odak verir; Enter/Space
// odak yokken eskisi gibi yok sayilir (hizli gecen oyuncu secimi farkinda olmadan
// secmesin). Secilebilirlik (yazar bayragi + Lua kosulu) her sorguda yeniden
// hesaplanir, secilebilirligini yitiren odak duser. Sahne dugmeleri degisince (yeni dugum,
// geri sarma, yukleme) kimlik sifirlanir. Kilit ALMAZ; `RowlEngine_` sembolu YOK.

#include "rowl/core/engine.hpp"

#include "rowl/render/window.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace Rowl::Core {

std::vector<std::size_t> Engine::selectableChoiceIndices() const {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < m_activeChoiceButtons.size(); ++i) {
        const auto& button = m_activeChoiceButtons[i];
        // Cizilmeyen (enabled=false) dugme odaga girmez: gorunmez odak olmaz.
        if (button.enabled && choiceButtonSelectable(button)) indices.push_back(i);
    }
    return indices;
}

int Engine::focusedChoiceIndex() const {
    // No focus until the player asks for one (arrow key / hover): a choice menu
    // that appears while Space/Enter are being mashed must not be answered by that
    // same key press. A remembered option that stopped being selectable drops out.
    if (m_choiceFocusId.empty()) return -1;
    for (const std::size_t index : selectableChoiceIndices()) {
        if (m_activeChoiceButtons[index].optionId == m_choiceFocusId) return static_cast<int>(index);
    }
    return -1;
}

bool Engine::choiceMenuActive() const {
    return !m_paused && !m_storyRuntime.empty() && !m_activeChoiceButtons.empty() &&
           !selectableChoiceIndices().empty();
}

void Engine::moveChoiceFocus(int direction) {
    if (!choiceMenuActive() || direction == 0) return;
    const auto indices = selectableChoiceIndices();
    const int current = focusedChoiceIndex();
    if (current < 0) {
        // First Down -> first selectable option, first Up -> last one.
        m_choiceFocusId = m_activeChoiceButtons[direction > 0 ? indices.front() : indices.back()].optionId;
        return;
    }
    std::size_t position = 0;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (static_cast<int>(indices[i]) == current) position = i;
    }
    const auto count = static_cast<long long>(indices.size());
    const long long next = ((static_cast<long long>(position) + (direction > 0 ? 1 : -1)) % count + count) % count;
    m_choiceFocusId = m_activeChoiceButtons[indices[static_cast<std::size_t>(next)]].optionId;
}

bool Engine::activateFocusedChoice() {
    // Without a focus Enter/Space stay what they were: a plain advance, which only
    // completes a typing line and otherwise is ignored ("waiting for a choice").
    if (!choiceMenuActive() || focusedChoiceIndex() < 0) return false;
    // Like a click beside the buttons: the first press finishes the typing line,
    // the next one answers the menu.
    if (completeTypewriterIfTyping()) {
        m_autoAdvanceElapsed = 0.0f;
        resetAutoWait();
        return true;
    }
    const int index = focusedChoiceIndex();
    if (index < 0) return false;
    // Copy: a successful choice rebuilds m_activeChoiceButtons.
    const std::string optionId = m_activeChoiceButtons[static_cast<std::size_t>(index)].optionId;
    advanceToChoice(optionId);
    return true;
}

bool Engine::selectChoiceByNumber(int number) {
    // A digit is a deliberate act: it needs no focus.
    if (!choiceMenuActive()) return false;
    const auto indices = selectableChoiceIndices();
    // 0 and numbers past the last option are consumed: while a menu is up a
    // digit must not silently retarget the quick-save slot.
    if (number < 1 || static_cast<std::size_t>(number) > indices.size()) return true;
    const std::string optionId = m_activeChoiceButtons[indices[static_cast<std::size_t>(number) - 1]].optionId;
    advanceToChoice(optionId);
    return true;
}

void Engine::focusChoiceAt(float physicalX, float physicalY) {
    if (!m_window || !choiceMenuActive()) return;
    if (!std::isfinite(physicalX) || !std::isfinite(physicalY)) return;
    float x = 0.0f, y = 0.0f;
    bool bezelTap = false;
    if (!m_window->mapPhysicalToVirtual(physicalX, physicalY, 1920, 1080, x, y, bezelTap) || bezelTap) return;
    const auto laidOut = getLaidOutChoiceButtons();
    for (std::size_t i = laidOut.size(); i-- > 0;) {
        const auto& button = laidOut[i];
        if (!button.enabled || !choiceButtonSelectable(button)) continue;
        if (x >= button.x && x <= button.x + button.width &&
            y >= button.y && y <= button.y + button.height) {
            m_choiceFocusId = button.optionId;
            return;
        }
    }
}

std::vector<Rowl::Render::ChoiceButtonRenderData> Engine::getLaidOutChoiceButtons() const {
    auto buttons = m_activeChoiceButtons;
    if (buttons.empty()) return buttons;
    if (m_window) m_window->fitChoiceButtons(buttons);
    if (!m_paused) {
        const int focus = focusedChoiceIndex();
        if (focus >= 0) buttons[static_cast<std::size_t>(focus)].focused = true;
    }
    return buttons;
}

}  // namespace Rowl::Core
