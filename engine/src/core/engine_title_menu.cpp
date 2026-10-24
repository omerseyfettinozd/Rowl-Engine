// engine_title_menu.cpp — D02: native title screen (Yeni Oyun / Devam Et /
// Oyunu Yükle / Tercihler / Çıkış), slot-metadata memo and preference
// persistence for the pause/title menu.
//
// Title is a pause-menu mode (PauseMenuMode::Title/Preferences) that reuses
// the pause overlay, the LoadSlots page and the input funnel of
// engine_pause_menu.cpp. Esc/Back never start the story from the title.
// TU-local sozlesme: bu yordamlar kilit ALMAZ. Yeni `RowlEngine_` sembolu bu
// TU'da YOK (C API sarmalayicilari c_api_pause.cpp'de).

#include "rowl/core/engine.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

namespace Rowl::Core {

namespace {

constexpr size_t kDetailSummaryBytes = 48;

}  // namespace

std::string pauseSlotDateText(const std::string& savedAt) {
    // "2026-10-24T12:34:56Z" -> "2026-10-24 12:34"
    if (savedAt.size() >= 16 && savedAt[10] == 'T') {
        return savedAt.substr(0, 10) + " " + savedAt.substr(11, 5);
    }
    return savedAt;
}

std::string pauseSlotDetail(const PauseSlotInfo& info) {
    if (info.state != PauseSlotInfo::State::Valid) return "";
    std::string detail = pauseSlotDateText(info.savedAt);
    auto append = [&detail](const std::string& part) {
        if (part.empty()) return;
        if (!detail.empty()) detail += "  ";
        detail += part;
    };
    append(info.chapterTitle);
    std::string summary = info.summary;
    for (char& c : summary) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    if (summary.size() > kDetailSummaryBytes) {
        size_t cut = kDetailSummaryBytes;
        while (cut > 0 && (static_cast<unsigned char>(summary[cut]) & 0xC0) == 0x80) --cut;
        summary = summary.substr(0, cut) + "...";
    }
    append(summary);
    return detail;
}

void Engine::ensureSlotCache() const {
    if (m_pauseSlotCacheValid) return;
    // Rebuilt only when invalidated (menu open, save/delete, folder change):
    // the view is rebuilt every frame and must never parse save files then.
    const std::string saveDirectory = Rowl::Platform::pathToUtf8(getSaveDirectoryPath());
    m_pauseSlotInfo = {};
    m_continueSlot = -1;
    m_continueSavedAt.clear();
    // saved_at has one-second resolution; two saves within the same second
    // are ordered by the slot file's write time (newest wins).
    std::filesystem::file_time_type continueWriteTime{};
    auto slotWriteTime = [this](int32_t slot) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(
            getSaveDirectoryPath() / ("save_slot_" + std::to_string(slot) + ".json"), ec);
        return ec ? std::filesystem::file_time_type{} : time;
    };
    for (int32_t slot = 0; slot <= 99; ++slot) {
        if (!hasSaveSlot(slot)) continue;
        PauseSlotInfo info;
        const auto state = Rowl::State::GameState::loadFromSlot(slot, saveDirectory);
        if (!state || m_slotLoadFailed.test(static_cast<size_t>(slot))) {
            info.state = PauseSlotInfo::State::Corrupt;
        } else {
            info.state = PauseSlotInfo::State::Valid;
            info.savedAt = state->savedAt;
            info.chapterTitle = state->chapterTitle;
            info.summary = state->summary;
            info.playtimeSeconds = state->playtimeSeconds;
            if (!state->thumbnailPng.empty()) {
                info.thumbnailPng = std::make_shared<const std::string>(state->thumbnailPng);
            }
            // ISO-8601 UTC strings order lexicographically; equal stamps fall
            // back to the file write time, then keep the lower slot.
            const auto writeTime = slotWriteTime(slot);
            if (m_continueSlot < 0 || info.savedAt > m_continueSavedAt ||
                (info.savedAt == m_continueSavedAt && writeTime > continueWriteTime)) {
                m_continueSlot = slot;
                m_continueSavedAt = info.savedAt;
                continueWriteTime = writeTime;
            }
        }
        if (slot <= kPauseMenuQuickSlotMax) {
            m_pauseSlotInfo[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)] = std::move(info);
        }
    }
    m_pauseSlotCacheValid = true;
}

