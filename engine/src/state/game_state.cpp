#include "rowl/state/game_state.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/core/story_graph.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/state/save_metadata.hpp"
#include "rowl/state/session_persistence.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace Rowl::State {

namespace {

constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSaveVariables = 10'000;
constexpr size_t kMaxVariableKeyBytes = 256;
constexpr size_t kMaxVariableValueBytes = 64 * 1024;
constexpr size_t kMaxDialogueHistoryEntries = 500;
constexpr size_t kMaxDialogueHistoryTextBytes = 64 * 1024;
// Faz 2 content ids are UUIDs (36 chars); the cap only bounds hostile input.
constexpr size_t kMaxContentIdBytes = 1024;
// Faz 2 Dilim 4: downscaled thumbnails stay far below this; the cap only
// bounds hostile slot files (the 4 MiB slot cap still applies first).
constexpr size_t kMaxThumbnailBase64Bytes = 1024 * 1024;
// A2b: nlohmann::json::parse recurses per nesting level with no depth limit
// of its own — a hostile slot of megabytes of "[[[..." exhausts the thread
// stack (SIGSEGV, uncatchable) before any field budget is reached. Legit
// saves nest ~6 deep (object > dialogue_history > entry > scalars); 128 is
// generous. String-aware linear pre-scan: quotes and backslash escapes are
// skipped so brackets inside dialogue text never count.
constexpr size_t kMaxSaveNestingDepth = 128;

bool saveNestingWithinBudget(std::string_view text) {
    size_t depth = 0;
    bool inString = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') {
                ++i;  // skip the escaped byte, whatever it is
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxSaveNestingDepth) return false;
        } else if (c == ']' || c == '}') {
            if (depth > 0) --depth;
        }
    }
    return true;
}

// B7 (#25/#30 reserve dual-reality): bridge and stdlib names must never enter
// saved state. The sandbox rejects them on the Lua side (isReservedVariableName
// in lua_sandbox.cpp — this mirror must stay in sync with it); these two
// createNextState* functions are the write-path choke-point, so the filter
// lives here and not in every caller. Grandfathered slot files that already
// contain such keys still load (decodeJson is untouched) — only new writes
// are refused.
bool isReservedStateKey(const std::string& key) {
    static const std::unordered_set<std::string_view> kReserved = {
        "rowl", "_G", "_ENV",
        "math", "string", "table", "coroutine", "utf8",
        "package", "io", "os", "debug",
        "dofile", "loadfile", "load", "collectgarbage", "require", "module",
    };
    return key.empty() || kReserved.find(key) != kReserved.end();
}

// D08 (a): tek-state obje kodlayıcı. Aktif state withThumbnail=true ile
// yazılır (mevcut tel format birebir korunur); "history" halkaları
// withThumbnail=false ile yazılır (thumbnail yalnızca aktif state'te kalır,
// 6. bölüm 768KB kilit payı korunur).
nlohmann::json encodeStateObject(const GameState& s, bool withThumbnail) {
    nlohmann::json j;
    j["version"] = GameState::CurrentSaveFormatVersion;
    j["step_id"] = s.stepId;
    j["active_node_id"] = s.activeNodeId;
    j["typewriter_index"] = s.typewriterIndex;
    j["active_background"] = s.activeBackground;
    j["dsp_filter"] = s.dspFilter;
    j["active_bgm"] = s.activeBgm;
    j["bgm_volume"] = s.bgmVolume;
    j["bgm_playing"] = s.bgmPlaying;
    // #86 (v4): full mixer. v3 and older readers ignore unknown keys, so old
    // builds still load v4 files (mixer falls back to 1.0 there).
    j["master_volume"] = s.masterVolume;
    j["sfx_volume"] = s.sfxVolume;
    j["voice_volume"] = s.voiceVolume;
    nlohmann::json history = nlohmann::json::array();
    if (s.dialogueHistory) {
        for (const auto& entry : *s.dialogueHistory) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
                {"content_id", entry.contentId},
            });
        }
    }
    j["dialogue_history"] = std::move(history);

    nlohmann::json varObj = nlohmann::json::object();
    if (s.variables) {
        for (const auto& [k, v] : s.variables->data) {
            varObj[k] = v;
        }
    }
    j["variables"] = varObj;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    // Faz 2 Dilim 4 display-only save metadata (optional on decode).
    j["saved_at"] = Rowl::State::iso8601UtcNow();
    // D4/G (#70): graph content identity. Written only when stamped (legacy
    // saves have no key → decode defaults to "" → legacy-warn path on load).
    // No format bump: additive optional key, v3 readers ignore unknowns.
    if (!s.graphIdentity.empty()) j["graph_id"] = s.graphIdentity;
    j["playtime_seconds"] = s.playtimeSeconds;
    j["chapter_id"] = s.chapterId;
    j["chapter_title"] = s.chapterTitle;
    j["summary"] = s.summary;
    if (withThumbnail) {
        j["thumbnail_width"] = s.thumbnailWidth;
        j["thumbnail_height"] = s.thumbnailHeight;
        j["thumbnail_png_base64"] =
            Rowl::State::base64Encode(
                reinterpret_cast<const uint8_t*>(s.thumbnailPng.data()),
                static_cast<uint32_t>(s.thumbnailPng.size()));
    }
    return j;
}

