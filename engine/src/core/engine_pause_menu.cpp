// engine_pause_menu.cpp — W8-d(1): pause-menu state-machine + view/JSON sunumu.
//
// engine.cpp'den birebir tasinmistir (D15 deseni):
//   2965-2978  Engine::setPaused
//   2980-2990  Engine::pauseMenuRowCount / pauseMenuMoveSelection
//   2992-3005  Engine::menuChooseSlot
//   3007-3031  Engine::menuActivateSelected
//   3033-3045  Engine::menuBack
//   3047-3057  Engine::pauseMenuCommand
//   3059-3078  Engine::pauseMenuClick
//   3080-3089  pauseMenuPercent / pauseMenuSpeedText (TU-local yardimcilar)
//   3091-3136  Engine::getPauseMenuView
//   3138-3155  appendPauseMenuJsonString (TU-local JSON kacisi)
//   3157-3186  Engine::getPauseMenuJson
// Kosul, sira, metinler ve JSON alan sirasi birebirdir. D15'in aldigi
// mixer-dortlusu engine_pause_mixer.cpp'de kaldi; bu TU yalnizca kalan
// kohezif kumeyi tasir. PauseMenuLayout::rowAt/adjustDirection zaten ayrik.
// TU-local sozlesme: bu yordamlar kilit ALMAZ (D13/D14 ile celismez).
// Yeni export YOK, `RowlEngine_` sembolu YOK (motor-ici Engine uyeleri).

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Rowl::Core {

void Engine::setPaused(bool paused) {
    if (m_paused == paused) return;
    endExtrasSession();  // D07-2: a music-room track never outlives the menu
    m_paused = paused;
    // D04-1: opening any menu (pause / backlog / preferences) ends Skip.
    if (paused) {
        m_skipActive = false;
        m_skipHold = false;
    }
    // D02: a host closing the menu also closes the title screen; the hint
    // of the previous session never leaks into a fresh menu.
    if (!paused) m_titleActive = false;
    m_titleHint = UiText::None;
    // Opening always lands on a predictable main page; closing resumes.
    // Pausing never quits — exit requires the two-step menu confirmation.
    m_pauseMode = PauseMenuMode::Main;
    m_pauseSelected = 0;
    m_pauseConfirmQuit = false;
    m_backlogTop = 0;  // D03-2: a fresh open never inherits backlog state
    m_backlogFromPause = false;
    m_prefsFromPause = false;  // D05-2
    // A2b: a fresh menu open rebuilds slot occupancy — saves or deletes may
    // have happened out-of-band while unpaused (editor, quick keys).
    m_pauseSlotCacheValid = false;
    ROWL_LOG_INFO(paused ? "[Player] Paused — menu open (Esc/P to resume)."
                         : "[Player] Resumed.");
}

int Engine::pauseMenuRowCount() const {
    switch (m_pauseMode) {
        case PauseMenuMode::Main: return PauseMenuLayout::kMainRows;
        case PauseMenuMode::Title: return titleRowCount();
        case PauseMenuMode::Extras:
        case PauseMenuMode::Gallery:
        case PauseMenuMode::MusicRoom:
        case PauseMenuMode::Achievements: return extrasItemCount();
        case PauseMenuMode::Preferences: return PauseMenuLayout::kPreferenceRows;
        case PauseMenuMode::Backlog: return static_cast<int>(getDialogueHistory().size());
        default: return PauseMenuLayout::kSlotRows;
    }
}

void Engine::pauseMenuMoveSelection(int direction) {
    if (m_pauseMode == PauseMenuMode::Backlog) {  // D03-2: a list, never wraps
        backlogMoveSelection(direction);
        return;
    }
    if (isExtrasMode(m_pauseMode)) {  // D07-2: grid / lists never wrap
        extrasMove(direction);
        return;
    }
    const int count = pauseMenuRowCount();
    if (count <= 0) return;
    m_pauseSelected = (m_pauseSelected + direction + count) % count;
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
}

void Engine::menuChooseSlot(int32_t slotIndex) {
    if (m_pauseMode != PauseMenuMode::SaveSlots &&
        m_pauseMode != PauseMenuMode::LoadSlots) return;
    if (slotIndex < kPauseMenuQuickSlotMin || slotIndex > kPauseMenuQuickSlotMax) return;
    if (m_titleActive) {  // D02: title Load page leaves the title on success
        titleLoadSlot(slotIndex);
        return;
    }
    if (m_pauseMode == PauseMenuMode::SaveSlots) {
        if (saveGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Saved to slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    } else {
        if (loadGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Loaded slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    }
    // Stay paused on the slot page so occupancy refreshes visibly.
}

void Engine::menuActivateSelected() {
    if (!m_paused) return;
    if (m_titleActive || m_pauseMode == PauseMenuMode::Title ||
        m_pauseMode == PauseMenuMode::Preferences || isExtrasMode(m_pauseMode)) {
        titleActivateSelected();
        return;
    }
    if (m_pauseMode == PauseMenuMode::Backlog) {
        backlogActivateSelected();
        return;
    }
    if (m_pauseMode != PauseMenuMode::Main) {
        menuChooseSlot(static_cast<int32_t>(m_pauseSelected));
        return;
    }
    switch (m_pauseSelected) {
        case 0: setPaused(false); break;
        case 1: m_pauseMode = PauseMenuMode::SaveSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case 2: m_pauseMode = PauseMenuMode::LoadSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case PauseMenuLayout::kMainPrefsRow:
            // D05-2: the shared Preferences page opens over the pause menu and
            // Back / Esc return to this row.
            m_pauseMode = PauseMenuMode::Preferences;
            m_prefsFromPause = true;
            m_pauseSelected = 0;
            m_pauseConfirmQuit = false;
            break;
        case PauseMenuLayout::kMainBacklogRow:
            // D03-2: the backlog opens over the pause menu and Back returns here.
            m_pauseMode = PauseMenuMode::Backlog;
            m_backlogFromPause = true;
            m_pauseConfirmQuit = false;
            m_pauseSelected = std::max<int>(0, static_cast<int>(getDialogueHistory().size()) - 1);
            m_backlogTop = m_pauseSelected;
            backlogEnsureVisible();
            break;
        case PauseMenuLayout::kMainTitleRow:
            // D02: back to the title screen; unsaved progress is lost, so it
            // takes the same two-step confirmation as exit.
            if (m_pauseConfirmQuit) {
                ROWL_LOG_INFO("[Player] Returning to the title screen from pause menu.");
                openTitleScreen();
            } else {
                m_pauseConfirmQuit = true;
            }
            break;
        case PauseMenuLayout::kMainExitRow:
            if (m_pauseConfirmQuit) {
                ROWL_LOG_INFO("[Player] Exit confirmed from pause menu.");
                m_isRunning = false;
            } else {
                m_pauseConfirmQuit = true;
            }
            break;
        default: break;
    }
}

void Engine::menuBack() {
    if (!m_paused) return;
    if (m_titleActive) {  // D02: Back never starts the story from the title
        titleBack();
        return;
    }
    if (m_pauseMode == PauseMenuMode::Backlog) {  // D03-2
        backlogClose();
        return;
    }
    if (isExtrasMode(m_pauseMode)) {  // D07-2
        extrasBack();
        return;
    }
    if (m_pauseConfirmQuit) {
        m_pauseConfirmQuit = false;
        return;
    }
    if (m_pauseMode == PauseMenuMode::Preferences) {  // D05-2: back to its opener's row
        leavePreferencesPage();
        return;
    }
    if (m_pauseMode != PauseMenuMode::Main) {
        m_pauseMode = PauseMenuMode::Main;
        m_pauseSelected = 0;
        return;
    }
    setPaused(false);
}

void Engine::pauseMenuCommand(PauseMenuCommand command) {
    if (!m_paused) return;
    switch (command) {
        case PauseMenuCommand::Up: pauseMenuMoveSelection(-1); break;
        case PauseMenuCommand::Down: pauseMenuMoveSelection(+1); break;
        case PauseMenuCommand::Left: pauseMenuAdjustSelected(-1); break;
        case PauseMenuCommand::Right: pauseMenuAdjustSelected(+1); break;
        case PauseMenuCommand::Back: menuBack(); break;
        case PauseMenuCommand::Confirm: menuActivateSelected(); break;
    }
}

void Engine::pauseMenuClick(float virtualX, float virtualY) {
    if (!m_paused) return;
    if (m_pauseMode == PauseMenuMode::Backlog) {  // D03-2: variable-height list
        backlogClick(virtualX, virtualY);
        return;
    }
    if (isExtrasMode(m_pauseMode)) {  // D07-2: grid cells / scrolled lists
        extrasClick(virtualX, virtualY);
        return;
    }
    const int row = PauseMenuLayout::rowAt(virtualX, virtualY, pauseMenuRowCount());
    if (row < 0) return; // gaps and outside miss; selection is kept
    const bool alreadySelected = (row == m_pauseSelected);
    m_pauseSelected = row;
    // A confirmation armed on one row never carries over to another row
    // (keyboard moves disarm the same way in pauseMenuMoveSelection).
    if (!alreadySelected) m_pauseConfirmQuit = false;
    if (pauseMenuValueRow() >= 0) {
        const int dir = PauseMenuLayout::adjustDirection(virtualX);
        if (dir != 0) {
            pauseMenuAdjustSelected(dir);
            return;
        }
        // Middle band only selects (a second tap confirms).
        if (!alreadySelected) {
            m_pauseConfirmQuit = false;
            return;
        }
    }
    menuActivateSelected();
}

std::string pauseMenuPercent(float volume) {
    return std::to_string(static_cast<int>(volume * 100.0f + 0.5f)) + "%";
}

std::string pauseMenuSpeedText(float multiplier) {
    const int quarters = static_cast<int>(multiplier * 4.0f + 0.5f);
    const int whole = quarters / 4;
    const int frac = (quarters % 4) * 25;
    return std::to_string(whole) + "." + (frac < 10 ? "0" : "") + std::to_string(frac) + "x";
}

std::string pauseMenuDelayText(float seconds, UiLang lang) {
    const int tenths = static_cast<int>(seconds * 10.0f + 0.5f);
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " " +
           uiText(UiText::SecondsUnit, lang);
}

PauseMenuView Engine::getPauseMenuView() const {
    PauseMenuView view;
    view.open = m_paused;
    if (!m_paused) return view;
    if (m_pauseMode == PauseMenuMode::Title || m_pauseMode == PauseMenuMode::Preferences) {
        return getTitleMenuView();
    }
    if (isExtrasMode(m_pauseMode)) return getExtrasMenuView();  // D07-2
    view.selected = m_pauseSelected;
    view.titleActive = m_titleActive;
    const UiLang lang = getUiLanguage();
    auto T = [lang](UiText id) { return std::string(uiText(id, lang)); };
    if (m_pauseMode == PauseMenuMode::Backlog) {
        // D03-2: entries (visible window) ride in view.backlog; an empty history
        // shows one dim line instead and selects nothing.
        view.title = T(UiText::History);
        view.backlog = buildBacklogView();
        view.selected = view.backlog.selected;
        if (view.backlog.total == 0) {
            PauseMenuRow empty;
            empty.label = T(UiText::HistoryEmpty);
            empty.enabled = false;
            view.rows.push_back(empty);
        }
        const bool voiced = view.backlog.selected >= 0 &&
            !getDialogueHistory()[static_cast<size_t>(view.backlog.selected)].voice.empty();
        view.hint = T(voiced ? UiText::HintBacklogVoiced : UiText::HintBacklog);
        return view;
    }
    if (m_pauseMode == PauseMenuMode::Main) {
        const bool confirmTitle =
            m_pauseConfirmQuit && m_pauseSelected == PauseMenuLayout::kMainTitleRow;
        const bool confirmExit = m_pauseConfirmQuit && !confirmTitle;
        view.title = T(UiText::Paused);
        // D-K3: makePauseRow names every field (aggregate rows tripped
        // -Wmissing-field-initializers); the rows are unchanged.
        view.rows = {
            makePauseRow(T(UiText::Resume), ""),
            makePauseRow(T(UiText::SaveGame), T(UiText::PickSlotValue)),
            makePauseRow(T(UiText::LoadGame), T(UiText::PickSlotValue)),
            makePauseRow(T(UiText::Preferences), ">", false, "", true),
            makePauseRow(T(UiText::History), T(UiText::LinesValue), false, "", true),
            makePauseRow(T(UiText::MainMenu), confirmTitle ? T(UiText::AreYouSure) : ""),
            makePauseRow(T(UiText::Quit), confirmExit ? T(UiText::AreYouSure) : ""),
        };
        view.hint = confirmTitle ? T(UiText::HintConfirmMainMenu)
                  : confirmExit ? T(UiText::HintConfirmQuit)
                  : T(UiText::HintPauseMain);
    } else {
        const bool saving = (m_pauseMode == PauseMenuMode::SaveSlots);
        view.title = T(saving ? UiText::SaveSlotTitle : UiText::LoadSlotTitle);
        // A2b: occupancy is memoized (see m_pauseSlotCacheValid) — a slot
        // page holds 10 rows and this view rebuilds per UI refresh.
        static_assert(kPauseMenuQuickSlotMax - kPauseMenuQuickSlotMin + 1 ==
                      10, "pause slot cache assumes the 0..9 quick-slot window");
        ensureSlotCache();
        for (int32_t slot = kPauseMenuQuickSlotMin; slot <= kPauseMenuQuickSlotMax; ++slot) {
            const PauseSlotInfo& info =
                m_pauseSlotInfo[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)];
            PauseMenuRow row;
            row.label = T(UiText::SlotLabel) + " " + std::to_string(slot);
            row.value = T(info.state == PauseSlotInfo::State::Valid ? UiText::SlotFull
                        : info.state == PauseSlotInfo::State::Corrupt ? UiText::SlotCorrupt
                        : UiText::SlotEmpty);
            row.detail = pauseSlotDetail(info);
            view.rows.push_back(row);
        }
        const PauseSlotInfo& selectedInfo = m_pauseSlotInfo[static_cast<size_t>(
            std::clamp(m_pauseSelected, 0, kPauseMenuQuickSlotMax))];
        if (selectedInfo.state == PauseSlotInfo::State::Valid && selectedInfo.thumbnailPng &&
            !selectedInfo.thumbnailPng->empty()) {
            view.thumbnailPng = selectedInfo.thumbnailPng;
            view.thumbnailKey = std::to_string(m_pauseSelected) + "|" + selectedInfo.savedAt;
        }
        view.hint = (m_titleActive && m_titleHint != UiText::None)
            ? T(m_titleHint)
            : T(UiText::HintSlots);
    }
    view.continueSlot = m_pauseSlotCacheValid ? m_continueSlot : -1;
    return view;
}

static void appendPauseMenuJsonString(std::string& out, const std::string& text) {
    out.push_back('"');
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

std::string Engine::getPauseMenuJson() const {
    const PauseMenuView view = getPauseMenuView();
    std::string out = "{\"open\":";
    out += view.open ? "true" : "false";
    out += ",\"mode\":";
    out += std::to_string(static_cast<int>(m_pauseMode));
    out += ",\"selected\":";
    out += std::to_string(view.selected);
    // D02: one armed flag serves both confirm rows; JSON tells them apart.
    const bool confirmTitle = m_pauseConfirmQuit && m_pauseMode == PauseMenuMode::Main &&
                              m_pauseSelected == PauseMenuLayout::kMainTitleRow;
    out += ",\"confirm_quit\":";
    out += (m_pauseConfirmQuit && !confirmTitle) ? "true" : "false";
    out += ",\"quick_slot\":";
    out += std::to_string(m_activeQuickSlot);
    out += ",\"title\":";
    appendPauseMenuJsonString(out, view.title);
    out += ",\"hint\":";
    appendPauseMenuJsonString(out, view.hint);
    out += ",\"rows\":[";
    for (size_t i = 0; i < view.rows.size(); ++i) {
        if (i > 0) out.push_back(',');
        out += "{\"label\":";
        appendPauseMenuJsonString(out, view.rows[i].label);
        out += ",\"value\":";
        appendPauseMenuJsonString(out, view.rows[i].value);
        out += ",\"selected\":";
        out += (static_cast<int>(i) == view.selected) ? "true" : "false";
        out += ",\"detail\":";
        appendPauseMenuJsonString(out, view.rows[i].detail);
        out += ",\"enabled\":";
        out += view.rows[i].enabled ? "true" : "false";
        out.push_back('}');
    }
    out += "],\"title_active\":";
    out += view.titleActive ? "true" : "false";
    out += ",\"continue_slot\":";
    out += std::to_string(view.continueSlot);
    out += ",\"confirm_title\":";
    out += confirmTitle ? "true" : "false";
    out += ",\"selected_has_thumbnail\":";
    out += view.thumbnailPng ? "true" : "false";
    if (view.backlog.active) {
        // D03-2: only the visible window rides along (history can hold 500
        // rows of up to 64 KiB); `total`/`scroll`/`selected` index the full history.
        out += ",\"backlog\":{\"total\":";
        out += std::to_string(view.backlog.total);
        out += ",\"selected\":";
        out += std::to_string(view.backlog.selected);
        out += ",\"scroll\":";
        out += std::to_string(view.backlog.scroll);
        out += ",\"counter\":";
        appendPauseMenuJsonString(out, view.backlog.counter);
        out += ",\"entries\":[";
        for (size_t i = 0; i < view.backlog.entries.size(); ++i) {
            const auto& entry = view.backlog.entries[i];
            if (i > 0) out.push_back(',');
            out += "{\"index\":";
            out += std::to_string(entry.index);
            out += ",\"speaker\":";
            appendPauseMenuJsonString(out, entry.speaker);
            out += ",\"dialogue\":";
            appendPauseMenuJsonString(out, entry.dialogue);
            out += ",\"has_voice\":";
            out += entry.hasVoice ? "true" : "false";
            out += ",\"selected\":";
            out += entry.selected ? "true" : "false";
            out.push_back('}');
        }
        out += "]}";
    }
    if (view.extras.active) {
        // D07-2: Ekstralar page state (gallery cells / list window); a locked
        // gallery cell carries no image path.
        out += ",\"extras\":";
        out += pauseExtrasViewJson(view.extras);
    }
    out += "}";
    return out;
}

}  // namespace Rowl::Core
