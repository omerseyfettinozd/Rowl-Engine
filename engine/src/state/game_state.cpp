#include "rowl/state/game_state.hpp"
#include "rowl/core/logger.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace Rowl::State {

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
    j["version"] = 1;
    j["step_id"] = stepId;
    j["active_node_id"] = activeNodeId;
    j["typewriter_index"] = typewriterIndex;
    j["active_background"] = activeBackground;
    j["dsp_filter"] = dspFilter;

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
    if (jsonStr.empty()) return nullptr;
    try {
        auto j = nlohmann::json::parse(jsonStr);
        auto state = std::make_shared<GameState>();
        state->stepId = j.value("step_id", static_cast<uint64_t>(1));
        state->activeNodeId = j.value("active_node_id", static_cast<uint64_t>(101));
        state->typewriterIndex = j.value("typewriter_index", static_cast<uint32_t>(0));
        state->activeBackground = j.value("active_background", "bg_beach_sunset.png");
        state->dspFilter = j.value("dsp_filter", "Normal");

        auto varMap = std::make_shared<VariableMap>();
        if (j.contains("variables") && j["variables"].is_object()) {
            for (auto& el : j["variables"].items()) {
                if (el.value().is_string()) {
                    varMap->data[el.key()] = el.value().get<std::string>();
                } else {
                    varMap->data[el.key()] = el.value().dump();
                }
            }
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
    if (!state) {
        ROWL_LOG_ERROR("Cannot save null GameState to slot #" + std::to_string(slotIndex));
        return false;
    }
    try {
        namespace fs = std::filesystem;
        fs::path dir(saveDir.empty() ? "saves" : saveDir);
        fs::create_directories(dir);

        fs::path finalPath = dir / ("save_slot_" + std::to_string(slotIndex) + ".json");
        fs::path tmpPath = dir / ("save_slot_" + std::to_string(slotIndex) + ".json.tmp");

        std::string jsonStr = state->serializeJson();
        {
            std::ofstream ofs(tmpPath, std::ios::out | std::ios::trunc);
            if (!ofs.is_open()) {
                ROWL_LOG_ERROR("Failed to open save slot temp file for writing: " + tmpPath.string());
                return false;
            }
            ofs << jsonStr;
            ofs.flush();
        }

        fs::rename(tmpPath, finalPath);
        ROWL_LOG_INFO("Successfully saved GameState to Slot #" + std::to_string(slotIndex) + " (" + finalPath.string() + ")");
        return true;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Exception while saving GameState to slot #" + std::to_string(slotIndex) + ": " + e.what());
        return false;
    }
}

std::shared_ptr<const GameState> GameState::loadFromSlot(int32_t slotIndex, const std::string& saveDir) {
    try {
        namespace fs = std::filesystem;
        fs::path dir(saveDir.empty() ? "saves" : saveDir);
        fs::path filePath = dir / ("save_slot_" + std::to_string(slotIndex) + ".json");

        if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
            ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) + " does not exist at: " + filePath.string());
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
    namespace fs = std::filesystem;
    fs::path dir(saveDir.empty() ? "saves" : saveDir);
    fs::path filePath = dir / ("save_slot_" + std::to_string(slotIndex) + ".json");
    return fs::exists(filePath) && fs::is_regular_file(filePath);
}

bool GameState::deleteSlot(int32_t slotIndex, const std::string& saveDir) {
    try {
        namespace fs = std::filesystem;
        fs::path dir(saveDir.empty() ? "saves" : saveDir);
        fs::path filePath = dir / ("save_slot_" + std::to_string(slotIndex) + ".json");
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