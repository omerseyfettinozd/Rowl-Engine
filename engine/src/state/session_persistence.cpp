#include "rowl/state/session_persistence.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/state/save_durability.hpp"
#include "rowl/state/save_slots.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Rowl::State {

namespace {

constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;

using Rowl::State::isValidSlot;

} // namespace

SessionPersistence::SessionPersistence(std::filesystem::path saveDirectory)
    : m_saveDirectory(saveDirectory.empty() ? "saves" : std::move(saveDirectory)) {}

std::shared_ptr<const GameState> SessionPersistence::checkpoint(
    const std::shared_ptr<const GameState>& current, uint64_t currentNodeId) {
    if (!current) {
        return GameState::createInitialState(currentNodeId);
    }
    if (current->activeNodeId != currentNodeId) {
        return GameState::createNextState(current, currentNodeId);
    }
    return current;
}

std::shared_ptr<const GameState> SessionPersistence::rewind(
    const std::shared_ptr<const GameState>& current, uint64_t steps) {
    if (!current || steps == 0) return nullptr;
    auto target = GameState::rewind(current, steps);
    if (!target || target == current) return nullptr;
    return target;
}

void SessionPersistence::setSaveDirectory(std::filesystem::path saveDirectory) {
    m_saveDirectory = saveDirectory.empty() ? "saves" : std::move(saveDirectory);
}

bool SessionPersistence::saveSlot(
    const std::shared_ptr<const GameState>& state, int32_t slotIndex) const {
    if (!state || !isValidSlot(slotIndex)) {
        ROWL_LOG_ERROR("Cannot save GameState to invalid or null slot #" +
                       std::to_string(slotIndex));
        return false;
    }
    try {
        namespace fs = std::filesystem;
        const fs::path finalPath = m_saveDirectory /
            ("save_slot_" + std::to_string(slotIndex) + ".json");
        fs::create_directories(finalPath.parent_path());

        const std::string json = state->serializeJson();
        std::string writeError;
        if (!writeSlotFileAtomically(finalPath, json, &writeError)) {
            ROWL_LOG_ERROR("Failed to durably save slot #" +
                           std::to_string(slotIndex) + ": " + writeError);
            return false;
        }
        ROWL_LOG_INFO("Successfully saved GameState to Slot #" +
                      std::to_string(slotIndex) + " (" + finalPath.string() + ")");
        return true;
    } catch (const std::exception& error) {
        ROWL_LOG_ERROR("Exception while saving GameState to slot #" +
                       std::to_string(slotIndex) + ": " + error.what());
        return false;
    }
}

SessionLoadResult SessionPersistence::loadSlotDetailed(int32_t slotIndex) const {
    if (!isValidSlot(slotIndex)) return {};
    try {
        namespace fs = std::filesystem;
        const fs::path filePath = m_saveDirectory /
            ("save_slot_" + std::to_string(slotIndex) + ".json");

        // A crash mid-write leaves only a stray "<slot>.json.tmp"; the good
        // slot beside it stays complete. Ignore and best-effort clean it so
        // load always answers from the last good slot.
        cleanupStraySlotTemp(filePath);

        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) +
                          " does not exist at: " + filePath.string());
            return {nullptr, SessionLoadStatus::NotFound, 0};
        }
        std::error_code sizeError;
        const auto fileSize = fs::file_size(filePath, sizeError);
        if (sizeError || fileSize > kMaxSaveFileBytes) {
            ROWL_LOG_ERROR("Save slot file is too large or unreadable: " + filePath.string());
            return {nullptr, sizeError ? SessionLoadStatus::IoError
                                       : SessionLoadStatus::FileTooLarge, 0};
        }

        std::ifstream input(filePath);
        if (!input.is_open()) {
            ROWL_LOG_ERROR("Failed to open save slot file for reading: " + filePath.string());
            return {nullptr, SessionLoadStatus::IoError, 0};
        }

        const std::string content((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        auto decoded = GameState::decodeJson(content);
        if (decoded.state) {
            ROWL_LOG_INFO("Successfully loaded GameState from Slot #" +
                          std::to_string(slotIndex) + " (Step #" +
                          std::to_string(decoded.state->stepId) + ", Node #" +
                          std::to_string(decoded.state->activeNodeId) + ")");
        }
        const auto status = decoded.status == GameStateDecodeStatus::Loaded
            ? SessionLoadStatus::Loaded
            : decoded.status == GameStateDecodeStatus::Migrated
                ? SessionLoadStatus::Migrated
                : decoded.status == GameStateDecodeStatus::UnsupportedVersion
                    ? SessionLoadStatus::UnsupportedVersion
                    : SessionLoadStatus::InvalidData;
        return {std::move(decoded.state), status, decoded.sourceVersion};
    } catch (const std::exception& error) {
        ROWL_LOG_ERROR("Exception while loading GameState from slot #" +
                       std::to_string(slotIndex) + ": " + error.what());
        return {nullptr, SessionLoadStatus::IoError, 0};
    }
}

std::shared_ptr<const GameState> SessionPersistence::loadSlot(int32_t slotIndex) const {
    return loadSlotDetailed(slotIndex).state;
}

bool SessionPersistence::hasSlot(int32_t slotIndex) const {
    if (!isValidSlot(slotIndex)) return false;
    const std::filesystem::path filePath = m_saveDirectory /
        ("save_slot_" + std::to_string(slotIndex) + ".json");
    return std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath);
}

bool SessionPersistence::deleteSlot(int32_t slotIndex) const {
    if (!isValidSlot(slotIndex)) return false;
    try {
        const std::filesystem::path filePath = m_saveDirectory /
            ("save_slot_" + std::to_string(slotIndex) + ".json");
        // A crash mid-save can leave "<slot>.json.tmp" behind; deleting the
        // slot removes its stray temp as well so no orphan lingers.
        cleanupStraySlotTemp(filePath);
        return std::filesystem::exists(filePath) && std::filesystem::remove(filePath);
    } catch (const std::exception& error) {
        ROWL_LOG_ERROR("Exception while deleting save slot #" +
                       std::to_string(slotIndex) + ": " + error.what());
        return false;
    }
}

} // namespace Rowl::State
