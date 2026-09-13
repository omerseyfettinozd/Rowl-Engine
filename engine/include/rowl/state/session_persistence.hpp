#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Rowl::State {

struct GameState;

enum class SessionLoadStatus {
    Loaded,
    Migrated,
    NotFound,
    FileTooLarge,
    IoError,
    InvalidData,
    UnsupportedVersion,
};

struct SessionLoadResult {
    std::shared_ptr<const GameState> state;
    SessionLoadStatus status = SessionLoadStatus::InvalidData;
    uint32_t sourceVersion = 0;

    bool succeeded() const { return state != nullptr; }
    bool migrated() const { return status == SessionLoadStatus::Migrated; }
};

/// Filesystem boundary for versioned session save slots.
///
/// GameState remains the immutable data model; this class owns slot paths,
/// bounded reads, atomic replacement writes, plus the checkpoint/rewind
/// chain stepping over the immutable history.
class SessionPersistence {
public:
    explicit SessionPersistence(std::string saveDirectory = "saves");

    /// Aligns a history chain with the live story cursor before a save.
    /// Null input yields a fresh initial state; a node mismatch appends one
    /// checkpoint step; an aligned chain is returned unchanged.
    static std::shared_ptr<const GameState> checkpoint(
        const std::shared_ptr<const GameState>& current, uint64_t currentNodeId);

    /// Steps back over the immutable history chain. Returns nullptr when
    /// there is no movement (null input, zero steps, or already at root).
    static std::shared_ptr<const GameState> rewind(
        const std::shared_ptr<const GameState>& current, uint64_t steps);

    bool saveSlot(const std::shared_ptr<const GameState>& state, int32_t slotIndex) const;
    SessionLoadResult loadSlotDetailed(int32_t slotIndex) const;
    std::shared_ptr<const GameState> loadSlot(int32_t slotIndex) const;
    bool hasSlot(int32_t slotIndex) const;
    bool deleteSlot(int32_t slotIndex) const;

    const std::string& saveDirectory() const { return m_saveDirectory; }
    void setSaveDirectory(std::string saveDirectory);

private:
    std::string m_saveDirectory;
};

} // namespace Rowl::State
