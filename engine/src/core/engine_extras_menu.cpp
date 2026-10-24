// engine_extras_menu.cpp — D07-2: the title screen's "Ekstralar" pages (Galeri,
// Muzik Odasi, Basarimlar) and the achievement toast.
//
// The pages are pause-menu modes (PauseMenuMode::Extras/Gallery/MusicRoom/
// Achievements) reached from the title only, so they reuse the overlay, the
// keyboard/pointer funnel of engine_pause_menu.cpp and the title Back rules.
// This TU carries the page state machine, the view/JSON builders and the toast.
//
// Read-only contract (docs/EXTRAS_UNLOCKS.md): nothing here writes a variable,
// GameState, save slot, read mark, history row or unlock. The only side effect
// outside the menu is the music room: it plays a catalog track straight on the
// audio engine's BGM channel (never through GameState, so activeBgm is
// untouched) and endExtrasSession() brings the previous sound back.
// TU-local: takes no lock. No `RowlEngine_` symbol (the C API wrapper lives in
// c_api_extras.cpp).

#include "rowl/core/engine.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace Rowl::Core {

namespace {

using LM = PauseMenuLayout;
using Rowl::State::UnlockKind;

constexpr float kToastSeconds = 3.0f;

const char* langTag(UiLang lang) { return lang == UiLang::English ? "en" : "tr"; }

// "2026-10-24T19:30:00Z" -> "2026-10-24".
std::string dateOnly(const std::string& iso) {
    return iso.size() >= 10 ? iso.substr(0, 10) : iso;
}

}  // namespace

// ── page state machine ──────────────────────────────────────────────────────

int Engine::extrasItemCount() const {
    switch (m_pauseMode) {
        case PauseMenuMode::Extras: return LM::kExtrasRootRows;
        case PauseMenuMode::Gallery: return static_cast<int>(m_extrasCatalog.gallery.size());
        case PauseMenuMode::MusicRoom: return static_cast<int>(m_extrasCatalog.music.size());
        case PauseMenuMode::Achievements: return static_cast<int>(m_extrasCatalog.achievements.size());
        default: return 0;
    }
}

void Engine::extrasEnsureVisible() const {
    const int total = extrasItemCount();
    if (m_pauseMode == PauseMenuMode::Extras || total <= 0) {
        m_extrasTop = 0;
        return;
    }
    const int sel = std::clamp(m_pauseSelected, 0, total - 1);
    int row = sel;
    int rows = total;
    int visible = m_pauseMode == PauseMenuMode::Achievements ? LM::kAchVisibleRows : LM::kExtrasListRows;
    if (m_pauseMode == PauseMenuMode::Gallery) {
        row = sel / LM::kGalleryColumns;
        rows = (total + LM::kGalleryColumns - 1) / LM::kGalleryColumns;
        visible = LM::kGalleryVisibleRows;
    }
    int top = std::clamp(m_extrasTop, 0, std::max(0, rows - visible));
    if (row < top) top = row;
    if (row >= top + visible) top = row - visible + 1;
    m_extrasTop = top;
}

void Engine::extrasMove(int direction) {
    if (!isExtrasMode(m_pauseMode) || m_galleryViewing) return;
    const int total = extrasItemCount();
    if (total <= 0) return;
    const int sel = std::clamp(m_pauseSelected, 0, total - 1);
    switch (m_pauseMode) {
        case PauseMenuMode::Extras:
            m_pauseSelected = (sel + direction + total) % total;
            break;
        case PauseMenuMode::Gallery: {
            // Up/Down move a whole grid row; the last row is reached from a
            // shorter column by landing on the final item. Never wraps.
            int target = sel + direction * LM::kGalleryColumns;
            if (target < 0) {
                target = sel;
            } else if (target >= total) {
                const bool lastRow = sel / LM::kGalleryColumns >= (total - 1) / LM::kGalleryColumns;
                target = lastRow ? sel : total - 1;
            }
            m_pauseSelected = target;
            break;
        }
        default:  // lists: one item, never wraps
            m_pauseSelected = std::clamp(sel + direction, 0, total - 1);
            break;
    }
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    extrasEnsureVisible();
}