// D08 (a): tek-state obje çözümleyici. Kök ve her "history" halkası aynı
// doğrulamadan geçer (bütçeler + fail-closed alanlar birebir); geçersiz
// girdi nullptr döner. "history" anahtarına bakmaz (tek seviye — iç içe
// history yoksayılır). previousState'e dokunmaz (zincirleme çağrıcının işi).
std::shared_ptr<GameState> decodeStateObject(const nlohmann::json& j, uint32_t version) {
    auto state = std::make_shared<GameState>();
    state->stepId = j.value("step_id", static_cast<uint64_t>(1));
    state->activeNodeId = j.value("active_node_id", static_cast<uint64_t>(101));
    state->typewriterIndex = j.value("typewriter_index", static_cast<uint32_t>(0));
    state->activeBackground = j.value("active_background", "bg_beach_sunset.png");
    state->dspFilter = j.value("dsp_filter", "Normal");
    state->activeBgm = j.value("active_bgm", "");
    state->bgmVolume = j.value("bgm_volume", 1.0f);
    state->bgmPlaying = j.value("bgm_playing", !state->activeBgm.empty());
    // #86 (v4): mixer keys are optional — legacy saves predate them.
    state->masterVolume = j.value("master_volume", 1.0f);
    state->sfxVolume = j.value("sfx_volume", 1.0f);
    state->voiceVolume = j.value("voice_volume", 1.0f);

    if (!std::isfinite(state->bgmVolume) || state->bgmVolume < 0.0f || state->bgmVolume > 1.0f) {
        ROWL_LOG_ERROR("GameState JSON contains an invalid BGM volume");
        return nullptr;
    }
    // #86: mixer volumes follow the same fail-closed contract as bgmVolume.
    for (const auto [label, value] : {
             std::pair{"master_volume", state->masterVolume},
             std::pair{"sfx_volume", state->sfxVolume},
             std::pair{"voice_volume", state->voiceVolume},
         }) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
            ROWL_LOG_ERROR(std::string("GameState JSON contains an invalid ") + label);
            return nullptr;
        }
    }

    if (state->stepId == 0 || state->activeNodeId == 0) {
        ROWL_LOG_ERROR("GameState JSON contains an invalid step or node identifier");
        return nullptr;
    }

    auto varMap = std::make_shared<VariableMap>();
    if (j.contains("variables") && j["variables"].is_object()) {
        if (j["variables"].size() > kMaxSaveVariables) {
            ROWL_LOG_ERROR("GameState JSON has too many variables");
            return nullptr;
        }
        for (auto& el : j["variables"].items()) {
            if (el.key().empty() || el.key().size() > kMaxVariableKeyBytes) {
                ROWL_LOG_ERROR("GameState JSON contains an invalid variable key");
                return nullptr;
            }
            std::string value;
            if (el.value().is_string()) {
                value = el.value().get<std::string>();
            } else {
                value = el.value().dump();
            }
            if (value.size() > kMaxVariableValueBytes) {
                ROWL_LOG_ERROR("GameState JSON contains an oversized variable value");
                return nullptr;
            }
            varMap->data[el.key()] = std::move(value);
        }
    } else if (j.contains("variables")) {
        ROWL_LOG_ERROR("GameState JSON variables must be an object");
        return nullptr;
    }
    state->variables = varMap;

    // Faz 2 Dilim 4 display metadata: all optional, legacy saves decode
    // to empty/zero. playtime must be finite and non-negative.
    state->savedAt = j.value("saved_at", "");
    state->playtimeSeconds = j.value("playtime_seconds", 0.0);
    if (!std::isfinite(state->playtimeSeconds) || state->playtimeSeconds < 0.0) {
        ROWL_LOG_ERROR("GameState JSON contains an invalid playtime");
        return nullptr;
    }
    state->chapterId = j.value("chapter_id", "");
    state->chapterTitle = j.value("chapter_title", "");
    state->summary = j.value("summary", "");
    if (state->chapterId.size() > 1024 || state->chapterTitle.size() > 1024 ||
        state->summary.size() > 4096) {
        ROWL_LOG_ERROR("GameState JSON contains oversized save metadata");
        return nullptr;
    }
    state->thumbnailWidth = j.value("thumbnail_width", static_cast<uint32_t>(0));
    state->thumbnailHeight = j.value("thumbnail_height", static_cast<uint32_t>(0));
    const std::string thumbnailBase64 = j.value("thumbnail_png_base64", "");
    if (thumbnailBase64.size() > kMaxThumbnailBase64Bytes) {
        ROWL_LOG_ERROR("GameState JSON contains an oversized thumbnail");
        return nullptr;
    }
    state->thumbnailPng.clear();
    if (!thumbnailBase64.empty() &&
        !Rowl::State::base64Decode(thumbnailBase64, state->thumbnailPng)) {
        ROWL_LOG_ERROR("GameState JSON contains a malformed thumbnail");
        return nullptr;
    }

    // D4/G (#70): graph identity is optional — legacy saves predate it.
    // Oversized values are hostile/foreign input → InvalidData.
    state->graphIdentity = j.value("graph_id", "");
    if (state->graphIdentity.size() > Rowl::Core::kMaxGraphIdentityBytes) {
        ROWL_LOG_ERROR("GameState JSON contains an oversized graph identity");
        return nullptr;
    }

    auto history = std::make_shared<std::vector<DialogueHistoryEntry>>();
    if (j.contains("dialogue_history")) {
        if (!j["dialogue_history"].is_array() ||
            j["dialogue_history"].size() > kMaxDialogueHistoryEntries) {
            ROWL_LOG_ERROR("GameState JSON dialogue history is invalid or too large");
            return nullptr;
        }
        for (const auto& rawEntry : j["dialogue_history"]) {
            if (!rawEntry.is_object()) {
                ROWL_LOG_ERROR("GameState JSON dialogue history entry must be an object");
                return nullptr;
            }
            DialogueHistoryEntry entry;
            entry.nodeId = rawEntry.value("node_id", uint64_t{0});
            entry.speaker = rawEntry.value("speaker", "");
            entry.dialogue = rawEntry.value("dialogue", "");
            entry.read = rawEntry.value("read", true);
            entry.contentId = rawEntry.value("content_id", "");
            if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
                entry.dialogue.size() > kMaxDialogueHistoryTextBytes ||
                entry.contentId.size() > kMaxContentIdBytes) {
                ROWL_LOG_ERROR("GameState JSON contains an invalid dialogue history entry");
                return nullptr;
            }
            history->push_back(std::move(entry));
        }
    }
    state->dialogueHistory = std::move(history);
    state->previousState = nullptr;
    return state;
}

