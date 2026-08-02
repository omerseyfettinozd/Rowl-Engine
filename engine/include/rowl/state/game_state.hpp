#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace Rowl::State {

struct VariableMap {
    std::unordered_map<std::string, std::string> data;
};

// GCC 16 false positive -Warray-bounds with shared_ptr template internals
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

struct GameState {
    // POD members first
    uint64_t stepId = 0;
    uint64_t activeNodeId = 101;
    uint32_t typewriterIndex = 0;

    // String members
    std::string activeBackground = "bg_beach_sunset.png";
    std::string dspFilter = "Normal";

    // Smart pointers last
    std::shared_ptr<const VariableMap> variables = std::make_shared<VariableMap>();
    std::shared_ptr<const GameState> previousState = nullptr;

    std::string getVariable(const std::string& key, const std::string& defaultValue = "") const;

    static std::shared_ptr<const GameState> createInitialState(uint64_t startNodeId = 101);
    static std::shared_ptr<const GameState> createNextState(
        const std::shared_ptr<const GameState>& current,
        uint64_t nextNodeId,
        const std::string& varKey = "",
        const std::string& varValue = ""
    );

    static std::shared_ptr<const GameState> rewind(
        const std::shared_ptr<const GameState>& current,
        uint64_t stepsToRewind = 1
    );
};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

} // namespace Rowl::State