void Engine::extrasAdjust(int direction) {
    if (m_pauseMode != PauseMenuMode::Gallery) return;
    if (m_galleryViewing) {
        galleryStepViewer(direction);
        return;
    }
    const int total = extrasItemCount();
    if (total <= 0) return;
    m_pauseSelected = std::clamp(std::clamp(m_pauseSelected, 0, total - 1) + direction, 0, total - 1);
    extrasEnsureVisible();
}

void Engine::galleryOpenViewer(int index) {
    const auto& gallery = m_extrasCatalog.gallery;
    if (index < 0 || index >= static_cast<int>(gallery.size())) return;
    if (!m_playerProfile.isUnlocked(UnlockKind::Cg, gallery[static_cast<size_t>(index)].id)) return;
    m_pauseSelected = index;
    m_galleryViewing = true;
    extrasEnsureVisible();
}

void Engine::galleryStepViewer(int direction) {
    // Previous / next UNLOCKED item, wrapping; a locked item is never shown.
    const auto& gallery = m_extrasCatalog.gallery;
    const int total = static_cast<int>(gallery.size());
    if (total <= 0 || direction == 0) return;
    const int step = direction > 0 ? 1 : -1;
    int index = std::clamp(m_pauseSelected, 0, total - 1);
    for (int tried = 0; tried < total; ++tried) {
        index = (index + step + total) % total;
        if (m_playerProfile.isUnlocked(UnlockKind::Cg, gallery[static_cast<size_t>(index)].id)) {
            m_pauseSelected = index;
            extrasEnsureVisible();
            return;
        }
    }
}

void Engine::musicRoomToggle(int index) {
    const auto& music = m_extrasCatalog.music;
    if (index < 0 || index >= static_cast<int>(music.size())) return;
    const auto& item = music[static_cast<size_t>(index)];
    if (!m_playerProfile.isUnlocked(UnlockKind::Music, item.id)) return;
    if (!m_audio) return;
    m_musicRoomTouchedAudio = true;
    if (m_musicPlayingId == item.id) {
        m_audio->stopBgm();
        m_musicPlayingId.clear();
        return;
    }
    // Straight on the BGM channel, with the project's own BGM transition when
    // it declares one. GameState is deliberately not told (activeBgm stays).
    playBgmWithDefaultTransition(item.track);
    const bool started = m_audio->isBgmPlaying() && m_audio->getCurrentBgmPath() == item.track;
    m_musicPlayingId = started ? item.id : std::string();
    if (!started) ROWL_LOG_WARN("[Extras] music room could not start track '" + item.track + "'");
}

void Engine::endExtrasSession() {
    const bool touched = m_musicRoomTouchedAudio;
    m_musicPlayingId.clear();
    m_musicRoomTouchedAudio = false;
    m_galleryViewing = false;
    m_extrasTop = 0;
    if (!touched || !m_audio) return;
    // Only a session that played (or stopped) a track re-sets the sound: the
    // GameState's mixer + BGM when there is one, silence on a bare title.
    if (m_gameState) restoreAudioStateFromGameState();
    else m_audio->stopBgm();
}

void Engine::extrasActivateSelected() {
    switch (m_pauseMode) {
        case PauseMenuMode::Extras:
            switch (m_pauseSelected) {
                case LM::kExtrasGalleryRow: m_pauseMode = PauseMenuMode::Gallery; break;
                case LM::kExtrasMusicRow: m_pauseMode = PauseMenuMode::MusicRoom; break;
                case LM::kExtrasAchievementsRow: m_pauseMode = PauseMenuMode::Achievements; break;
                case LM::kExtrasBackRow: extrasBack(); return;
                default: return;
            }
            m_pauseSelected = 0;
            m_extrasTop = 0;
            m_galleryViewing = false;
            break;
        case PauseMenuMode::Gallery:
            if (m_galleryViewing) m_galleryViewing = false;
            else galleryOpenViewer(m_pauseSelected);
            break;
        case PauseMenuMode::MusicRoom:
            musicRoomToggle(m_pauseSelected);
            break;
        default:  // achievements: a read-only list
            break;
    }
}

