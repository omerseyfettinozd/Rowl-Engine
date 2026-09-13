#include "rowl/state/session_persistence.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/state/game_state.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace Rowl::State {

namespace {

constexpr int32_t kMinSaveSlot = 0;
constexpr int32_t kMaxSaveSlot = 99;
constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;

bool isValidSlotIndex(int32_t slotIndex) {
    return slotIndex >= kMinSaveSlot && slotIndex <= kMaxSaveSlot;
}

bool replaceFileAtomically(const std::filesystem::path& temporaryPath,
                           const std::filesystem::path& finalPath,
                           std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(temporaryPath.c_str(), finalPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporaryPath, finalPath, error);
    return !error;
#endif
}

} // namespace

SessionPersistence::SessionPersistence(std::string saveDirectory)
    : m_saveDirectory(saveDirectory.empty() ? "saves" : std::move(saveDirectory)) {}

void SessionPersistence::setSaveDirectory(std::string saveDirectory) {
    m_saveDirectory = saveDirectory.empty() ? "saves" : std::move(saveDirectory);
}

bool SessionPersistence::saveSlot(
    const std::shared_ptr<const GameState>& state, int32_t slotIndex) const {
    if (!state || !isValidSlotIndex(slotIndex)) {
        ROWL_LOG_ERROR("Cannot save GameState to invalid or null slot #" +
                       std::to_string(slotIndex));
        return false;
    }
    try {
        namespace fs = std::filesystem;
        const fs::path finalPath = fs::path(m_saveDirectory) /
            ("save_slot_" + std::to_string(slotIndex) + ".json");
        fs::create_directories(finalPath.parent_path());
        fs::path temporaryPath = finalPath;
        temporaryPath += ".tmp";

        const std::string json = state->serializeJson();
        {
            std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
            if (!output.is_open()) {
                ROWL_LOG_ERROR("Failed to open save slot temp file for writing: " +
                               temporaryPath.string());
                return false;
            }
            output << json;
            output.flush();
            if (!output.good()) {
                ROWL_LOG_ERROR("Failed to write complete save slot temp file: " +
                               temporaryPath.string());
                output.close();
                fs::remove(temporaryPath);
                return false;
            }
        }

        std::error_code replaceError;
        if (!replaceFileAtomically(temporaryPath, finalPath, replaceError)) {
            fs::remove(temporaryPath);
            ROWL_LOG_ERROR("Failed to atomically replace save slot file: " +
                           replaceError.message());
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

std::shared_ptr<const GameState> SessionPersistence::loadSlot(int32_t slotIndex) const {
    if (!isValidSlotIndex(slotIndex)) return nullptr;
    try {
        namespace fs = std::filesystem;
        const fs::path filePath = fs::path(m_saveDirectory) /
            ("save_slot_" + std::to_string(slotIndex) + ".json");

        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) +
                          " does not exist at: " + filePath.string());
            return nullptr;
        }
        std::error_code sizeError;
        const auto fileSize = fs::file_size(filePath, sizeError);
        if (sizeError || fileSize > kMaxSaveFileBytes) {
            ROWL_LOG_ERROR("Save slot file is too large or unreadable: " + filePath.string());
            return nullptr;
        }

        std::ifstream input(filePath);
        if (!input.is_open()) {
            ROWL_LOG_ERROR("Failed to open save slot file for reading: " + filePath.string());
            return nullptr;
        }

        const std::string content((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        auto loadedState = GameState::deserializeJson(content);
        if (loadedState) {
            ROWL_LOG_INFO("Successfully loaded GameState from Slot #" +
                          std::to_string(slotIndex) + " (Step #" +
                          std::to_string(loadedState->stepId) + ", Node #" +
                          std::to_string(loadedState->activeNodeId) + ")");
        }
        return loadedState;
    } catch (const std::exception& error) {
        ROWL_LOG_ERROR("Exception while loading GameState from slot #" +
                       std::to_string(slotIndex) + ": " + error.what());
        return nullptr;
    }
}

bool SessionPersistence::hasSlot(int32_t slotIndex) const {
    if (!isValidSlotIndex(slotIndex)) return false;
    const std::filesystem::path filePath = std::filesystem::path(m_saveDirectory) /
        ("save_slot_" + std::to_string(slotIndex) + ".json");
    return std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath);
}

bool SessionPersistence::deleteSlot(int32_t slotIndex) const {
    if (!isValidSlotIndex(slotIndex)) return false;
    try {
        const std::filesystem::path filePath = std::filesystem::path(m_saveDirectory) /
            ("save_slot_" + std::to_string(slotIndex) + ".json");
        return std::filesystem::exists(filePath) && std::filesystem::remove(filePath);
    } catch (const std::exception& error) {
        ROWL_LOG_ERROR("Exception while deleting save slot #" +
                       std::to_string(slotIndex) + ": " + error.what());
        return false;
    }
}

} // namespace Rowl::State
