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

/// A player-visible line kept independently from the rewind-chain internals.
/// The bounded vector is structurally shared until a new line is appended.
struct DialogueHistoryEntry {
    uint64_t nodeId = 0;
    std::string speaker;
    std::string dialogue;
    bool read = true;
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