void Engine::extrasBack() {
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    if (m_galleryViewing) {  // the viewer closes first, the grid stays
        m_galleryViewing = false;
        return;
    }
    switch (m_pauseMode) {
        case PauseMenuMode::Gallery:
            m_pauseMode = PauseMenuMode::Extras;
            m_pauseSelected = LM::kExtrasGalleryRow;
            break;
        case PauseMenuMode::MusicRoom:  // the track keeps playing across pages
            m_pauseMode = PauseMenuMode::Extras;
            m_pauseSelected = LM::kExtrasMusicRow;
            break;
        case PauseMenuMode::Achievements:
            m_pauseMode = PauseMenuMode::Extras;
            m_pauseSelected = LM::kExtrasAchievementsRow;
            break;
        case PauseMenuMode::Extras:
            endExtrasSession();
            m_pauseMode = PauseMenuMode::Title;
            m_pauseSelected = LM::kTitleExtrasRow;
            return;
        default: return;
    }
    m_extrasTop = 0;
}

void Engine::extrasClick(float virtualX, float virtualY) {
    if (m_galleryViewing) {  // any click closes the full-screen viewer
        m_galleryViewing = false;
        return;
    }
    const int total = extrasItemCount();
    switch (m_pauseMode) {
        case PauseMenuMode::Extras: {
            const int row = LM::rowAt(virtualX, virtualY, LM::kExtrasRootRows);
            if (row < 0) return;
            m_pauseSelected = row;
            extrasActivateSelected();
            break;
        }
        case PauseMenuMode::Gallery: {
            int column = 0, visibleRow = 0;
            if (!LM::galleryCellAt(virtualX, virtualY, column, visibleRow)) return;
            const int index = (m_extrasTop + visibleRow) * LM::kGalleryColumns + column;
            if (index < 0 || index >= total) return;
            m_pauseSelected = index;
            galleryOpenViewer(index);  // a locked cell only gets selected
            extrasEnsureVisible();
            break;
        }
        case PauseMenuMode::MusicRoom: {
            const int visible = std::min(LM::kExtrasListRows, std::max(0, total - m_extrasTop));
            const int row = LM::rowAt(virtualX, virtualY, visible);
            if (row < 0) return;
            m_pauseSelected = m_extrasTop + row;
            extrasActivateSelected();
            break;
        }
        case PauseMenuMode::Achievements: {  // its own two-line row geometry
            const int visible = std::min(LM::kAchVisibleRows, std::max(0, total - m_extrasTop));
            const int row = LM::achRowAt(virtualX, virtualY, visible);
            if (row < 0) return;
            m_pauseSelected = m_extrasTop + row;  // a read-only list: selecting is all a click does
            break;
        }
        default: break;
    }
}

void Engine::extrasScroll(float deltaY) {
    if (deltaY == 0.0f || !std::isfinite(deltaY) || m_galleryViewing) return;
    if (m_pauseMode == PauseMenuMode::Extras) return;
    extrasMove(deltaY > 0.0f ? -1 : +1);  // wheel up = towards the first row
}

bool Engine::openExtras() {
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot open the extras: engine is not initialized", "open_extras", "");
        }
        return false;
    }
    if (m_extrasCatalog.empty()) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot open the extras: the game has no extras.json items",
                                "open_extras", "");
        }
        return false;
    }
    if (isExtrasOpen()) return true;
    if (!m_titleActive && !openTitleScreen()) return false;
    m_pauseMode = PauseMenuMode::Extras;
    m_pauseSelected = 0;
    m_extrasTop = 0;
    m_galleryViewing = false;
    m_pauseConfirmQuit = false;
    m_titleHint = UiText::None;
    ROWL_LOG_INFO("[Player] Extras open.");
    return true;
}

