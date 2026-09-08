#include "rowl/state/game_state.hpp"
#include "rowl/core/logger.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <system_error>
#include <algorithm>
#include <cmath>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace Rowl::State {

namespace {

constexpr uint32_t kSaveFormatVersion = 2;
constexpr int32_t kMinSaveSlot = 0;
constexpr int32_t kMaxSaveSlot = 99;
constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSaveVariables = 10'000;
constexpr size_t kMaxVariableKeyBytes = 256;
constexpr size_t kMaxVariableValueBytes = 64 * 1024;

bool isValidSlotIndex(int32_t slotIndex) {
    return slotIndex >= kMinSaveSlot && slotIndex <= kMaxSaveSlot;
}

std::filesystem::path savePathForSlot(int32_t slotIndex, const std::string& saveDir) {
    const std::filesystem::path directory(saveDir.empty() ? "saves" : saveDir);
    return directory / ("save_slot_" + std::to_string(slotIndex) + ".json");
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
    }

    // Structural sharing: only create new VariableMap if a variable actually changes
    if (!varKey.empty()) {
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
    }
    auto variableMap = std::make_shared<VariableMap>();
    variableMap->data = nextVariables;
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
    const std::string& filter) {
    auto nextState = std::make_shared<GameState>();
    nextState->stepId = current ? current->stepId + 1 : 1;
    nextState->activeNodeId = activeNodeId;
    nextState->previousState = current;
    nextState->activeBackground = background;
    nextState->activeBgm = bgm;
    nextState->bgmVolume = std::clamp(volume, 0.0f, 1.0f);
    nextState->bgmPlaying = playing;
    nextState->dspFilter = filter;
    nextState->variables = current ? current->variables : std::make_shared<VariableMap>();
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
    nlohmann::json j;
    j["version"] = kSaveFormatVersion;
    j["step_id"] = stepId;
    j["active_node_id"] = activeNodeId;
    j["typewriter_index"] = typewriterIndex;
    j["active_background"] = activeBackground;
    j["dsp_filter"] = dspFilter;
    j["active_bgm"] = activeBgm;
    j["bgm_volume"] = bgmVolume;
    j["bgm_playing"] = bgmPlaying;

    nlohmann::json varObj = nlohmann::json::object();
    if (variables) {
        for (const auto& [k, v] : variables->data) {
            varObj[k] = v;
        }
    }
    j["variables"] = varObj;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    return j.dump(2);
}

std::shared_ptr<const GameState> GameState::deserializeJson(const std::string& jsonStr) {
    if (jsonStr.empty() || jsonStr.size() > kMaxSaveFileBytes) return nullptr;
    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.is_object()) {
            ROWL_LOG_ERROR("GameState JSON root must be an object");
            return nullptr;
        }
        const auto version = j.value("version", kSaveFormatVersion);
        if (version != 1 && version != kSaveFormatVersion) {
            ROWL_LOG_ERROR("Unsupported GameState save version: " + std::to_string(version));
            return nullptr;
        }

        auto state = std::make_shared<GameState>();
        state->stepId = j.value("step_id", static_cast<uint64_t>(1));
        state->activeNodeId = j.value("active_node_id", static_cast<uint64_t>(101));
        state->typewriterIndex = j.value("typewriter_index", static_cast<uint32_t>(0));
        state->activeBackground = j.value("active_background", "bg_beach_sunset.png");
        state->dspFilter = j.value("dsp_filter", "Normal");
        state->activeBgm = j.value("active_bgm", "");
        state->bgmVolume = j.value("bgm_volume", 1.0f);
        state->bgmPlaying = j.value("bgm_playing", !state->activeBgm.empty());

        if (!std::isfinite(state->bgmVolume) || state->bgmVolume < 0.0f || state->bgmVolume > 1.0f) {
            ROWL_LOG_ERROR("GameState JSON contains an invalid BGM volume");
            return nullptr;
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
        state->previousState = nullptr;
        return state;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Failed to deserialize GameState JSON: " + std::string(e.what()));
        return nullptr;
    }
}

