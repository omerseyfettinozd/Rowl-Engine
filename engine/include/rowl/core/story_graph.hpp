#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace Rowl::Core {

inline constexpr std::size_t kMaxStoryJsonBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxStoryNodes = 10'000;
inline constexpr std::size_t kMaxEdgesPerStoryNode = 4'096;
inline constexpr std::size_t kMaxComponentsPerScene = 2'048;

/// Serialized component data attached to a story node.
struct ComponentData {
    std::string type;
    std::string id;
    bool enabled = true;
    nlohmann::json data;
};

/// Runtime-neutral representation produced by StoryGraphParser.
struct StoryNode {
    uint64_t id = 0;
    std::string speaker;
    std::string dialogue;
    std::string background;
    float backgroundX = 0.0f;
    float backgroundY = 0.0f;
    float backgroundWidth = 1920.0f;
    float backgroundHeight = 1080.0f;
    std::string character;
    float characterX = 1440.0f;
    float characterY = 340.0f;
    float characterWidth = 360.0f;
    float characterHeight = 540.0f;
    float characterScale = 1.0f;
    float dialogueBoxX = 80.0f;
    float dialogueBoxY = 860.0f;
    float dialogueBoxWidth = 1760.0f;
    float dialogueBoxHeight = 180.0f;

    struct NextNode {
        uint64_t nodeId = 0;
        std::string label;
        std::string optionId;
    };
    std::vector<NextNode> nextNodes;
    std::vector<ComponentData> components;
};

struct StoryGraphDocument {
    std::unordered_map<uint64_t, StoryNode> nodes;
    uint64_t startNodeId = 0;
};

} // namespace Rowl::Core