// ── view ────────────────────────────────────────────────────────────────────

PauseExtrasView Engine::buildExtrasView() const {
    PauseExtrasView view;
    if (!isExtrasMode(m_pauseMode)) return view;
    view.active = true;
    const UiLang lang = getUiLanguage();
    const char* tag = langTag(lang);
    const std::string locked = uiText(UiText::LockedMark, lang);
    const int total = extrasItemCount();
    view.total = total;
    const int sel = total > 0 ? std::clamp(m_pauseSelected, 0, total - 1) : -1;
    view.selected = sel;
    extrasEnsureVisible();
    view.scroll = m_extrasTop;

    const auto& profile = m_playerProfile;
    switch (m_pauseMode) {
        case PauseMenuMode::Extras: {
            view.page = "root";
            view.scroll = 0;
            return view;
        }
        case PauseMenuMode::Gallery: {
            view.page = "gallery";
            view.columns = LM::kGalleryColumns;
            const auto& gallery = m_extrasCatalog.gallery;
            for (const auto& item : gallery) {
                if (profile.isUnlocked(UnlockKind::Cg, item.id)) ++view.unlocked;
            }
            view.counter = total > 0 ? std::to_string(sel + 1) + " / " + std::to_string(total) : "0 / 0";
            for (int row = 0; row < LM::kGalleryVisibleRows; ++row) {
                for (int column = 0; column < LM::kGalleryColumns; ++column) {
                    const int index = (m_extrasTop + row) * LM::kGalleryColumns + column;
                    if (index >= total) break;
                    const auto& item = gallery[static_cast<size_t>(index)];
                    PauseGalleryCellView cell;
                    cell.index = index;
                    cell.x = LM::galleryCellX(column);
                    cell.y = LM::galleryCellY(row);
                    cell.w = LM::kGalleryCellW;
                    cell.h = LM::kGalleryCellH;
                    cell.unlocked = profile.isUnlocked(UnlockKind::Cg, item.id);
                    cell.selected = (index == sel);
                    if (cell.unlocked) {
                        cell.title = item.title.resolve(tag);
                        cell.thumbnail = item.thumbnail.empty() ? item.image : item.thumbnail;
                    } else {
                        cell.title = locked;  // no path: the renderer cannot leak it
                    }
                    view.cells.push_back(std::move(cell));
                }
            }
            if (m_galleryViewing && sel >= 0 &&
                profile.isUnlocked(UnlockKind::Cg, gallery[static_cast<size_t>(sel)].id)) {
                view.viewing = true;
                view.viewIndex = sel;
                view.viewImage = gallery[static_cast<size_t>(sel)].image;
                view.viewTitle = gallery[static_cast<size_t>(sel)].title.resolve(tag);
            }
            return view;
        }
        case PauseMenuMode::MusicRoom: {
            view.page = "music";
            const auto& music = m_extrasCatalog.music;
            for (const auto& item : music) {
                if (profile.isUnlocked(UnlockKind::Music, item.id)) ++view.unlocked;
            }
            view.playingId = m_musicPlayingId;
            view.counter = total > 0 ? std::to_string(sel + 1) + " / " + std::to_string(total) : "0 / 0";
            const int end = std::min(total, m_extrasTop + LM::kExtrasListRows);
            for (int index = m_extrasTop; index < end; ++index) {
                const auto& item = music[static_cast<size_t>(index)];
                PauseExtrasItemView entry;
                entry.index = index;
                entry.id = item.id;
                entry.unlocked = profile.isUnlocked(UnlockKind::Music, item.id);
                entry.selected = (index == sel);
                entry.playing = entry.unlocked && item.id == m_musicPlayingId;
                entry.title = entry.unlocked ? item.title.resolve(tag) : locked;
                view.items.push_back(std::move(entry));
            }
            return view;
        }
        default: {
            view.page = "achievements";
            const auto& list = m_extrasCatalog.achievements;
            for (const auto& item : list) {
                if (profile.isUnlocked(UnlockKind::Achievement, item.id)) ++view.unlocked;
            }
            view.counter = total > 0 ? std::to_string(sel + 1) + " / " + std::to_string(total) : "0 / 0";
            const int end = std::min(total, m_extrasTop + LM::kAchVisibleRows);
            for (int index = m_extrasTop; index < end; ++index) {
                const auto& item = list[static_cast<size_t>(index)];
                PauseExtrasItemView entry;
                entry.index = index;
                entry.id = item.id;
                entry.unlocked = profile.isUnlocked(UnlockKind::Achievement, item.id);
                entry.selected = (index == sel);
                // A locked entry shows its title (a goal to chase) but not its
                // description; a locked hidden one hides both.
                entry.title = (entry.unlocked || !item.hidden) ? item.title.resolve(tag) : locked;
                entry.detail = entry.unlocked ? item.description.resolve(tag) : locked;
                if (entry.unlocked) entry.at = dateOnly(profile.achievementTime(item.id));
                entry.y = LM::achRowTop(index - m_extrasTop);
                entry.h = LM::kAchRowH;
                view.items.push_back(std::move(entry));
            }
            return view;
        }
    }
}