// D08 (a): opsiyonel "history" dizisini öncül zincire çevirir. Başarısızlık
// (eksik/yabancı/bozuk/sınır-aşan/monoton-olmayan) nullptr döner — çağrıcı
// bugünkü davranışa düşer (previousState=nullptr), kök decode başarısını
// etkilemez, InvalidData'ya düşürmez.
std::shared_ptr<GameState> decodeHistoryChain(const nlohmann::json& raw,
                                              const GameState& root,
                                              uint32_t version) {
    if (!raw.is_array() || raw.empty() ||
        raw.size() > GameState::kMaxSerializedHistoryEntries) {
        return nullptr;
    }
    std::vector<std::shared_ptr<GameState>> links;
    links.reserve(raw.size());
    for (const auto& rawEntry : raw) {
        if (!rawEntry.is_object()) return nullptr;
        auto entry = decodeStateObject(rawEntry, version);
        if (!entry) return nullptr;
        links.push_back(std::move(entry));
    }
    // stepId monotonluğu: kökten geçmişe kesin azalan sırada olmalı
    // (serialize azalan sırada yazar; withMixerVolumes/withSaveMetadata/
    // withGraphIdentity kopya-üzeri olduğundan ara halka üretmez).
    uint64_t newerStep = root.stepId;
    for (const auto& link : links) {
        if (link->stepId >= newerStep) return nullptr;
        newerStep = link->stepId;
    }
    for (size_t i = 0; i + 1 < links.size(); ++i) {
        links[i]->previousState = links[i + 1];
    }
    return links.front();
}

} // namespace