bool Engine::openTitleScreen() {
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot open the title screen: engine is not initialized",
                                "open_title_screen", "");
        }
        return false;
    }
    m_paused = true;
    m_titleActive = true;
    m_skipActive = false;  // D04-1: the title ends Skip; Auto's credit starts over
    m_skipHold = false;
    resetAutoWait();
    m_pauseMode = PauseMenuMode::Title;
    m_prefsFromPause = false;  // D05-2
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    m_pauseSlotCacheValid = false;
    ensureSlotCache();
    m_pauseSelected = m_continueSlot >= 0 ? 1 : 0;
    ROWL_LOG_INFO("[Player] Title screen open (continue slot: " +
                  std::to_string(m_continueSlot) + ").");
    return true;
}

void Engine::leaveTitleScreen() {
    setPaused(false);  // clears m_titleActive, hint and the menu navigation
}

void Engine::markPresentedDialoguesRead() {
    // D04-1: snapshot before marking (see recordActiveDialogueHistory). When the
    // presentation already recorded this node (New Game with play on: the reset
    // re-presents the start node through recordActiveDialogueHistory, which
    // marked it), the lines are "read" only because of that marking - keep the
    // snapshot taken then.
    if (m_lastRecordedDialogueNodeId != m_storyRuntime.currentNodeId()) refreshReadSnapshot();
    for (const auto& dlg : m_activeDialogues) {
        if (!dlg.dialogue.empty() && !dlg.contentId.empty()) {
            m_playerProfile.markRead(dlg.contentId);
        }
    }
}

void Engine::titleStartNewGame() {
    resetToStartNode();
    leaveTitleScreen();
    // The start lines become visible to the player only now (setPlayState
    // skipped them while the title was up).
    if (m_isPlaying) markPresentedDialoguesRead();
    ROWL_LOG_INFO("[Player] New game started from the title screen.");
}

bool Engine::titleLoadSlot(int32_t slotIndex) {
    ensureSlotCache();
    const PauseSlotInfo& info =
        m_pauseSlotInfo[static_cast<size_t>(slotIndex - kPauseMenuQuickSlotMin)];
    if (info.state == PauseSlotInfo::State::Empty) {
        m_titleHint = UiText::HintEmptySlot;
        return false;
    }
    if (info.state == PauseSlotInfo::State::Corrupt) {
        m_titleHint = UiText::HintCorruptSave;
        return false;
    }
    if (loadGameSlot(slotIndex)) {
        ROWL_LOG_INFO("[Player] Loaded slot #" + std::to_string(slotIndex) + " from the title screen.");
        leaveTitleScreen();
        return true;
    }
    m_slotLoadFailed.set(static_cast<size_t>(slotIndex));
    m_pauseSlotCacheValid = false;
    ensureSlotCache();
    m_titleHint = UiText::HintLoadFailed;
    return false;
}

bool Engine::titleContinue() {
    ensureSlotCache();
    if (m_continueSlot < 0) {
        m_titleHint = UiText::HintNoSaveToContinue;
        return false;
    }
    const int32_t slot = m_continueSlot;
    if (loadGameSlot(slot)) {
        ROWL_LOG_INFO("[Player] Continued from slot #" + std::to_string(slot) + ".");
        leaveTitleScreen();
        return true;
    }
    // The next Continue falls back to the next newest valid save.
    m_slotLoadFailed.set(static_cast<size_t>(slot));
    m_pauseSlotCacheValid = false;
    ensureSlotCache();
    m_titleHint = UiText::HintLoadFailed;
    return false;
}