bool GameState::saveToSlot(const std::shared_ptr<const GameState>& state, int32_t slotIndex, const std::string& saveDir) {
    if (!state || !isValidSlotIndex(slotIndex)) {
        ROWL_LOG_ERROR("Cannot save GameState to invalid or null slot #" + std::to_string(slotIndex));
        return false;
    }
    try {
        namespace fs = std::filesystem;
        fs::path finalPath = savePathForSlot(slotIndex, saveDir);
        fs::path dir = finalPath.parent_path();
        fs::create_directories(dir);
        fs::path tmpPath = finalPath;
        tmpPath += ".tmp";

        std::string jsonStr = state->serializeJson();
        {
            std::ofstream ofs(tmpPath, std::ios::out | std::ios::trunc);
            if (!ofs.is_open()) {
                ROWL_LOG_ERROR("Failed to open save slot temp file for writing: " + tmpPath.string());
                return false;
            }
            ofs << jsonStr;
            ofs.flush();
            if (!ofs.good()) {
                ROWL_LOG_ERROR("Failed to write complete save slot temp file: " + tmpPath.string());
                ofs.close();
                fs::remove(tmpPath);
                return false;
            }
        }

        std::error_code renameError;
        if (!replaceFileAtomically(tmpPath, finalPath, renameError)) {
            fs::remove(tmpPath);
            ROWL_LOG_ERROR("Failed to atomically replace save slot file: " + renameError.message());
            return false;
        }
        ROWL_LOG_INFO("Successfully saved GameState to Slot #" + std::to_string(slotIndex) + " (" + finalPath.string() + ")");
        return true;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Exception while saving GameState to slot #" + std::to_string(slotIndex) + ": " + e.what());
        return false;
    }
}

std::shared_ptr<const GameState> GameState::loadFromSlot(int32_t slotIndex, const std::string& saveDir) {
    if (!isValidSlotIndex(slotIndex)) return nullptr;
    try {
        namespace fs = std::filesystem;
        fs::path filePath = savePathForSlot(slotIndex, saveDir);

        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) + " does not exist at: " + filePath.string());
            return nullptr;
        }
        std::error_code sizeError;
        const auto fileSize = fs::file_size(filePath, sizeError);
        if (sizeError || fileSize > kMaxSaveFileBytes) {
            ROWL_LOG_ERROR("Save slot file is too large or unreadable: " + filePath.string());
            return nullptr;
        }

        std::ifstream ifs(filePath);
        if (!ifs.is_open()) {
            ROWL_LOG_ERROR("Failed to open save slot file for reading: " + filePath.string());
            return nullptr;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto loadedState = deserializeJson(content);
        if (loadedState) {
            ROWL_LOG_INFO("Successfully loaded GameState from Slot #" + std::to_string(slotIndex) +
                          " (Step #" + std::to_string(loadedState->stepId) + ", Node #" + std::to_string(loadedState->activeNodeId) + ")");
        }
        return loadedState;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Exception while loading GameState from slot #" + std::to_string(slotIndex) + ": " + e.what());
        return nullptr;
    }
}

bool GameState::hasSlot(int32_t slotIndex, const std::string& saveDir) {
    if (!isValidSlotIndex(slotIndex)) return false;
    namespace fs = std::filesystem;
    fs::path filePath = savePathForSlot(slotIndex, saveDir);
    return fs::exists(filePath) && fs::is_regular_file(filePath);
}

bool GameState::deleteSlot(int32_t slotIndex, const std::string& saveDir) {
    if (!isValidSlotIndex(slotIndex)) return false;
    try {
        namespace fs = std::filesystem;
        fs::path filePath = savePathForSlot(slotIndex, saveDir);
        if (fs::exists(filePath)) {
            return fs::remove(filePath);
        }
        return false;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Exception while deleting save slot #" + std::to_string(slotIndex) + ": " + e.what());
        return false;
    }
}

} // namespace Rowl::State
