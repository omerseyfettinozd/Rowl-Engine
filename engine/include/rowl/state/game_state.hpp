#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <vector>

namespace Rowl::State {

struct GameState;

enum class GameStateDecodeStatus {
    Loaded,
    Migrated,
    InvalidData,
    UnsupportedVersion,
};

struct GameStateDecodeResult {
    std::shared_ptr<const GameState> state;
    GameStateDecodeStatus status = GameStateDecodeStatus::InvalidData;
    uint32_t sourceVersion = 0;

    bool succeeded() const { return state != nullptr; }
    bool migrated() const { return status == GameStateDecodeStatus::Migrated; }
};

struct VariableMap {
    std::unordered_map<std::string, std::string> data;
};

/// Display-only save-slot metadata stamped at save time (Faz 2 Dilim 4).
/// Never affects simulation, rewind or migration: all fields are optional
/// on decode and default to empty/zero for legacy saves.
struct SaveMetadata {
    double playtimeSeconds = 0.0;
    std::string chapterId;
    std::string chapterTitle;
    /// Last presented dialogue line, truncated for slot listings.
    std::string summary;
    /// Downscaled PNG bytes (empty when no framebuffer was available).
    std::string thumbnailPng;
    uint32_t thumbnailWidth = 0;
    uint32_t thumbnailHeight = 0;
};

/// A player-visible line kept independently from the rewind-chain internals.
struct DialogueHistoryEntry {
    uint64_t nodeId = 0;
    std::string speaker;
    std::string dialogue;
    bool read = true;
    /// Persistent Faz 2 content identity (UUID form); empty when the
    /// presented line predates content_id migration.
    std::string contentId;
    /// D03-1: voice clip path (VFS/asset path of the node's voice_track) the
    /// line was presented with; empty when the node had no voice. Optional
    /// "voice" key in saves - older saves decode to empty.
    std::string voice;
    /// F1 (A ürün kararı 1): the row is the answer the player picked at a
    /// choice (rendered "▸ <option>", no speaker, no voice). Optional
    /// "choice" key in saves - older saves decode to false. For a choice row
    /// `dialogue` holds the option text and `contentId` the option's id.
    bool choice = false;
};

/// F1 (A-3): the bounded dialogue backlog as a PERSISTENT (immutable,
/// structurally shared) list. Appending allocates exactly one node and shares
/// every older node with the previous rewind link, so a state chain of N links
/// costs O(N) entries instead of O(N * kMaxEntries) vector copies (the old
/// per-link copy was ~95 KB per advance - 778 MB after 8000 advances).
/// Value semantics: copying is a shared_ptr copy. The view is the newest
/// min(total, kMaxEntries) entries; older nodes stay linked (they are also
/// reachable from older rewind links, which own them anyway).
class DialogueHistory {
public:
    /// Player-visible bound (the backlog shows at most this many rows).
    static constexpr size_t kMaxEntries = 500;

    DialogueHistory() = default;
    /// Builds a list from entries oldest->newest (keeps the newest kMaxEntries).
    explicit DialogueHistory(const std::vector<DialogueHistoryEntry>& entries);

    size_t size() const noexcept;
    bool empty() const noexcept { return size() == 0; }
    /// Entry `index` of the visible view (0 = oldest visible). O(size - index).
    /// Out of range throws std::out_of_range (like std::vector::at).
    const DialogueHistoryEntry& at(size_t index) const;
    const DialogueHistoryEntry& operator[](size_t index) const { return at(index); }
    /// Newest entry; the list must not be empty.
    const DialogueHistoryEntry& back() const;

    /// New list sharing every existing node plus `entry` (O(1) memory).
    DialogueHistory appended(DialogueHistoryEntry entry) const;

    /// Visible view copied oldest->newest.
    std::vector<DialogueHistoryEntry> toVector() const;

