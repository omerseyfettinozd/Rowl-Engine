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
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>

namespace Rowl::Core {

namespace {

constexpr size_t kDetailSummaryBytes = 48;

}  // namespace

namespace {

bool isDigits(const std::string& s, size_t pos, size_t count) {
    if (pos + count > s.size()) return false;
    for (size_t i = pos; i < pos + count; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

int twoDigits(const std::string& s, size_t pos) { return (s[pos] - '0') * 10 + (s[pos + 1] - '0'); }

// "YYYY-MM-DDTHH:MM[:SS[.fff]]Z" (UTC) -> local broken-down time. False for
// anything else (legacy "Z"-less stamps, garbage), so callers keep the old
// truncating display for those.
bool utcIsoToLocal(const std::string& iso, std::tm& local) {
    if (iso.size() < 17 || iso.back() != 'Z' || iso[10] != 'T' || iso[4] != '-' || iso[7] != '-' ||
        iso[13] != ':' || !isDigits(iso, 0, 4) || !isDigits(iso, 5, 2) || !isDigits(iso, 8, 2) ||
        !isDigits(iso, 11, 2) || !isDigits(iso, 14, 2)) {
        return false;
    }
    int second = 0;
    size_t pos = 16;
    if (iso[pos] == ':') {
        if (!isDigits(iso, pos + 1, 2)) return false;
        second = twoDigits(iso, pos + 1);
        pos += 3;
        if (pos < iso.size() && iso[pos] == '.') {
            ++pos;
            const size_t start = pos;
            while (pos < iso.size() && iso[pos] >= '0' && iso[pos] <= '9') ++pos;
            if (pos == start) return false;
        }
    }
    if (pos + 1 != iso.size()) return false;  // only the trailing "Z" may remain
    std::tm utc{};
    utc.tm_year = std::stoi(iso.substr(0, 4)) - 1900;
    utc.tm_mon = twoDigits(iso, 5) - 1;
    utc.tm_mday = twoDigits(iso, 8);
    utc.tm_hour = twoDigits(iso, 11);
    utc.tm_min = twoDigits(iso, 14);
    utc.tm_sec = second;
    if (utc.tm_mon < 0 || utc.tm_mon > 11 || utc.tm_mday < 1 || utc.tm_mday > 31 || utc.tm_hour > 23 ||
        utc.tm_min > 59 || utc.tm_sec > 60) {
        return false;
    }
#if defined(_WIN32)
    const std::time_t t = _mkgmtime(&utc);
#else
    const std::time_t t = timegm(&utc);
#endif
    if (t == static_cast<std::time_t>(-1)) return false;
#if defined(_WIN32)
    return localtime_s(&local, &t) == 0;
#else
    return localtime_r(&t, &local) != nullptr;
#endif
}

}  // namespace

std::string utcIsoToLocalText(const std::string& iso, bool dateOnly) {
    std::tm local{};
    if (utcIsoToLocal(iso, local)) {
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), dateOnly ? "%Y-%m-%d" : "%Y-%m-%d %H:%M", &local);
        return std::string(buffer);
    }
    // Legacy / unparsable stamp: show it as stored (truncated), as before.
    if (dateOnly) return iso.size() >= 10 ? iso.substr(0, 10) : iso;
    if (iso.size() >= 16 && iso[10] == 'T') return iso.substr(0, 10) + " " + iso.substr(11, 5);
    return iso;
}

std::string pauseSlotDateText(const std::string& savedAt) {
    // "2026-10-24T12:34:56Z" (UTC on disk) -> local "2026-10-24 15:34".
    return utcIsoToLocalText(savedAt, false);
}

// "<chapter>  <summary ~48 bytes>" of a valid slot (the part of the detail after the date).
static std::string pauseSlotSummary(const PauseSlotInfo& info) {
    std::string out;
    auto append = [&out](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out += "  ";
        out += part;
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
    return out;
}

std::string pauseSlotDetail(const PauseSlotInfo& info) {
    if (info.state != PauseSlotInfo::State::Valid) return "";
    std::string detail = pauseSlotDateText(info.savedAt);
    const std::string rest = pauseSlotSummary(info);
    if (!rest.empty()) {
        if (!detail.empty()) detail += "  ";
        detail += rest;
    }
    return detail;
}

// T2b: the pager strip and (card layout) the cards of the visible slot page.
void Engine::fillSlotPageView(PauseMenuView& view, const MenuLayout& layout) const {
    const int pages = slotPageCount();
    const UiLang lang = getUiLanguage();
    if (pages > 1) {
        view.pager.active = true;
        view.pager.page = m_slotPage;
        view.pager.pageCount = pages;
        view.pager.label = std::string("< ") + uiText(UiText::SlotPage, lang) + " " +
                           std::to_string(m_slotPage + 1) + " / " + std::to_string(pages) + " >";
        view.pager.x = layout.pagerX;
        view.pager.y = layout.pagerY;
        view.pager.w = layout.pagerW;
        view.pager.h = layout.pagerH;
    }
    if (!layout.cards) return;
    for (int row = 0; row < static_cast<int>(view.rows.size()); ++row) {
        const int slot = slotAtRow(row);
        if (slot < 0) break;
        const PauseSlotInfo& info = m_pauseSlotInfo[static_cast<size_t>(slot)];
        const PauseMenuRow& source = view.rows[static_cast<size_t>(row)];
        PauseSlotCardView card;
        card.row = row;
        card.slot = slot;
        card.x = layout.cardX(row);
        card.y = layout.cardY(row);
        card.w = layout.cardW();
        card.h = layout.cardH;
        card.slotState = source.slotState;
        card.selected = row == view.selected;
        card.label = source.label;
        card.value = source.value;
        if (info.state == PauseSlotInfo::State::Valid) {
            card.date = pauseSlotDateText(info.savedAt);
            card.summary = pauseSlotSummary(info);
            if (info.thumbnailPng && !info.thumbnailPng->empty()) {
                card.thumbnailPng = info.thumbnailPng;
                card.thumbnailKey = std::to_string(slot) + "|" + info.savedAt;
            }
        }
        view.cards.push_back(std::move(card));
    }
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
        if (slot < m_saveSlotCount) {  // T2b: the pages list project save_slot_count slots (1..100)
            m_pauseSlotInfo[static_cast<size_t>(slot)] = std::move(info);
        }
    }
    m_pauseSlotCacheValid = true;
}

// ── T2b: slot pages (save_slot_count slots, paged) ───────────────────────────

void Engine::setSaveSlotCount(int count) {
    m_saveSlotCount = std::clamp(count, 1, 100);
    m_pauseSlotCacheValid = false;
    m_slotPage = 0;
}

namespace {
int slotCapacity(const MenuLayout& layout) {
    return std::max(1, layout.cards ? layout.cardColumns * layout.cardVisibleRows() : layout.slotsPerPage);
}
}  // namespace

int Engine::slotPageCount() const {
    const int per = slotCapacity(activeMenuLayout());
    return std::max(1, (m_saveSlotCount + per - 1) / per);
}

int Engine::slotRowsOnPage() const {
    const int per = slotCapacity(activeMenuLayout());
    const int pages = std::max(1, (m_saveSlotCount + per - 1) / per);
    m_slotPage = std::clamp(m_slotPage, 0, pages - 1);
    return std::min(per, m_saveSlotCount - m_slotPage * per);
}

int Engine::slotAtRow(int row) const {
    const int per = slotCapacity(activeMenuLayout());
    const int rows = slotRowsOnPage();
    if (row < 0 || row >= rows) return -1;
    return m_slotPage * per + row;
}

void Engine::slotSetPage(int page) {
    const int pages = slotPageCount();
    const int target = std::clamp(page, 0, pages - 1);
    if (target == m_slotPage) return;
    m_slotPage = target;
    m_pauseSelected = std::clamp(m_pauseSelected, 0, std::max(0, slotRowsOnPage() - 1));
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
}

void Engine::slotPageMove(int direction) {
    slotSetPage(m_slotPage + (direction > 0 ? 1 : -1));
}

// Card grid: a step along a row (dx) or across rows (dy); stepping off the page edge turns
// the page (the same column / the neighbouring card) and never wraps around the end.
void Engine::slotCardsMove(int dx, int dy) {
    const MenuLayout layout = activeMenuLayout();
    const int per = slotCapacity(layout);
    const int columns = std::max(1, layout.cardColumns);
    const int rows = slotRowsOnPage();
    int absolute = m_slotPage * per + std::clamp(m_pauseSelected, 0, std::max(0, rows - 1));
    int target = absolute + dx + dy * columns;
    if (dy != 0) {
        // A shorter last line: land on the final card instead of dropping the move.
        if (target >= m_saveSlotCount && absolute / columns < (m_saveSlotCount - 1) / columns) {
            target = m_saveSlotCount - 1;
        }
    }
    target = std::clamp(target, 0, m_saveSlotCount - 1);
    m_slotPage = target / per;
    m_pauseSelected = target % per;
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
}

std::string Engine::slotNumberText(int slot) const {
    return std::to_string(slot + (m_uiTheme.slots.numberBase >= 0.5f ? 1 : 0));
}

int Engine::titleRowCount() const {
    return m_extrasCatalog.empty() ? PauseMenuLayout::kTitleRows : PauseMenuLayout::kTitleRowsWithExtras;
}

int Engine::titlePrefsRow() const {
    return m_extrasCatalog.empty() ? PauseMenuLayout::kTitleExtrasRow : PauseMenuLayout::kTitleExtrasRow + 1;
}

bool Engine::openTitleScreen() {
    endExtrasSession();  // D07-2: a re-opened title never keeps a music-room track
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
    m_titleSessionSeen = true;  // D-K3: this session returns to the title when the story ends
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
    startTitleMusic();  // T2
    ROWL_LOG_INFO("[Player] Title screen open (continue slot: " +
                  std::to_string(m_continueSlot) + ").");
    return true;
}

void Engine::leaveTitleScreen() {
    setPaused(false);  // clears m_titleActive, hint and the menu navigation (and ends the title music)
}

// T2: the theme's title music (title_screen.music). It plays on the BGM channel with the
// project's own transition; GameState is not told, like the music room.
void Engine::startTitleMusic() {
    const std::string& track = m_uiTheme.titleScreen.music;
    if (track.empty() || !m_audio) return;
    playBgmWithDefaultTransition(track);
    m_titleMusicStarted = true;
    if (!(m_audio->isBgmPlaying() && m_audio->getCurrentBgmPath() == track)) {
        ROWL_LOG_WARN("[UiTheme] title music '" + track + "' could not start");
    }
}

// Leaving the title: a story scene that wants BGM has already replaced (or kept) the track;
// a story with none must not inherit the title music.
void Engine::endTitleMusic() {
    if (!m_titleMusicStarted) return;
    m_titleMusicStarted = false;
    if (!m_audio) return;
    const bool storyWantsBgm = m_gameState && !m_gameState->activeBgm.empty();
    if (!storyWantsBgm && m_audio->isBgmPlaying() && m_audio->getCurrentBgmPath() == m_uiTheme.titleScreen.music) {
        m_audio->stopBgm();
    }
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
    const PauseSlotInfo& info = m_pauseSlotInfo[static_cast<size_t>(std::clamp(slotIndex, 0, 99))];
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
    // D07-2: with an extras catalog the title is New / Continue / Load / Extras /
    // Preferences / Quit; without one the older five rows (Extras row absent).
    const bool hasExtras = !m_extrasCatalog.empty();
    const int extrasRow = hasExtras ? PauseMenuLayout::kTitleExtrasRow : -1;
    const int prefsRow = titlePrefsRow();
    const int quitRowIndex = titleRowCount() - 1;
    const bool quitRow = (m_pauseMode == PauseMenuMode::Title && m_pauseSelected == quitRowIndex);
    if (!quitRow) m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    switch (m_pauseMode) {
        case PauseMenuMode::Title:
            if (m_pauseSelected == 0) {
                titleStartNewGame();
            } else if (m_pauseSelected == 1) {
                titleContinue();
            } else if (m_pauseSelected == 2) {
                m_pauseMode = PauseMenuMode::LoadSlots;
                m_pauseSelected = 0;
                m_slotPage = 0;
            } else if (m_pauseSelected == extrasRow) {
                m_pauseMode = PauseMenuMode::Extras;
                m_pauseSelected = 0;
                m_extrasTop = 0;
                m_galleryViewing = false;
            } else if (m_pauseSelected == prefsRow) {
                m_pauseMode = PauseMenuMode::Preferences;
                m_prefsFromPause = false;  // D05-2: Back returns to the title
                m_pauseSelected = 0;
            } else if (m_pauseSelected == quitRowIndex) {
                if (m_pauseConfirmQuit) {
                    ROWL_LOG_INFO("[Player] Exit confirmed from title screen.");
                    m_isRunning = false;
                } else {
                    m_pauseConfirmQuit = true;
                }
            }
            break;
        case PauseMenuMode::Extras:
        case PauseMenuMode::Gallery:
        case PauseMenuMode::MusicRoom:
        case PauseMenuMode::Achievements:
            extrasActivateSelected();  // D07-2
            break;
        case PauseMenuMode::Preferences:
            if (m_pauseSelected == PauseMenuLayout::kPreferenceRows - 1) {
                leavePreferencesPage();
            } else {
                pauseMenuAdjustSelected(+1);  // keyboard Confirm steps like Right
            }
            break;
        case PauseMenuMode::LoadSlots:
            menuChooseSlot(slotAtRow(std::clamp(m_pauseSelected, 0, std::max(0, slotRowsOnPage() - 1))));
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
    } else if (isExtrasMode(m_pauseMode)) {
        extrasBack();  // D07-2: one level up; the root returns to the title row
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
        m_pauseSelected = titlePrefsRow();
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
        view.page = "preferences";
        view.title = T(UiText::PreferencesTitle);
        view.rows = {
            {T(UiText::MasterVolume), pauseMenuPercent(pauseMenuVolume(3)), true, "", true},
            {T(UiText::Music), pauseMenuPercent(pauseMenuVolume(4)), true, "", true},
            {T(UiText::Sfx), pauseMenuPercent(pauseMenuVolume(5)), true, "", true},
            {T(UiText::Voice), pauseMenuPercent(pauseMenuVolume(6)), true, "", true},
            {T(UiText::TextSpeed), pauseMenuSpeedText(m_textSpeedMultiplier), true, "", true},
            {T(UiText::AutoSpeed), pauseMenuDelayText(m_autoDelay, lang), true, "", true},
            {T(UiText::Skip), T(m_skipPolicy == SkipPolicy::Off ? UiText::Off
                              : m_skipPolicy == SkipPolicy::All ? UiText::All : UiText::ReadOnly),
             true, "", true},
            {T(UiText::Language), preferenceLanguageText(), true, "", true},
            {T(UiText::TextSize), pauseMenuPercent(textScale()), true, "", true},
            {T(UiText::HighContrast), T(highContrast() ? UiText::On : UiText::Off), true, "", true},
            {T(UiText::ReduceMotion), T(reducedMotion() ? UiText::On : UiText::Off), true, "", true},
            {T(UiText::Back), "", false, "", true},
        };
        // T2: slider position of the numeric rows (volumes, text speed, auto delay).
        for (int row = 0; row < 6; ++row) {
            view.rows[static_cast<size_t>(row)].fraction = pauseMenuFraction(row + 3);
            view.rows[static_cast<size_t>(row)].dragging = (m_sliderDragRow == row + 3);
        }
        view.hint = T(UiText::HintPreferences);
        return view;
    }
    view.page = m_pauseConfirmQuit ? "confirm" : "title";
    const std::string& name = m_gameIdentity.name;
    view.title = name.empty() ? "Rowl Engine" : name;
    PauseMenuRow cont;
    cont.label = T(UiText::Continue);
    if (m_continueSlot >= 0) {
        cont.value = T(UiText::SlotLabel) + " " + slotNumberText(m_continueSlot) + "  " +
                     pauseSlotDateText(m_continueSavedAt);
    } else {
        cont.value = T(UiText::NoSaveValue);
        cont.enabled = false;
    }
    view.rows.push_back(makePauseRow(T(UiText::NewGame)));
    view.rows.push_back(cont);
    view.rows.push_back(makePauseRow(T(UiText::LoadGame), T(UiText::PickSlotValue)));
    // D07-2: "Ekstralar" sits before Tercihler, only when the game has extras.
    if (!m_extrasCatalog.empty()) view.rows.push_back(makePauseRow(T(UiText::Extras), ">"));
    view.rows.push_back(makePauseRow(T(UiText::Preferences), ">"));
    view.rows.push_back(makePauseRow(T(UiText::Quit), m_pauseConfirmQuit ? T(UiText::AreYouSure) : ""));
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
