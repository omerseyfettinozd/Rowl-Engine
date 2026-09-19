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
/// The bounded vector is structurally shared until a new line is appended.
struct DialogueHistoryEntry {
    uint64_t nodeId = 0;
    std::string speaker;
    std::string dialogue;
    bool read = true;
    /// Persistent Faz 2 content identity (UUID form); empty when the
    /// presented line predates content_id migration.
    std::string contentId;
};

// GCC 16 false positive -Warray-bounds with shared_ptr template internals
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

struct GameState {
    static constexpr uint32_t CurrentSaveFormatVersion = 3;

    // POD members first
    uint64_t stepId = 0;
    uint64_t activeNodeId = 101;
    uint32_t typewriterIndex = 0;

    // String members
    std::string activeBackground = "bg_beach_sunset.png";
    std::string dspFilter = "Normal";
    std::string activeBgm;
    float bgmVolume = 1.0f;
    bool bgmPlaying = false;

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
    std::shared_ptr<const std::vector<DialogueHistoryEntry>> dialogueHistory =
        std::make_shared<std::vector<DialogueHistoryEntry>>();
    std::shared_ptr<const GameState> previousState = nullptr;

    std::string getVariable(const std::string& key, const std::string& defaultValue = "") const;

    static std::shared_ptr<const GameState> createInitialState(uint64_t startNodeId = 101);
    static std::shared_ptr<const GameState> createNextState(
        const std::shared_ptr<const GameState>& current,
        uint64_t nextNodeId,
        const std::string& varKey = "",
        const std::string& varValue = ""
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
        const std::string& filter
    );

    static std::shared_ptr<const GameState> rewind(
        const std::shared_ptr<const GameState>& current,
        uint64_t stepsToRewind = 1
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

    // Serialization & slot persistence
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