PauseMenuView Engine::getExtrasMenuView() const {
    PauseMenuView view;
    view.open = m_paused;
    view.titleActive = m_titleActive;
    view.extras = buildExtrasView();
    const UiLang lang = getUiLanguage();
    auto T = [lang](UiText id) { return std::string(uiText(id, lang)); };
    const auto count = [](int unlocked, size_t total) {
        return std::to_string(unlocked) + "/" + std::to_string(total);
    };
    const auto& profile = m_playerProfile;
    const auto& catalog = m_extrasCatalog;
    auto unlockedCount = [&](UnlockKind kind, auto& list) {
        int n = 0;
        for (const auto& item : list) n += profile.isUnlocked(kind, item.id) ? 1 : 0;
        return n;
    };

    if (m_pauseMode == PauseMenuMode::Extras) {
        view.title = T(UiText::Extras);
        view.selected = m_pauseSelected;
        view.rows.push_back(makePauseRow(
            T(UiText::Gallery), count(unlockedCount(UnlockKind::Cg, catalog.gallery), catalog.gallery.size())));
        view.rows.push_back(makePauseRow(
            T(UiText::MusicRoom), count(unlockedCount(UnlockKind::Music, catalog.music), catalog.music.size())));
        view.rows.push_back(makePauseRow(
            T(UiText::Achievements),
            count(unlockedCount(UnlockKind::Achievement, catalog.achievements), catalog.achievements.size())));
        view.rows.push_back(makePauseRow(T(UiText::Back)));
        view.hint = T(UiText::HintExtras);
        return view;
    }

    const bool empty = view.extras.total == 0;
    if (m_pauseMode == PauseMenuMode::Gallery) {
        view.title = T(UiText::Gallery) + "  " + count(view.extras.unlocked, catalog.gallery.size());
        view.selected = view.extras.selected;
        view.hint = T(view.extras.viewing ? UiText::HintGalleryViewer : UiText::HintGallery);
    } else if (m_pauseMode == PauseMenuMode::MusicRoom) {
        view.title = T(UiText::MusicRoom) + "  " + count(view.extras.unlocked, catalog.music.size());
        view.hint = T(UiText::HintMusicRoom);
        for (const auto& item : view.extras.items) {
            PauseMenuRow row;
            row.label = item.title;
            row.value = item.playing ? T(UiText::NowPlaying) : std::string();
            row.enabled = item.unlocked;
            view.rows.push_back(std::move(row));
            if (item.selected) view.selected = static_cast<int>(view.rows.size()) - 1;
        }
    } else {
        view.title = T(UiText::Achievements) + "  " + count(view.extras.unlocked, catalog.achievements.size());
        view.hint = T(UiText::HintAchievements);
        // The two-line rows are drawn from extras.items (own geometry), not from
        // the generic one-line row list.
        view.selected = view.extras.selected;
    }
    if (empty) {
        PauseMenuRow none;
        none.label = T(UiText::NothingYet);
        none.enabled = false;
        view.rows.push_back(none);
        view.selected = -1;
    }
    return view;
}