std::string GameState::getVariable(const std::string& key, const std::string& defaultValue) const {
    if (!variables) return defaultValue;
    auto it = variables->data.find(key);
    if (it != variables->data.end()) {
        return it->second;
    }
    return defaultValue;
}

std::shared_ptr<const GameState> GameState::createInitialState(uint64_t startNodeId) {
    auto state = std::make_shared<GameState>();
    state->stepId = 1;
    state->activeNodeId = startNodeId;
    state->previousState = nullptr;
    state->variables = std::make_shared<VariableMap>();
    state->dialogueHistory = std::make_shared<std::vector<DialogueHistoryEntry>>();
    return state;
}

std::shared_ptr<const GameState> GameState::createNextState(
    const std::shared_ptr<const GameState>& current,
    uint64_t nextNodeId,
    const std::string& varKey,
    const std::string& varValue) {

    auto nextState = std::make_shared<GameState>();
    nextState->stepId = current ? current->stepId + 1 : 1;
    nextState->activeNodeId = nextNodeId;
    nextState->previousState = current;

    if (current) {
        nextState->activeBackground = current->activeBackground;
        nextState->dspFilter = current->dspFilter;
        nextState->activeBgm = current->activeBgm;
        nextState->bgmVolume = current->bgmVolume;
        nextState->bgmPlaying = current->bgmPlaying;
        // #86: mixer gains ride every step transition (in-memory chain and
        // save/load/rewind restore them via the same copy).
        nextState->masterVolume = current->masterVolume;
        nextState->sfxVolume = current->sfxVolume;
        nextState->voiceVolume = current->voiceVolume;
        nextState->dialogueHistory = current->dialogueHistory;
    }

    // Structural sharing: only create new VariableMap if a variable actually changes
    // B7 (#25/#30): reserved bridge/stdlib names never enter saved state —
    // the write is dropped (the step/node transition still applies).
    if (!varKey.empty() && isReservedStateKey(varKey)) {
        ROWL_LOG_WARN("GameState dropped reserved variable key: '" + varKey + "'");
    }
    if (!varKey.empty() && !isReservedStateKey(varKey)) {
        // Create new variable map only if the value is different from current
        bool valueChanged = true;
        if (current && current->variables) {
            auto it = current->variables->data.find(varKey);
            if (it != current->variables->data.end() && it->second == varValue) {
                valueChanged = false;
            }
        }

        if (valueChanged) {
            auto newVarMap = std::make_shared<VariableMap>();
            if (current && current->variables) {
                newVarMap->data = current->variables->data; // Copy only when needed
            }
            newVarMap->data[varKey] = varValue;
            nextState->variables = newVarMap;
        } else {
            // Value unchanged - share the same variable map (true structural sharing!)
            nextState->variables = current ? current->variables : std::make_shared<VariableMap>();
        }
    } else {
        // No variable change - share the same pointer (zero-copy structural sharing!)
        nextState->variables = current ? current->variables : std::make_shared<VariableMap>();
    }

    return nextState;
}

