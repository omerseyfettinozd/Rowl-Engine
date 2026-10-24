// engine_title_menu.cpp — D02: native title screen (Yeni Oyun / Devam Et /
// Oyunu Yukle / Tercihler / Cikis), slot-metadata memo and preference
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
    m_pauseConfirmQuit = false;
    m_titleHint.clear();
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
        m_titleHint = "bos yuva";
        return false;
    }
    if (info.state == PauseSlotInfo::State::Corrupt) {
        m_titleHint = "bozuk kayit";
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
    m_titleHint = "kayit yuklenemedi";
    return false;
}

bool Engine::titleContinue() {
    ensureSlotCache();
    if (m_continueSlot < 0) {
        m_titleHint = "devam icin kayit yok";
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
    m_titleHint = "kayit yuklenemedi";
    return false;
}

void Engine::titleActivateSelected() {
    const bool quitRow = (m_pauseMode == PauseMenuMode::Title && m_pauseSelected == 4);
    if (!quitRow) m_pauseConfirmQuit = false;
    m_titleHint.clear();
    switch (m_pauseMode) {
        case PauseMenuMode::Title:
            switch (m_pauseSelected) {
                case 0: titleStartNewGame(); break;
                case 1: titleContinue(); break;
                case 2: m_pauseMode = PauseMenuMode::LoadSlots; m_pauseSelected = 0; break;
                case 3: m_pauseMode = PauseMenuMode::Preferences; m_pauseSelected = 0; break;
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
                m_pauseMode = PauseMenuMode::Title;
                m_pauseSelected = 3;
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
    m_titleHint.clear();
    if (m_pauseConfirmQuit) {
        m_pauseConfirmQuit = false;
        return;
    }
    // The title root has nowhere to go back to: Esc must never start the story.
    if (m_pauseMode == PauseMenuMode::LoadSlots) {
        m_pauseMode = PauseMenuMode::Title;
        m_pauseSelected = 2;
    } else if (m_pauseMode == PauseMenuMode::Preferences) {
        m_pauseMode = PauseMenuMode::Title;
        m_pauseSelected = 3;
    }
}

int Engine::pauseMenuValueRow() const {
    // Main rows 3..7 are the adjustable ones; the preferences page lists the
    // same five first (row r -> main row r+3), then D04-2's two page-only value
    // rows (5 -> 8 auto delay, 6 -> 9 skip mode) followed by "Geri". Rows 8/9
    // only exist as value rows on the preferences page (on the main page they
    // are Gecmis / Ana Menu, which this never reports).
    if (m_pauseMode == PauseMenuMode::Main) {
        return (m_pauseSelected >= 3 && m_pauseSelected <= 7) ? m_pauseSelected : -1;
    }
    if (m_pauseMode == PauseMenuMode::Preferences) {
        return (m_pauseSelected >= 0 && m_pauseSelected <= PauseMenuLayout::kPreferenceSkipRow)
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
    if (m_pauseMode == PauseMenuMode::Preferences) {
        view.title = "Tercihler";
        view.rows = {
            {"Ana Ses", pauseMenuPercent(pauseMenuVolume(3)), true},
            {"Muzik", pauseMenuPercent(pauseMenuVolume(4)), true},
            {"SFX", pauseMenuPercent(pauseMenuVolume(5)), true},
            {"Seslendirme", pauseMenuPercent(pauseMenuVolume(6)), true},
            {"Metin Hizi", pauseMenuSpeedText(m_textSpeedMultiplier), true},
            {"Otomatik Hizi", pauseMenuDelayText(m_autoDelay), true, "", true},
            {"Atlama", m_skipPolicy == SkipPolicy::Off ? "Kapali"
                     : m_skipPolicy == SkipPolicy::All ? "Hepsi" : "Okunan", true, "", true},
            {"Geri", "", false},
        };
        view.hint = "YUKARI/ASAGI: sec   SOL/SAG: ayar   ESC: geri";
        return view;
    }
    const std::string& name = m_gameIdentity.name;
    view.title = name.empty() ? "Rowl Engine" : name;
    PauseMenuRow cont;
    cont.label = "Devam Et";
    if (m_continueSlot >= 0) {
        cont.value = "Yuva " + std::to_string(m_continueSlot) + "  " +
                     pauseSlotDateText(m_continueSavedAt);
    } else {
        cont.value = "kayit yok";
        cont.enabled = false;
    }
    view.rows = {
        {"Yeni Oyun", "", false},
        cont,
        {"Oyunu Yukle", "yuva sec >", false},
        {"Tercihler", ">", false},
        {"Cikis", m_pauseConfirmQuit ? "emin misin?" : "", false},
    };
    if (m_pauseConfirmQuit) {
        view.hint = "ENTER: cikisi onayla   ESC: vazgec";
    } else if (!m_titleHint.empty()) {
        view.hint = m_titleHint;
    } else {
        view.hint = "YUKARI/ASAGI: sec   ENTER: tamam";
    }
    return view;
}

}  // namespace Rowl::Core