std::string pauseExtrasViewJson(const PauseExtrasView& view) {
    using nlohmann::ordered_json;
    ordered_json out = ordered_json::object();
    out["page"] = view.page;
    out["total"] = view.total;
    out["unlocked"] = view.unlocked;
    out["selected"] = view.selected;
    out["scroll"] = view.scroll;
    out["counter"] = view.counter;
    if (view.page == "gallery") {
        out["columns"] = view.columns;
        out["viewing"] = view.viewing;
        out["view_index"] = view.viewIndex;
        out["view_title"] = view.viewTitle;
        ordered_json cells = ordered_json::array();
        for (const auto& cell : view.cells) {
            // A locked cell carries no image path at all.
            ordered_json entry = {{"index", cell.index},
                                  {"x", cell.x}, {"y", cell.y}, {"w", cell.w}, {"h", cell.h},
                                  {"unlocked", cell.unlocked},
                                  {"selected", cell.selected},
                                  {"title", cell.title}};
            if (!cell.thumbnail.empty()) entry["thumbnail"] = cell.thumbnail;
            cells.push_back(std::move(entry));
        }
        out["cells"] = std::move(cells);
    } else if (view.page == "music" || view.page == "achievements") {
        if (view.page == "music") out["playing"] = view.playingId;
        ordered_json items = ordered_json::array();
        for (const auto& item : view.items) {
            ordered_json entry = {{"index", item.index},
                                  {"id", item.id},
                                  {"unlocked", item.unlocked},
                                  {"selected", item.selected},
                                  {"title", item.title}};
            if (view.page == "music") {
                entry["playing"] = item.playing;
            } else {
                entry["y"] = item.y;
                entry["h"] = item.h;
                entry["description"] = item.detail;
                entry["at"] = item.at.empty() ? ordered_json(nullptr) : ordered_json(item.at);
            }
            items.push_back(std::move(entry));
        }
        out["items"] = std::move(items);
    }
    return out.dump(-1, ' ', false, ordered_json::error_handler_t::replace);
}

// ── achievement toast ───────────────────────────────────────────────────────

void Engine::updateUnlockToast(float deltaTime) {
    // The toast belongs to a running, unpaused story: any menu freezes the clock
    // and hides it (a queued notice simply waits), Auto / Skip do not matter.
    if (!m_isPlaying || m_paused) return;
    if (m_toastActive) {
        if (std::isfinite(deltaTime) && deltaTime > 0.0f) m_toastRemaining -= deltaTime;
        if (m_toastRemaining > 0.0f) return;
        m_toastActive = false;
    }
    // Take the next announceable notice off the queue (a notice whose
    // achievement left the catalog is dropped). Doing it in the same step as
    // an expiry leaves no clean frame between two toasts.
    while (!m_toastActive && !m_unlockNotices.empty()) {
        const UnlockNotice notice = m_unlockNotices.front();
        m_unlockNotices.erase(m_unlockNotices.begin());
        if (notice.kind != "achievement" || m_extrasCatalog.findAchievement(notice.id) == nullptr) continue;
        m_toastNotice = notice;
        m_toastRemaining = kToastSeconds;
        m_toastActive = true;
    }
}

std::string Engine::unlockToastText() const {
    if (!m_toastActive || !m_isPlaying || m_paused) return std::string();
    const auto* item = m_extrasCatalog.findAchievement(m_toastNotice.id);
    if (item == nullptr) return std::string();
    const UiLang lang = getUiLanguage();
    return std::string(uiText(UiText::ToastAchievement, lang)) + item->title.resolve(langTag(lang));
}

}  // namespace Rowl::Core
