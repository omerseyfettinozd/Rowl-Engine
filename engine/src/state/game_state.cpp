#include "rowl/state/game_state.hpp"
#include "rowl/scripting/reserved_names.hpp"
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
#include <atomic>
#include <stdexcept>
#include <unordered_set>

namespace Rowl::State {

// ── F1 (A-3): persistent dialogue backlog ───────────────────────────────────
struct DialogueHistory::Node {
    DialogueHistoryEntry entry;
    std::shared_ptr<const Node> prev;
    size_t total = 0;  // nodes from the first one through this one
    uint64_t serial = 0;  // process-unique, never reused (memoization key)

    Node() = default;
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;
    // Iterative release: a long session builds a chain thousands of nodes
    // deep; the default recursive shared_ptr teardown would recurse per node.
    ~Node() {
        auto p = std::move(prev);
        while (p && p.use_count() == 1) {
            auto next = std::move(const_cast<Node*>(p.get())->prev);
            p = std::move(next);
        }
    }
};

DialogueHistory::DialogueHistory(const std::vector<DialogueHistoryEntry>& entries) {
    const size_t first = entries.size() > kMaxEntries ? entries.size() - kMaxEntries : 0;
    for (size_t i = first; i < entries.size(); ++i) {
        *this = appended(entries[i]);
    }
}

size_t DialogueHistory::size() const noexcept {
    return tail_ ? std::min(tail_->total, kMaxEntries) : 0;
}

uint64_t DialogueHistory::identity() const noexcept {
    return tail_ ? tail_->serial : 0;
}

size_t DialogueHistory::totalNodes() const noexcept {
    return tail_ ? tail_->total : 0;
}

const DialogueHistoryEntry& DialogueHistory::at(size_t index) const {
    const size_t count = size();
    if (index >= count) throw std::out_of_range("DialogueHistory::at");
    const Node* node = tail_.get();
    for (size_t back = count - 1 - index; back > 0; --back) node = node->prev.get();
    return node->entry;
}

const DialogueHistoryEntry& DialogueHistory::back() const {
    if (!tail_) throw std::out_of_range("DialogueHistory::back on an empty history");
    return tail_->entry;
}

DialogueHistory DialogueHistory::appended(DialogueHistoryEntry entry) const {
    auto node = std::make_shared<Node>();
    node->entry = std::move(entry);
    node->prev = tail_;
    node->total = (tail_ ? tail_->total : 0) + 1;
    static std::atomic<uint64_t> nextSerial{1};
    node->serial = nextSerial.fetch_add(1, std::memory_order_relaxed);
    DialogueHistory next;
    next.tail_ = std::move(node);
    return next;
}

std::vector<DialogueHistoryEntry> DialogueHistory::toVector() const {
    const size_t count = size();
    std::vector<DialogueHistoryEntry> out(count);
    const Node* node = tail_.get();
    for (size_t i = count; i > 0; --i) {
        out[i - 1] = node->entry;
        node = node->prev.get();
    }
    return out;
}

void DialogueHistory::collectNodeAddresses(std::vector<const void*>& out) const {
    const Node* node = tail_.get();
    for (size_t i = size(); i > 0; --i) {
        out.push_back(node);
        node = node->prev.get();
    }
}

namespace {

constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSaveVariables = 10'000;
constexpr size_t kMaxVariableKeyBytes = 256;
constexpr size_t kMaxVariableValueBytes = 64 * 1024;
constexpr size_t kMaxDialogueHistoryEntries = DialogueHistory::kMaxEntries;
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
// saved state. The sandbox rejects them on the Lua side (see
// rowl/scripting/reserved_names.hpp); these two createNextState* functions are
// the write-path choke-point, so the filter lives here and not in every
// caller. Grandfathered slot files that already contain such keys still load
// (decodeJson is untouched) — only new writes are refused.


// D08 (a): tek-state obje kodlayıcı. Aktif state withThumbnail=true ile
// yazılır (mevcut tel format birebir korunur); "history" halkaları
// withThumbnail=false ile yazılır (thumbnail yalnızca aktif state'te kalır,
// 6. bölüm 768KB kilit payı korunur).
// Perf: "history" halkalari kokun dialogue_history'si ile neredeyse ayni
// listeyi tasir (gecmis yalniz buyur; 500 kapagindan sonra bastan kirpilir).
// Halka L = [head (kokun kirptigi eski kayitlar)] + R[0..shared) ise liste
// yeniden yazilmaz: "dialogue_history_root_window" {head:[...], shared:N}
// yazilir (head cogunlukla bos/kucuk). Aksi halde her halka tum listeyi
// tekrarlar ve save boyutu/suresi ~5x buyur (soak-stress). Eslesme yoksa
// (dallanmis gecmis) tam liste yazilir.
constexpr size_t kMaxHistoryWindowHead = 64;
// Root-level "history_format": 2 marks that links may use
// "dialogue_history_root_window". Absent/1 = legacy (D08) full lists. An
// unknown (future) value makes the whole optional chain ignored (fail-open,
// same as any foreign "history"); the top-level save "version" is untouched.
constexpr uint32_t kHistoryFormatShared = 2;

// D03-1: bound for the optional per-line voice path (decode degrades to empty).
constexpr size_t kMaxDialogueHistoryVoiceBytes = 1024;

bool historyEntryEq(const DialogueHistoryEntry& x, const DialogueHistoryEntry& y) {
    return x.nodeId == y.nodeId && x.read == y.read && x.speaker == y.speaker &&
           x.dialogue == y.dialogue && x.contentId == y.contentId && x.voice == y.voice &&
           x.choice == y.choice;
}

nlohmann::json encodeHistoryEntry(const DialogueHistoryEntry& entry) {
    nlohmann::json encoded = {
        {"node_id", entry.nodeId}, {"speaker", entry.speaker},
        {"dialogue", entry.dialogue}, {"read", entry.read},
        {"content_id", entry.contentId},
    };
    // D03-1: optional key - voiceless lines keep the pre-D03 byte shape.
    if (!entry.voice.empty()) encoded["voice"] = entry.voice;
    // F1: optional key - ordinary lines keep the pre-F1 byte shape.
    if (entry.choice) encoded["choice"] = true;
    return encoded;
}

// Smallest head length h such that link[h..) == root[0..link.size()-h).
bool findHistoryWindow(const std::vector<DialogueHistoryEntry>& link,
                       const std::vector<DialogueHistoryEntry>& root, size_t& head) {
    const size_t m = link.size();
    for (size_t h = 0; h <= std::min(m, kMaxHistoryWindowHead); ++h) {
        const size_t shared = m - h;
        if (shared > root.size()) continue;
        bool same = true;
        for (size_t i = 0; i < shared && same; ++i) same = historyEntryEq(link[h + i], root[i]);
        if (same) {
            head = h;
            return true;
        }
    }
    return false;
}

nlohmann::json encodeStateObject(const GameState& s, bool withThumbnail,
                                 const GameState* historyRoot = nullptr) {
    nlohmann::json j;
    j["version"] = GameState::CurrentSaveFormatVersion;
    j["step_id"] = s.stepId;
    j["active_node_id"] = s.activeNodeId;
    j["typewriter_index"] = s.typewriterIndex;
    // F1: optional - links that are not node entries keep the pre-F1 shape.
    if (s.nodeEntry) j["node_entry"] = true;
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
    size_t windowHead = 0;
    const std::vector<DialogueHistoryEntry> linkHistory = s.dialogueHistory.toVector();
    const bool windowed =
        historyRoot && findHistoryWindow(linkHistory, historyRoot->dialogueHistory.toVector(),
                                         windowHead);
    if (windowed) {
        nlohmann::json head = nlohmann::json::array();
        for (size_t i = 0; i < windowHead; ++i) {
            head.push_back(encodeHistoryEntry(linkHistory[i]));
        }
        j["dialogue_history_root_window"] = {
            {"head", std::move(head)}, {"shared", linkHistory.size() - windowHead}};
    } else {
        nlohmann::json history = nlohmann::json::array();
        for (const auto& entry : linkHistory) history.push_back(encodeHistoryEntry(entry));
        j["dialogue_history"] = std::move(history);
    }

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
std::shared_ptr<GameState> decodeStateObject(const nlohmann::json& j, uint32_t version,
                                             const GameState* historyRoot = nullptr) {
    auto state = std::make_shared<GameState>();
    state->stepId = j.value("step_id", static_cast<uint64_t>(1));
    state->activeNodeId = j.value("active_node_id", static_cast<uint64_t>(101));
    state->typewriterIndex = j.value("typewriter_index", static_cast<uint32_t>(0));
    state->nodeEntry = j.contains("node_entry") && j["node_entry"].is_boolean() &&
                       j["node_entry"].get<bool>();
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
    auto decodeEntry = [](const nlohmann::json& rawEntry, DialogueHistoryEntry& entry) {
        if (!rawEntry.is_object()) {
            ROWL_LOG_ERROR("GameState JSON dialogue history entry must be an object");
            return false;
        }
        entry.nodeId = rawEntry.value("node_id", uint64_t{0});
        entry.speaker = rawEntry.value("speaker", "");
        entry.dialogue = rawEntry.value("dialogue", "");
        entry.read = rawEntry.value("read", true);
        entry.contentId = rawEntry.value("content_id", "");
        // D03-1: voice is a convenience path, never a reason to refuse a
        // save: a wrong-typed or oversized value degrades to "no voice".
        entry.voice.clear();
        if (rawEntry.contains("voice") && rawEntry["voice"].is_string()) {
            const auto& voice = rawEntry["voice"].get_ref<const std::string&>();
            if (voice.size() <= kMaxDialogueHistoryVoiceBytes) entry.voice = voice;
        }
        // F1: optional choice-row marker; a wrong-typed value degrades to false.
        entry.choice = rawEntry.contains("choice") && rawEntry["choice"].is_boolean() &&
                       rawEntry["choice"].get<bool>();
        if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
            entry.dialogue.size() > kMaxDialogueHistoryTextBytes ||
            entry.contentId.size() > kMaxContentIdBytes) {
            ROWL_LOG_ERROR("GameState JSON contains an invalid dialogue history entry");
            return false;
        }
        return true;
    };
    if (historyRoot && j.contains("dialogue_history_root_window")) {
        // Halka: head + kokun dialogue_history'sinin ilk `shared` kaydi.
        const auto& win = j["dialogue_history_root_window"];
        if (!win.is_object() || !win.contains("head") || !win["head"].is_array() ||
            win["head"].size() > kMaxHistoryWindowHead || !win.contains("shared") ||
            !win["shared"].is_number_unsigned() ||
            win["shared"].get<uint64_t>() > historyRoot->dialogueHistory.size() ||
            win["head"].size() + win["shared"].get<uint64_t>() > kMaxDialogueHistoryEntries) {
            return nullptr;
        }
        for (const auto& rawEntry : win["head"]) {
            DialogueHistoryEntry entry;
            if (!decodeEntry(rawEntry, entry)) return nullptr;
            history->push_back(std::move(entry));
        }
        const auto shared = static_cast<size_t>(win["shared"].get<uint64_t>());
        const auto rootEntries = historyRoot->dialogueHistory.toVector();
        history->insert(history->end(), rootEntries.begin(),
                        rootEntries.begin() + static_cast<std::ptrdiff_t>(shared));
    } else if (j.contains("dialogue_history")) {
        if (!j["dialogue_history"].is_array() ||
            j["dialogue_history"].size() > kMaxDialogueHistoryEntries) {
            ROWL_LOG_ERROR("GameState JSON dialogue history is invalid or too large");
            return nullptr;
        }
        for (const auto& rawEntry : j["dialogue_history"]) {
            DialogueHistoryEntry entry;
            if (!decodeEntry(rawEntry, entry)) return nullptr;
            history->push_back(std::move(entry));
        }
    }
    state->dialogueHistory = DialogueHistory(*history);
    state->previousState = nullptr;
    return state;
}

// D08 (a): opsiyonel "history" dizisini öncül zincire çevirir. Başarısızlık
// (eksik/yabancı/bozuk/sınır-aşan/monoton-olmayan) nullptr döner — çağrıcı
// bugünkü davranışa düşer (previousState=nullptr), kök decode başarısını
// etkilemez, InvalidData'ya düşürmez.
std::shared_ptr<GameState> decodeHistoryChain(const nlohmann::json& raw,
                                              const GameState& root,
                                              uint32_t version, bool sharedBacklog) {
    if (!raw.is_array() || raw.empty() ||
        raw.size() > GameState::kMaxSerializedHistoryEntries) {
        return nullptr;
    }
    std::vector<std::shared_ptr<GameState>> links;
    links.reserve(raw.size());
    for (const auto& rawEntry : raw) {
        if (!rawEntry.is_object()) return nullptr;
        auto entry = decodeStateObject(rawEntry, version, sharedBacklog ? &root : nullptr);
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
    state->dialogueHistory = DialogueHistory{};
    return state;
}

std::shared_ptr<const GameState> GameState::createNextState(
    const std::shared_ptr<const GameState>& current,
    uint64_t nextNodeId,
    const std::string& varKey,
    const std::string& varValue,
    bool nodeEntry) {

    auto nextState = std::make_shared<GameState>();
    nextState->stepId = current ? current->stepId + 1 : 1;
    nextState->activeNodeId = nextNodeId;
    nextState->nodeEntry = nodeEntry;
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
        // D09: graf-kimliği adım geçişlerinde taşınır (mixer ile aynı
        // sözleşme). Engine save yolu yine withGraphIdentity ile damgalar;
        // G kilidi dosyadaki kimliği commit'li belgeyle karşılaştırır, bu
        // bellek kopyası kapıyı etkilemez. Taşıma, Engine-dışı saveSlot
        // yolunun (public API) da kimlikli slot yazmasını sağlar — kayıt
        // legacy-warn yoluna düşmez, #70 provenance garantisi yapısal olur.
        nextState->graphIdentity = current->graphIdentity;
    }

    // Structural sharing: only create new VariableMap if a variable actually changes
    // B7 (#25/#30): reserved bridge/stdlib names never enter saved state —
    // the write is dropped (the step/node transition still applies).
    if (!varKey.empty() && Rowl::Scripting::isReservedVariableName(varKey)) {
        ROWL_LOG_WARN("GameState dropped reserved variable key: '" + varKey + "'");
    }
    if (!varKey.empty() && !Rowl::Scripting::isReservedVariableName(varKey)) {
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
    // note on Rowl::Scripting::isReservedVariableName).
    for (auto it = variableMap->data.begin(); it != variableMap->data.end();) {
        if (Rowl::Scripting::isReservedVariableName(it->first)) {
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
    nextState->dialogueHistory = current ? current->dialogueHistory : DialogueHistory{};
    return nextState;
}

std::shared_ptr<const GameState> GameState::withDialogueHistory(
    const std::shared_ptr<const GameState>& current,
    const std::vector<DialogueHistoryEntry>& entries) {
    if (!current || entries.empty()) return current;
    auto nextState = std::make_shared<GameState>(*current);
    DialogueHistory history = current->dialogueHistory;
    for (const auto& entry : entries) {
        if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
            entry.dialogue.size() > kMaxDialogueHistoryTextBytes ||
            entry.contentId.size() > kMaxContentIdBytes) {
            continue;
        }
        history = history.appended(entry);
    }
    // F1 (A-3): the backlog is a persistent list. The previous design copied
    // the whole (up to 500 entry) vector into every rewind link: ~95 KB per
    // advance, 778 MB after 8000 advances (audit A bulgu 3). Appending now
    // allocates one node and shares all older nodes with the previous link, so
    // per-link memory is constant. The previousState chain is untouched, so
    // rewind depth (down to the root) is unchanged. MUTASYON: copying the
    // list back into a fresh private vector per link turns
    // test_history_sharing red.
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

std::shared_ptr<const GameState> GameState::rewindNodes(
    const std::shared_ptr<const GameState>& current, uint64_t nodes) {
    if (!current || nodes == 0) return nullptr;
    auto target = current;
    uint64_t done = 0;
    for (; done < nodes; ++done) {
        // Walk to the first link of the visit target belongs to, then one
        // more: the last link of the previous visit.
        auto link = target;
        bool crossed = false;
        while (link->previousState) {
            const bool visitStart =
                link->nodeEntry || link->previousState->activeNodeId != link->activeNodeId;
            link = link->previousState;
            if (visitStart) {
                crossed = true;
                break;
            }
        }
        if (!crossed) break;
        target = link;
    }
    if (done == 0) return nullptr;
    ROWL_LOG_INFO("Rewound GameState by " + std::to_string(done) + " node(s) from Step #" +
                  std::to_string(current->stepId) + " back to Step #" +
                  std::to_string(target->stepId) + " (Active Node #" +
                  std::to_string(target->activeNodeId) + ")");
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
        chain.push_back(encodeStateObject(*p, false, this));
    }
    if (!chain.empty()) {
        j["history"] = std::move(chain);
        j["history_format"] = kHistoryFormatShared;
    }

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
            uint32_t historyFormat = 1;
            bool knownFormat = true;
            if (j.contains("history_format")) {
                knownFormat = j["history_format"].is_number_unsigned() &&
                              (j["history_format"].get<uint64_t>() == 1 ||
                               j["history_format"].get<uint64_t>() == kHistoryFormatShared);
                if (knownFormat) historyFormat = j["history_format"].get<uint32_t>();
            }
            if (knownFormat) {
                chain = decodeHistoryChain(j["history"], *state, version,
                                           historyFormat == kHistoryFormatShared);
            }
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
