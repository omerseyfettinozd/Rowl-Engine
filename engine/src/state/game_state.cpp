#include "rowl/state/game_state.hpp"
#include "rowl/core/logger.hpp"

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

} // namespace Rowl::State