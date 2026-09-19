#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "rowl/core/story_graph_vnext.hpp"

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
    /// Owning vNext chapter id; empty when the node is unassigned (v4 graphs).
    std::string chapterId;
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
    /// vNext structure contract; empty for format v4 documents.
    std::vector<GraphGroup> groups;
    std::vector<GraphSubgraph> subgraphs;
    std::vector<GraphChapter> chapters;
};

/// Max serialized graph-identity bytes accepted from save files (D4/G #70).
inline constexpr std::size_t kMaxGraphIdentityBytes = 64;

/// Content identity of a story graph (D4/G #70): FNV-1a-64 over the start id
/// plus the nodes in sorted id order (id, chapter, speaker, dialogue,
/// background, character, next ids/labels, component type/id/enabled flags).
/// Empty document → "" (nothing committed yet; callers treat it as
/// "nothing to compare against", same legacy-accept as the V gate).
/// Change-detector, not a security hash; deterministic across runs.
inline std::string computeGraphIdentity(const StoryGraphDocument& doc) {
    if (doc.nodes.empty()) return {};
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::string_view piece) {
        for (const unsigned char c : piece) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        // Field separator so ("ab","c") and ("a","bc") hash differently.
        hash ^= 0xFFu;
        hash *= 1099511628211ull;
    };
    const auto mixU64 = [&mix](uint64_t value) {
        char buf[8];
        for (int i = 0; i < 8; ++i) {
            buf[i] = static_cast<char>((value >> (i * 8)) & 0xFF);
        }
        mix(std::string_view(buf, 8));
    };
    mixU64(doc.startNodeId);
    std::vector<uint64_t> ids;
    ids.reserve(doc.nodes.size());
    for (const auto& [id, node] : doc.nodes) {
        (void)node;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    mixU64(ids.size());
    for (const uint64_t id : ids) {
        const StoryNode& node = doc.nodes.at(id);
        mixU64(node.id);
        mix(node.chapterId);
        mix(node.speaker);
        mix(node.dialogue);
        mix(node.background);
        mix(node.character);
        mixU64(node.nextNodes.size());
        for (const auto& next : node.nextNodes) {
            mixU64(next.nodeId);
            mix(next.label);
            mix(next.optionId);
        }
        mixU64(node.components.size());
        for (const auto& component : node.components) {
            mix(component.type);
            mix(component.id);
            mix(component.enabled ? "1" : "0");
        }
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(hex);
}

} // namespace Rowl::Core