std::shared_ptr<const GameState> GameState::createNextStateWithVariables(
    const std::shared_ptr<const GameState>& current,
    uint64_t nextNodeId,
    const std::unordered_map<std::string, std::string>& nextVariables) {

    if (current && current->activeNodeId == nextNodeId && current->variables &&
        current->variables->data == nextVariables) {
        return current;
    }

    auto nextState = std::make_shared<GameState>();
    nextState->stepId = current ? current->stepId + 1 : 1;
    nextState->activeNodeId = nextNodeId;
    nextState->previousState = current;
    if (current) {
        nextState->activeBackground = current->activeBackground;
        nextState->dspFilter = current->dspFilter;
        nextState->activeBgm = current->activeBgm;
        nextState->bgmVolume = current->bgmVolume;
        nextState->bgmPlaying = current->bgmPlaying;
        // #86: bulk write path carries the mixer too (see createNextState).
        nextState->masterVolume = current->masterVolume;
        nextState->sfxVolume = current->sfxVolume;
        nextState->voiceVolume = current->voiceVolume;
        nextState->dialogueHistory = current->dialogueHistory;
    }
    auto variableMap = std::make_shared<VariableMap>();
    variableMap->data = nextVariables;
    // B7 (#25/#30): bulk write path — strip reserved keys (see choke-point
    // note on isReservedStateKey).
    for (auto it = variableMap->data.begin(); it != variableMap->data.end();) {
        if (isReservedStateKey(it->first)) {
            ROWL_LOG_WARN("GameState dropped reserved variable key: '" + it->first + "'");
            it = variableMap->data.erase(it);
        } else {
            ++it;
        }
    }
    nextState->variables = std::move(variableMap);
    return nextState;
}

std::shared_ptr<const GameState> GameState::createNextStateWithAudio(
    const std::shared_ptr<const GameState>& current,
    uint64_t activeNodeId,
    const std::string& background,
    const std::string& bgm,
    float volume,
    bool playing,
    const std::string& filter,
    float masterVolume,
    float sfxVolume,
    float voiceVolume) {
    auto nextState = std::make_shared<GameState>();
    nextState->stepId = current ? current->stepId + 1 : 1;
    nextState->activeNodeId = activeNodeId;
    nextState->previousState = current;
    nextState->activeBackground = background;
    nextState->activeBgm = bgm;
    nextState->bgmVolume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f)
                                                 : (current ? current->bgmVolume : 1.0f);
    nextState->bgmPlaying = playing;
    nextState->dspFilter = filter;
    // #86: scene audio commit carries the live mixer (same finite-or-keep
    // contract as bgmVolume above so a hostile component value cannot poison
    // the chain).
    const float fallbackMaster = current ? current->masterVolume : 1.0f;
    const float fallbackSfx = current ? current->sfxVolume : 1.0f;
    const float fallbackVoice = current ? current->voiceVolume : 1.0f;
    nextState->masterVolume = std::isfinite(masterVolume) ? std::clamp(masterVolume, 0.0f, 1.0f) : fallbackMaster;
    nextState->sfxVolume = std::isfinite(sfxVolume) ? std::clamp(sfxVolume, 0.0f, 1.0f) : fallbackSfx;
    nextState->voiceVolume = std::isfinite(voiceVolume) ? std::clamp(voiceVolume, 0.0f, 1.0f) : fallbackVoice;
    nextState->variables = current ? current->variables : std::make_shared<VariableMap>();
    nextState->dialogueHistory = current ? current->dialogueHistory :
        std::make_shared<std::vector<DialogueHistoryEntry>>();
    return nextState;
}