void Engine::titleActivateSelected() {
    const bool quitRow = (m_pauseMode == PauseMenuMode::Title && m_pauseSelected == 4);
    if (!quitRow) m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    switch (m_pauseMode) {
        case PauseMenuMode::Title:
            switch (m_pauseSelected) {
                case 0: titleStartNewGame(); break;
                case 1: titleContinue(); break;
                case 2: m_pauseMode = PauseMenuMode::LoadSlots; m_pauseSelected = 0; break;
                case 3:
                    m_pauseMode = PauseMenuMode::Preferences;
                    m_prefsFromPause = false;  // D05-2: Back returns to the title
                    m_pauseSelected = 0;
                    break;
                case 4:
                    if (m_pauseConfirmQuit) {
                        ROWL_LOG_INFO("[Player] Exit confirmed from title screen.");
                        m_isRunning = false;
                    } else {
                        m_pauseConfirmQuit = true;
                    }
                    break;
                default: break;
            }
            break;
        case PauseMenuMode::Preferences:
            if (m_pauseSelected == PauseMenuLayout::kPreferenceRows - 1) {
                leavePreferencesPage();
            } else {
                pauseMenuAdjustSelected(+1);  // keyboard Confirm steps like Right
            }
            break;
        case PauseMenuMode::LoadSlots:
            menuChooseSlot(static_cast<int32_t>(m_pauseSelected));
            break;
        default: break;
    }
}

void Engine::titleBack() {
    m_titleHint = UiText::None;
    if (m_pauseConfirmQuit) {
        m_pauseConfirmQuit = false;
        return;
    }
    // The title root has nowhere to go back to: Esc must never start the story.
    if (m_pauseMode == PauseMenuMode::LoadSlots) {
        m_pauseMode = PauseMenuMode::Title;
        m_pauseSelected = 2;
    } else if (m_pauseMode == PauseMenuMode::Preferences) {
        leavePreferencesPage();
    }
}

void Engine::leavePreferencesPage() {
    // D05-2: the page remembers where it was opened from. The pause menu's
    // "Tercihler" row and the title's both land back on their own row.
    if (m_prefsFromPause) {
        m_prefsFromPause = false;
        m_pauseMode = PauseMenuMode::Main;
        m_pauseSelected = PauseMenuLayout::kMainPrefsRow;
    } else {
        m_pauseMode = PauseMenuMode::Title;
        m_pauseSelected = 3;
    }
    m_pauseConfirmQuit = false;
}

int Engine::pauseMenuValueRow() const {
    // Only the Preferences page has adjustable rows (D05-2: the pause main page
    // links to it). Page row r maps onto the value-row numbers r+3 used by
    // pauseMenuAdjustSelected / persistPauseMenuPreference: 3..6 the four
    // volumes, 7 text speed, then D04-2's 8 auto delay and 9 skip mode and
    // D05-2's 10 language, 11 text size, 12 high contrast, 13 reduced motion.
    // The last page row ("Geri") has no value.
    if (m_pauseMode == PauseMenuMode::Preferences) {
        return (m_pauseSelected >= 0 && m_pauseSelected <= PauseMenuLayout::kPreferenceMotionRow)
            ? m_pauseSelected + 3 : -1;
    }
    return -1;
}

