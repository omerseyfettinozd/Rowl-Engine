#include "rowl/state/game_state.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/state/session_persistence.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace Rowl::State {

namespace {

constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSaveVariables = 10'000;
constexpr size_t kMaxVariableKeyBytes = 256;
constexpr size_t kMaxVariableValueBytes = 64 * 1024;
constexpr size_t kMaxDialogueHistoryEntries = 500;
constexpr size_t kMaxDialogueHistoryTextBytes = 64 * 1024;

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
    state->dialogueHistory = std::make_shared<std::vector<DialogueHistoryEntry>>();
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
        nextState->dialogueHistory = current->dialogueHistory;
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
        nextState->dialogueHistory = current->dialogueHistory;
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
    nextState->dialogueHistory = current ? current->dialogueHistory :
        std::make_shared<std::vector<DialogueHistoryEntry>>();
    return nextState;
}

std::shared_ptr<const GameState> GameState::withDialogueHistory(
    const std::shared_ptr<const GameState>& current,
    const std::vector<DialogueHistoryEntry>& entries) {
    if (!current || entries.empty()) return current;
    auto nextState = std::make_shared<GameState>(*current);
    auto history = std::make_shared<std::vector<DialogueHistoryEntry>>(
        current->dialogueHistory ? *current->dialogueHistory : std::vector<DialogueHistoryEntry>{});
    for (const auto& entry : entries) {
        if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
            entry.dialogue.size() > kMaxDialogueHistoryTextBytes) {
            continue;
        }
        history->push_back(entry);
    }
    if (history->size() > kMaxDialogueHistoryEntries) {
        history->erase(history->begin(), history->begin() +
            static_cast<std::ptrdiff_t>(history->size() - kMaxDialogueHistoryEntries));
    }
    nextState->dialogueHistory = std::move(history);
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
    j["version"] = CurrentSaveFormatVersion;
    j["step_id"] = stepId;
    j["active_node_id"] = activeNodeId;
    j["typewriter_index"] = typewriterIndex;
    j["active_background"] = activeBackground;
    j["dsp_filter"] = dspFilter;
    j["active_bgm"] = activeBgm;
    j["bgm_volume"] = bgmVolume;
    j["bgm_playing"] = bgmPlaying;
    nlohmann::json history = nlohmann::json::array();
    if (dialogueHistory) {
        for (const auto& entry : *dialogueHistory) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
            });
        }
    }
    j["dialogue_history"] = std::move(history);

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

GameStateDecodeResult GameState::decodeJson(const std::string& jsonStr) {
    if (jsonStr.empty() || jsonStr.size() > kMaxSaveFileBytes) return {};
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
        if (version != 1 && version != 2 && version != CurrentSaveFormatVersion) {
            ROWL_LOG_ERROR("Unsupported GameState save version: " + std::to_string(version));
            return {nullptr, GameStateDecodeStatus::UnsupportedVersion, version};
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
            return {nullptr, GameStateDecodeStatus::InvalidData, version};
        }

        if (state->stepId == 0 || state->activeNodeId == 0) {
            ROWL_LOG_ERROR("GameState JSON contains an invalid step or node identifier");
            return {nullptr, GameStateDecodeStatus::InvalidData, version};
        }

        auto varMap = std::make_shared<VariableMap>();
        if (j.contains("variables") && j["variables"].is_object()) {
            if (j["variables"].size() > kMaxSaveVariables) {
                ROWL_LOG_ERROR("GameState JSON has too many variables");
                return {nullptr, GameStateDecodeStatus::InvalidData, version};
            }
            for (auto& el : j["variables"].items()) {
                if (el.key().empty() || el.key().size() > kMaxVariableKeyBytes) {
                    ROWL_LOG_ERROR("GameState JSON contains an invalid variable key");
                    return {nullptr, GameStateDecodeStatus::InvalidData, version};
                }
                std::string value;
                if (el.value().is_string()) {
                    value = el.value().get<std::string>();
                } else {
                    value = el.value().dump();
                }
                if (value.size() > kMaxVariableValueBytes) {
                    ROWL_LOG_ERROR("GameState JSON contains an oversized variable value");
                    return {nullptr, GameStateDecodeStatus::InvalidData, version};
                }
                varMap->data[el.key()] = std::move(value);
            }
        } else if (j.contains("variables")) {
            ROWL_LOG_ERROR("GameState JSON variables must be an object");
            return {nullptr, GameStateDecodeStatus::InvalidData, version};
        }
        state->variables = varMap;
        auto history = std::make_shared<std::vector<DialogueHistoryEntry>>();
        if (j.contains("dialogue_history")) {
            if (!j["dialogue_history"].is_array() ||
                j["dialogue_history"].size() > kMaxDialogueHistoryEntries) {
                ROWL_LOG_ERROR("GameState JSON dialogue history is invalid or too large");
                return {nullptr, GameStateDecodeStatus::InvalidData, version};
            }
            for (const auto& rawEntry : j["dialogue_history"]) {
                if (!rawEntry.is_object()) {
                    ROWL_LOG_ERROR("GameState JSON dialogue history entry must be an object");
                    return {nullptr, GameStateDecodeStatus::InvalidData, version};
                }
                DialogueHistoryEntry entry;
                entry.nodeId = rawEntry.value("node_id", uint64_t{0});
                entry.speaker = rawEntry.value("speaker", "");
                entry.dialogue = rawEntry.value("dialogue", "");
                entry.read = rawEntry.value("read", true);
                if (entry.nodeId == 0 || entry.speaker.size() > kMaxDialogueHistoryTextBytes ||
                    entry.dialogue.size() > kMaxDialogueHistoryTextBytes) {
                    ROWL_LOG_ERROR("GameState JSON contains an invalid dialogue history entry");
                    return {nullptr, GameStateDecodeStatus::InvalidData, version};
                }
                history->push_back(std::move(entry));
            }
        }
        state->dialogueHistory = std::move(history);
        state->previousState = nullptr;
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
    return SessionPersistence(saveDir).saveSlot(state, slotIndex);
}

std::shared_ptr<const GameState> GameState::loadFromSlot(
    int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(saveDir).loadSlot(slotIndex);
}

bool GameState::hasSlot(int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(saveDir).hasSlot(slotIndex);
}

bool GameState::deleteSlot(int32_t slotIndex, const std::string& saveDir) {
    return SessionPersistence(saveDir).deleteSlot(slotIndex);
}

} // namespace Rowl::State