std::shared_ptr<const GameState> GameState::withDialogueHistory(
    const std::shared_ptr<const GameState>& current,
    const std::vector<DialogueHistoryEntry>& entries) {
    if (!current || entries.empty()) return current;
    auto nextState = std::make_shared<GameState>(*current);
    auto history = std::make_shared<std::vector<DialogueHistoryEntry>>(
        current->dialogueHistory ? *current->dialogueHistory : std::vector<DialogueHistoryEntry>{});
    for (const auto& entry : entries) {
        if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
            entry.dialogue.size() > kMaxDialogueHistoryTextBytes ||
            entry.contentId.size() > kMaxContentIdBytes) {
            continue;
        }
        history->push_back(entry);
    }
    if (history->size() > kMaxDialogueHistoryEntries) {
        history->erase(history->begin(), history->begin() +
            static_cast<std::ptrdiff_t>(history->size() - kMaxDialogueHistoryEntries));
    }
    nextState->dialogueHistory = std::move(history);
    return nextState;
}

std::shared_ptr<const GameState> GameState::withSaveMetadata(
    const std::shared_ptr<const GameState>& current,
    const SaveMetadata& metadata) {
    if (!current) return nullptr;
    auto nextState = std::make_shared<GameState>(*current);
    nextState->playtimeSeconds =
        (std::isfinite(metadata.playtimeSeconds) && metadata.playtimeSeconds >= 0.0)
        ? metadata.playtimeSeconds
        : 0.0;
    nextState->chapterId = metadata.chapterId;
    nextState->chapterTitle = metadata.chapterTitle;
    nextState->summary = metadata.summary;
    nextState->thumbnailPng = metadata.thumbnailPng;
    nextState->thumbnailWidth = metadata.thumbnailWidth;
    nextState->thumbnailHeight = metadata.thumbnailHeight;
    return nextState;
}

std::shared_ptr<const GameState> GameState::withGraphIdentity(
    const std::shared_ptr<const GameState>& current,
    const std::string& graphIdentity) {
    if (!current) return nullptr;
    if (current->graphIdentity == graphIdentity) return current;
    auto nextState = std::make_shared<GameState>(*current);
    nextState->graphIdentity = graphIdentity;
    return nextState;
}

std::shared_ptr<const GameState> GameState::withMixerVolumes(
    const std::shared_ptr<const GameState>& current,
    float masterVolume,
    float bgmVolume,
    float sfxVolume,
    float voiceVolume) {
    if (!current) return nullptr;
    auto nextState = std::make_shared<GameState>(*current);
    // stepId/previousState untouched: no new rewind link (see header note).
    if (std::isfinite(masterVolume)) nextState->masterVolume = std::clamp(masterVolume, 0.0f, 1.0f);
    if (std::isfinite(bgmVolume)) nextState->bgmVolume = std::clamp(bgmVolume, 0.0f, 1.0f);
    if (std::isfinite(sfxVolume)) nextState->sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
    if (std::isfinite(voiceVolume)) nextState->voiceVolume = std::clamp(voiceVolume, 0.0f, 1.0f);
    return nextState;
}

std::shared_ptr<const GameState> GameState::rewind(
    const std::shared_ptr<const GameState>& current,
    uint64_t stepsToRewind) {

    if (!current) return nullptr;
    if (stepsToRewind == 0) return current;

    auto target = current;
    for (uint64_t i = 0; i < stepsToRewind && target->previousState; ++i) {
        target = target->previousState;
    }
    ROWL_LOG_INFO("Rewound GameState from Step #" + std::to_string(current->stepId) + " back to Step #" + std::to_string(target->stepId) + " (Active Node #" + std::to_string(target->activeNodeId) + ")");
    return target;
}