void Engine::persistPauseMenuPreference(int row) {
    // A menu change is a player preference: mirror it into the profile. A
    // read-only / unmounted profile refuses silently (no error, no write).
    auto rounded = [](float v) { return std::round(static_cast<double>(v) * 100.0) / 100.0; };
    nlohmann::json patch = nlohmann::json::object();
    switch (row) {
        case 3: patch["master_volume"] = rounded(pauseMenuVolume(3)); break;
        case 4: patch["bgm_volume"] = rounded(pauseMenuVolume(4)); break;
        case 5: patch["sfx_volume"] = rounded(pauseMenuVolume(5)); break;
        case 6: patch["voice_volume"] = rounded(pauseMenuVolume(6)); break;
        case 7: patch["text_speed_multiplier"] = rounded(m_textSpeedMultiplier); break;
        // D04-2: preferences-page-only rows (see pauseMenuValueRow).
        case 8: patch["auto_advance_delay"] = rounded(m_autoDelay); break;
        case 9:
            patch["skip_mode"] = m_skipPolicy == SkipPolicy::Off ? "off"
                               : m_skipPolicy == SkipPolicy::All ? "all" : "read_only";
            break;
        // D05-2: language and accessibility rows. The stored value is read back
        // from the engine (what really took effect), never the requested one.
        case 10: patch["language"] = m_localization.getLocale(); break;
        case 11: patch["text_scale"] = rounded(textScale()); break;
        case 12: patch["high_contrast"] = highContrast(); break;
        case 13: patch["reduced_motion"] = reducedMotion(); break;
        default: return;
    }
    m_playerProfile.setPreferencesJson(patch.dump());
}

PauseMenuView Engine::getTitleMenuView() const {
    PauseMenuView view;
    view.open = m_paused;
    view.selected = m_pauseSelected;
    view.titleActive = m_titleActive;
    ensureSlotCache();
    view.continueSlot = m_continueSlot;
    const UiLang lang = getUiLanguage();
    auto T = [lang](UiText id) { return std::string(uiText(id, lang)); };
    if (m_pauseMode == PauseMenuMode::Preferences) {
        view.title = T(UiText::PreferencesTitle);
        view.rows = {
            {T(UiText::MasterVolume), pauseMenuPercent(pauseMenuVolume(3)), true},
            {T(UiText::Music), pauseMenuPercent(pauseMenuVolume(4)), true},
            {T(UiText::Sfx), pauseMenuPercent(pauseMenuVolume(5)), true},
            {T(UiText::Voice), pauseMenuPercent(pauseMenuVolume(6)), true},
            {T(UiText::TextSpeed), pauseMenuSpeedText(m_textSpeedMultiplier), true},
            {T(UiText::AutoSpeed), pauseMenuDelayText(m_autoDelay, lang), true, "", true},
            {T(UiText::Skip), T(m_skipPolicy == SkipPolicy::Off ? UiText::Off
                              : m_skipPolicy == SkipPolicy::All ? UiText::All : UiText::ReadOnly),
             true, "", true},
            {T(UiText::Language), preferenceLanguageText(), true, "", true},
            {T(UiText::TextSize), pauseMenuPercent(textScale()), true, "", true},
            {T(UiText::HighContrast), T(highContrast() ? UiText::On : UiText::Off), true, "", true},
            {T(UiText::ReduceMotion), T(reducedMotion() ? UiText::On : UiText::Off), true, "", true},
            {T(UiText::Back), "", false},
        };
        view.hint = T(UiText::HintPreferences);
        return view;
    }
    const std::string& name = m_gameIdentity.name;
    view.title = name.empty() ? "Rowl Engine" : name;
    PauseMenuRow cont;
    cont.label = T(UiText::Continue);
    if (m_continueSlot >= 0) {
        cont.value = T(UiText::SlotLabel) + " " + std::to_string(m_continueSlot) + "  " +
                     pauseSlotDateText(m_continueSavedAt);
    } else {
        cont.value = T(UiText::NoSaveValue);
        cont.enabled = false;
    }
    view.rows = {
        {T(UiText::NewGame), "", false},
        cont,
        {T(UiText::LoadGame), T(UiText::PickSlotValue), false},
        {T(UiText::Preferences), ">", false},
        {T(UiText::Quit), m_pauseConfirmQuit ? T(UiText::AreYouSure) : "", false},
    };
    if (m_pauseConfirmQuit) {
        view.hint = T(UiText::HintTitleQuit);
    } else if (m_titleHint != UiText::None) {
        view.hint = T(m_titleHint);
    } else {
        view.hint = T(UiText::HintTitle);
    }
    return view;
}

}  // namespace Rowl::Core