    /// Identity of the newest node: a process-unique serial (0 when empty), never
    /// reused after a node is freed (an address could be). Equal identity means
    /// an identical list; used to memoize views.
    uint64_t identity() const noexcept;
    /// Number of nodes reachable from this list (including trimmed ones).
    size_t totalNodes() const noexcept;
    /// Appends the address of every node of the visible view to `out`
    /// (measurement hook: distinct addresses across a rewind chain = real
    /// entry storage).
    void collectNodeAddresses(std::vector<const void*>& out) const;

private:
    struct Node;
    std::shared_ptr<const Node> tail_;
};

// GCC 16 false positive -Warray-bounds with shared_ptr template internals
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

struct GameState {
    // #86: mixer volumes are save state (v4). v3 and older saves carry no
    // mixer keys and decode to 1.0 defaults (Migrated).
    static constexpr uint32_t CurrentSaveFormatVersion = 4;

    // D08 (a): serializeJson'un "history" anahtarına yazdığı sınırlı öncül
    // zincirin üst sınırı (halka sayısı, aktif state hariç). Her halka
    // thumbnail'siz tam state'tir; thumbnail yalnızca aktif state'te kalır.
    // K=4: 6. bölüm 1200-adım ölçümünde ~656KB ile 768KB kilidinin altında
    // kalır (K=8 → ~974KB ile kilidi aşıyordu; eşik genişletilmedi, K
    // düşürüldü). Additive ABI: anahtar opsiyonel, eski okuyucular yoksayar,
    // format version bump YOK. Eksik/yabancı/bozuk "history" decode'da
    // yoksayılır (previousState=nullptr — bugünkü davranış), InvalidData'ya
    // düşürmez.
    static constexpr size_t kMaxSerializedHistoryEntries = 4;

    // POD members first
    uint64_t stepId = 0;
    uint64_t activeNodeId = 101;
    uint32_t typewriterIndex = 0;
    // F1 (A-1): true on the link a real node ENTRY created (the player
    // arrived at activeNodeId). A node's entry effects (variable, script,
    // audio commits) add further links with the same activeNodeId and
    // nodeEntry=false; rewindNodes() treats [entry link .. last commit] as one
    // node visit. Optional "node_entry" key in saves (absent = false).
    bool nodeEntry = false;

    // String members
    std::string activeBackground = "bg_beach_sunset.png";
    std::string dspFilter = "Normal";
    std::string activeBgm;
    float bgmVolume = 1.0f;
    bool bgmPlaying = false;
    // #86: full mixer (bgmVolume above + these three). Stamped on every
    // volume-setter commit and every scene audio commit, restored by
    // Engine::restoreAudioStateFromGameState on load/rewind. Ambience/Ui
    // gains stay session-local and are deliberately not persisted.
    float masterVolume = 1.0f;
    float sfxVolume = 1.0f;
    float voiceVolume = 1.0f;

    // Faz 2 Dilim 4 display-only save metadata (see SaveMetadata).
    // savedAt is the ISO-8601 stamp written by serializeJson ("saved_at").
    std::string savedAt;
    double playtimeSeconds = 0.0;
    std::string chapterId;
    std::string chapterTitle;
    std::string summary;
    std::string thumbnailPng;
    uint32_t thumbnailWidth = 0;
    uint32_t thumbnailHeight = 0;

    // Faz D4/G (#70): story/graph content identity ("graph_id" on the wire).
    // Stamped at save time from the committed graph; empty for legacy saves
    // (decode default) and graph-less flows. Never affects simulation or
    // rewind — load-time gate only.
    std::string graphIdentity;

    // Smart pointers last
    std::shared_ptr<const VariableMap> variables = std::make_shared<VariableMap>();
    // F1 (A-3): persistent list shared between rewind links (see DialogueHistory).
    DialogueHistory dialogueHistory;
    std::shared_ptr<const GameState> previousState = nullptr;

    std::string getVariable(const std::string& key, const std::string& defaultValue = "") const;

