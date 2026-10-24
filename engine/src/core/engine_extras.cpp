// engine_extras.cpp — D07-1: unlockable content (CG gallery, music room,
// achievements). The catalog (`Assets/json/extras.json`) says WHAT can be
// unlocked, the player profile (`unlocks`) records what WAS. This file loads the
// catalog, applies the unlock side of a real node entry and renders the state
// as JSON for the C API. The menus that show it are D07-2.
//
// Unlocks never touch the story: no GameState, variable, step, history row,
// read mark or audio is changed here - only the profile (and a notice queue).

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

namespace Rowl::Core {

namespace {

using Rowl::State::ProfileMutation;
using Rowl::State::UnlockKind;

constexpr std::size_t kMaxPendingNotices = 64;

const char* languageTag(UiLang lang) {
    return lang == UiLang::English ? "en" : "tr";
}

}  // namespace

void Engine::loadExtrasCatalog() {
    m_extrasCatalog = {};
    m_extrasCgKeys.clear();
    m_extrasMusicKeys.clear();
    m_unlockNotices.clear();

    auto* vfs = getVfs();
    if (vfs == nullptr) return;
    // Same probing order as the active-story file: the project's json/ folder
    // first (loose project and packaged game alike).
    static const char* const kCandidates[] = {"json/extras.json", "extras.json",
                                              "Assets/json/extras.json", "Assets/extras.json"};
    for (const char* candidate : kCandidates) {
        if (!vfs->exists(candidate)) continue;
        const std::vector<uint8_t> bytes = vfs->readBytes(candidate);
        m_extrasCatalog = Rowl::State::parseExtrasCatalog(
            std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        break;
    }
    for (const auto& line : m_extrasCatalog.diagnostics) ROWL_LOG_WARN("[Extras] " + line);
    for (const auto& item : m_extrasCatalog.gallery) {
        m_extrasCgKeys.push_back(Rowl::State::canonicalMediaPath(item.image, "images/"));
    }
    for (const auto& item : m_extrasCatalog.music) {
        m_extrasMusicKeys.push_back(Rowl::State::canonicalMediaPath(item.track, "audio/"));
    }
    if (!m_extrasCatalog.empty()) {
        ROWL_LOG_INFO("[Extras] catalog: " + std::to_string(m_extrasCatalog.gallery.size()) + " CG, " +
                      std::to_string(m_extrasCatalog.music.size()) + " music, " +
                      std::to_string(m_extrasCatalog.achievements.size()) + " achievements");
    }
}

ProfileMutation Engine::unlockExtra(UnlockKind kind, std::string_view id) {
    bool known = false;
    switch (kind) {
        case UnlockKind::Cg: known = m_extrasCatalog.findGallery(id) != nullptr; break;
        case UnlockKind::Music: known = m_extrasCatalog.findMusic(id) != nullptr; break;
        case UnlockKind::Achievement: known = m_extrasCatalog.findAchievement(id) != nullptr; break;
    }
    if (!known) return ProfileMutation::Invalid;
    const ProfileMutation result = m_playerProfile.unlock(kind, id);
    if (result == ProfileMutation::Ok && kind == UnlockKind::Achievement) {
        if (m_unlockNotices.size() >= kMaxPendingNotices) m_unlockNotices.erase(m_unlockNotices.begin());
        m_unlockNotices.push_back({Rowl::State::unlockKindName(kind), std::string(id)});
    }
    if (result == ProfileMutation::Ok) {
        ROWL_LOG_INFO(std::string("[Extras] unlocked ") + Rowl::State::unlockKindName(kind) + " '" +
                      std::string(id) + "'");
    }
    return result;
}

void Engine::applyEntryUnlocks(const std::vector<nlohmann::json>& unlockComponents,
                               const std::vector<nlohmann::json>& audioComponents) {
    if (m_extrasCatalog.empty() && unlockComponents.empty()) return;

    // Automatic CG: the node's background texture or a character sprite that is a
    // gallery image. Automatic music: the node's bgm_track that is a music item.
    std::vector<std::string> shownImages;
    if (m_hasBackground && !m_activeBackground.empty()) {
        shownImages.push_back(Rowl::State::canonicalMediaPath(m_activeBackground, "images/"));
    }
    for (const auto& character : m_activeCharacters) {
        if (!character.sprite.empty()) {
            shownImages.push_back(Rowl::State::canonicalMediaPath(character.sprite, "images/"));
        }
    }
    for (std::size_t i = 0; i < m_extrasCgKeys.size(); ++i) {
        for (const auto& shown : shownImages) {
            if (shown == m_extrasCgKeys[i]) {
                unlockExtra(UnlockKind::Cg, m_extrasCatalog.gallery[i].id);
                break;
            }
        }
    }
    for (const auto& data : audioComponents) {
        const auto bgmIt = data.find("bgm_track");
        if (bgmIt == data.end() || !bgmIt->is_string() || bgmIt->get_ref<const std::string&>().empty()) {
            continue;
        }
        const std::string key = Rowl::State::canonicalMediaPath(bgmIt->get<std::string>(), "audio/");
        for (std::size_t i = 0; i < m_extrasMusicKeys.size(); ++i) {
            if (key == m_extrasMusicKeys[i]) unlockExtra(UnlockKind::Music, m_extrasCatalog.music[i].id);
        }
    }

    // Explicit {"type":"unlock","data":{"kind":"cg|music|achievement","id":"..."}}.
    const uint64_t nodeId = m_storyRuntime.currentNodeId();
    for (const auto& data : unlockComponents) {
        const auto kindIt = data.find("kind");
        const auto idIt = data.find("id");
        std::string problem;
        if (kindIt == data.end() || !kindIt->is_string() || idIt == data.end() || !idIt->is_string()) {
            problem = "needs string 'kind' and 'id'";
        } else if (const auto kind = Rowl::State::parseUnlockKind(kindIt->get<std::string>())) {
            const std::string id = idIt->get<std::string>();
            const ProfileMutation result = unlockExtra(*kind, id);
            if (result == ProfileMutation::Invalid) {
                problem = std::string("names ") + Rowl::State::unlockKindName(*kind) + " '" + id.substr(0, 64) +
                          "' which is not in extras.json";
            }
        } else {
            problem = "has an unknown kind (use cg, music or achievement)";
        }
        if (!problem.empty() && m_unlockWarnedNodes.insert(nodeId).second) {
            ROWL_LOG_WARN("Node #" + std::to_string(nodeId) + " unlock component " + problem +
                          "; nothing was unlocked");
        }
    }
}

std::string Engine::getExtrasJson() const {
    using nlohmann::ordered_json;
    const char* lang = languageTag(getUiLanguage());
    ordered_json out = ordered_json::object();
    out["format"] = Rowl::State::kExtrasFormat;
    out["loaded"] = m_extrasCatalog.fileFound;
    out["language"] = lang;
    out["profile_status"] = Rowl::State::playerProfileStatusName(m_playerProfile.status());
    out["diagnostics"] = m_extrasCatalog.diagnostics;

    ordered_json gallery = ordered_json::array();
    std::size_t galleryUnlocked = 0;
    for (const auto& item : m_extrasCatalog.gallery) {
        const bool unlocked = m_playerProfile.isUnlocked(UnlockKind::Cg, item.id);
        galleryUnlocked += unlocked ? 1u : 0u;
        gallery.push_back({{"id", item.id},
                           {"title", item.title.resolve(lang)},
                           {"image", item.image},
                           {"thumbnail", item.thumbnail.empty() ? item.image : item.thumbnail},
                           {"unlocked", unlocked}});
    }
    ordered_json music = ordered_json::array();
    std::size_t musicUnlocked = 0;
    for (const auto& item : m_extrasCatalog.music) {
        const bool unlocked = m_playerProfile.isUnlocked(UnlockKind::Music, item.id);
        musicUnlocked += unlocked ? 1u : 0u;
        music.push_back({{"id", item.id},
                         {"title", item.title.resolve(lang)},
                         {"track", item.track},
                         {"unlocked", unlocked}});
    }
    ordered_json achievements = ordered_json::array();
    std::size_t achievementsUnlocked = 0;
    for (const auto& item : m_extrasCatalog.achievements) {
        const bool unlocked = m_playerProfile.isUnlocked(UnlockKind::Achievement, item.id);
        achievementsUnlocked += unlocked ? 1u : 0u;
        ordered_json row = {{"id", item.id},
                            {"title", item.title.resolve(lang)},
                            {"description", item.description.resolve(lang)},
                            {"hidden", item.hidden},
                            {"unlocked", unlocked}};
        const std::string at = unlocked ? m_playerProfile.achievementTime(item.id) : std::string();
        row["at"] = at.empty() ? ordered_json(nullptr) : ordered_json(at);
        achievements.push_back(std::move(row));
    }
    out["gallery"] = std::move(gallery);
    out["music"] = std::move(music);
    out["achievements"] = std::move(achievements);
    out["counts"] = {{"gallery", {{"total", m_extrasCatalog.gallery.size()}, {"unlocked", galleryUnlocked}}},
                     {"music", {{"total", m_extrasCatalog.music.size()}, {"unlocked", musicUnlocked}}},
                     {"achievements",
                      {{"total", m_extrasCatalog.achievements.size()}, {"unlocked", achievementsUnlocked}}}};
    ordered_json notices = ordered_json::array();
    for (const auto& notice : m_unlockNotices) notices.push_back({{"kind", notice.kind}, {"id", notice.id}});
    out["pending_notices"] = std::move(notices);
    return out.dump(-1, ' ', false, ordered_json::error_handler_t::replace);
}

}  // namespace Rowl::Core
