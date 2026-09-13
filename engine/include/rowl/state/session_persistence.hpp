#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Rowl::State {

struct GameState;

/// Filesystem boundary for versioned session save slots.
///
/// GameState remains the immutable data model; this class owns slot paths,
/// bounded reads and atomic replacement writes.
class SessionPersistence {
public:
    explicit SessionPersistence(std::string saveDirectory = "saves");

    bool saveSlot(const std::shared_ptr<const GameState>& state, int32_t slotIndex) const;
    std::shared_ptr<const GameState> loadSlot(int32_t slotIndex) const;
    bool hasSlot(int32_t slotIndex) const;
    bool deleteSlot(int32_t slotIndex) const;

    const std::string& saveDirectory() const { return m_saveDirectory; }
    void setSaveDirectory(std::string saveDirectory);

private:
    std::string m_saveDirectory;
};

} // namespace Rowl::State