    static std::shared_ptr<const GameState> createInitialState(uint64_t startNodeId = 101);
    static std::shared_ptr<const GameState> createNextState(
        const std::shared_ptr<const GameState>& current,
        uint64_t nextNodeId,
        const std::string& varKey = "",
        const std::string& varValue = "",
        // F1: set by the engine's real node-entry transition only.
        bool nodeEntry = false
    );
    static std::shared_ptr<const GameState> createNextStateWithVariables(
        const std::shared_ptr<const GameState>& current,
        uint64_t nextNodeId,
        const std::unordered_map<std::string, std::string>& nextVariables
    );
    static std::shared_ptr<const GameState> createNextStateWithAudio(
        const std::shared_ptr<const GameState>& current,
        uint64_t activeNodeId,
        const std::string& background,
        const std::string& bgm,
        float volume,
        bool playing,
        const std::string& filter,
        // #86: live mixer at commit time (default 1.0 keeps older callers
        // compiling; Engine passes the live gains).
        float masterVolume = 1.0f,
        float sfxVolume = 1.0f,
        float voiceVolume = 1.0f
    );

    static std::shared_ptr<const GameState> rewind(
        const std::shared_ptr<const GameState>& current,
        uint64_t stepsToRewind = 1
    );
    /// F1 (A-1): steps back over whole NODE visits and lands on the LAST link
    /// of the previous visit - i.e. the state after every entry commit of the
    /// previous node (variables, script, audio, history line). One player
    /// "back" press = one node, however many links the node's entry wrote.
    /// A visit starts at a link with nodeEntry=true or where activeNodeId
    /// changes. Returns nullptr when there is no earlier node (the chain's
    /// first node cannot be rewound out of); if `nodes` exceeds the available
    /// visits the oldest reachable visit is returned.
    static std::shared_ptr<const GameState> rewindNodes(
        const std::shared_ptr<const GameState>& current,
        uint64_t nodes = 1
    );
    static std::shared_ptr<const GameState> withDialogueHistory(
        const std::shared_ptr<const GameState>& current,
        const std::vector<DialogueHistoryEntry>& entries
    );

    /// Returns a structurally shared copy carrying fresh save metadata.
    static std::shared_ptr<const GameState> withSaveMetadata(
        const std::shared_ptr<const GameState>& current,
        const SaveMetadata& metadata
    );

    /// Returns a structurally shared copy stamped with a graph identity
    /// (D4/G #70). Equal identity returns the input unchanged.
    static std::shared_ptr<const GameState> withGraphIdentity(
        const std::shared_ptr<const GameState>& current,
        const std::string& graphIdentity
    );

    /// #86: volume-setter commit. Returns a structurally shared copy carrying
    /// the live mixer gains WITHOUT advancing stepId or extending the rewind
    /// chain (same convention as withSaveMetadata/withGraphIdentity): slider
    /// ticks must not become rewind steps, but save/load/rewind restore them.
    /// Non-finite inputs keep the current value; finite inputs clamp to
    /// [0,1]. Null input returns nullptr.
    static std::shared_ptr<const GameState> withMixerVolumes(
        const std::shared_ptr<const GameState>& current,
        float masterVolume,
        float bgmVolume,
        float sfxVolume,
        float voiceVolume
    );

    // Serialization & slot persistence
    //
    // D08 (a): serializeJson aktif state'e ek olarak en fazla
    // kMaxSerializedHistoryEntries öncül halkayı "history" anahtarına yazar
    // (thumbnail'siz tam state'ler); decodeJson "history" varsa previousState
    // zincirini kurar, böylece save->load sonrası rewind çalışır.
    std::string serializeJson() const;
    static GameStateDecodeResult decodeJson(const std::string& jsonStr);
    static std::shared_ptr<const GameState> deserializeJson(const std::string& jsonStr);

    static bool saveToSlot(const std::shared_ptr<const GameState>& state, int32_t slotIndex, const std::string& saveDir = "saves");
    static std::shared_ptr<const GameState> loadFromSlot(int32_t slotIndex, const std::string& saveDir = "saves");
    static bool hasSlot(int32_t slotIndex, const std::string& saveDir = "saves");
    static bool deleteSlot(int32_t slotIndex, const std::string& saveDir = "saves");
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace Rowl::State