std::string GameState::serializeJson() const {
    nlohmann::json j = encodeStateObject(*this, true);
    // D08 (a): sınırlı öncül zincir — en fazla
    // kMaxSerializedHistoryEntries halka, her halka thumbnail'siz tam state.
    // Zincirsiz state'lerde "history" yazılmaz (legacy dosyalarla aynı tel).
    nlohmann::json chain = nlohmann::json::array();
    size_t count = 0;
    for (auto p = previousState;
         p && count < kMaxSerializedHistoryEntries;
         p = p->previousState, ++count) {
        chain.push_back(encodeStateObject(*p, false));
    }
    if (!chain.empty()) j["history"] = std::move(chain);

    return j.dump(2);
}

GameStateDecodeResult GameState::decodeJson(const std::string& jsonStr) {
    if (jsonStr.empty() || jsonStr.size() > kMaxSaveFileBytes) return {};
    // A2b: nesting pre-scan before parse — see kMaxSaveNestingDepth. A
    // hostile "[[[..." payload would otherwise recurse the parser off the
    // thread stack (uncatchable) instead of answering InvalidData.
    if (!saveNestingWithinBudget(jsonStr)) {
        ROWL_LOG_ERROR("GameState JSON exceeds the nesting depth budget");
        return {nullptr, GameStateDecodeStatus::InvalidData, CurrentSaveFormatVersion};
    }
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.is_object()) {
            ROWL_LOG_ERROR("GameState JSON root must be an object");
            return {};
        }
        if (j.contains("version") && !j["version"].is_number_unsigned()) {
            ROWL_LOG_ERROR("GameState JSON version must be an unsigned integer");
            return {};
        }
        const auto version = j.value("version", CurrentSaveFormatVersion);
        // #86 (v4): 1/2/3 decode as Migrated (mixer keys default to 1.0);
        // anything else is foreign.
        if (version != 1 && version != 2 && version != 3 && version != CurrentSaveFormatVersion) {
            ROWL_LOG_ERROR("Unsupported GameState save version: " + std::to_string(version));
            return {nullptr, GameStateDecodeStatus::UnsupportedVersion, version};
        }

        auto state = decodeStateObject(j, version);
        if (!state) {
            ROWL_LOG_ERROR("GameState JSON root object is invalid");
            return {nullptr, GameStateDecodeStatus::InvalidData, version};
        }
        // D08 (a): opsiyonel sınırlı "history" → öncül zincir. Eksik/yabancı/
        // bozuk history bugünkü davranışa düşer (previousState=nullptr); kök
        // decode başarısını etkilemez, InvalidData'ya düşürmez.
        std::shared_ptr<GameState> chain;
        if (j.contains("history")) {
            chain = decodeHistoryChain(j["history"], *state, version);
        }
        state->previousState = std::move(chain);
        const auto status = version == CurrentSaveFormatVersion
            ? GameStateDecodeStatus::Loaded
            : GameStateDecodeStatus::Migrated;
        return {std::move(state), status, version};
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Failed to deserialize GameState JSON: " + std::string(e.what()));
        return {};
    }
}

std::shared_ptr<const GameState> GameState::deserializeJson(const std::string& jsonStr) {
    return decodeJson(jsonStr).state;
}

bool GameState::saveToSlot(const std::shared_ptr<const GameState>& state,
                           int32_t slotIndex, const std::string& saveDir) {
    // Tur-11: saveDir is UTF-8 (C ABI contract). The implicit
    // path-from-string ctor would reinterpret it in the ANSI codepage on
    // Windows (CI-10: CJK temp root mojibake -> "slot does not exist").
    return SessionPersistence(Rowl::Platform::pathFromUtf8(saveDir))
        .saveSlot(state, slotIndex);
}

std::shared_ptr<const GameState> GameState::loadFromSlot(
    int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(Rowl::Platform::pathFromUtf8(saveDir))
        .loadSlot(slotIndex);
}

bool GameState::hasSlot(int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(Rowl::Platform::pathFromUtf8(saveDir))
        .hasSlot(slotIndex);
}

bool GameState::deleteSlot(int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(Rowl::Platform::pathFromUtf8(saveDir))
        .deleteSlot(slotIndex);
}

} // namespace Rowl::State
