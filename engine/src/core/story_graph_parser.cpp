#include "rowl/core/story_graph_parser.hpp"

#include <algorithm>
#include <utility>

namespace Rowl::Core {

namespace {

StoryGraphParseResult validationFailure(std::string message) {
    return StoryGraphParseResult::failure(
        StoryGraphParseErrorKind::Validation, std::move(message));
}

bool appendComponent(const nlohmann::json& componentJson, StoryNode& node,
                     std::string& error) {
    if (node.components.size() >= kMaxComponentsPerScene) {
        error = "Story graph node exceeds the maximum component count; rejected";
        return false;
    }
    if (!componentJson.is_object()) {
        error = "Story graph contains a non-object component; rejected";
        return false;
    }

    ComponentData component;
    component.type = componentJson.value("type", "");
    component.id = componentJson.value("id", "");
    component.enabled = componentJson.value("enabled", true);
    if (componentJson.contains("data")) component.data = componentJson["data"];
    node.components.push_back(std::move(component));
    return true;
}

} // namespace

StoryGraphParseResult StoryGraphParser::parse(std::string_view jsonContent) {
    if (jsonContent.empty()) {
        return validationFailure("Story graph JSON is empty; rejected");
    }
    if (jsonContent.size() > kMaxStoryJsonBytes) {
        return validationFailure(
            "Story graph JSON exceeds the maximum accepted size; rejected");
    }

    try {
        const auto data = nlohmann::json::parse(jsonContent);
        if (!data.is_object() || !data.contains("nodes") || !data["nodes"].is_array()) {
            return validationFailure(
                "Story graph must be an object containing a nodes array; rejected");
        }
        if (data["nodes"].size() > kMaxStoryNodes) {
            return validationFailure("Story graph exceeds the maximum node count; rejected");
        }

        StoryGraphParseResult result;
        auto& parsedNodes = result.document.nodes;
        uint64_t parsedStartId = data.value("start_node_id", static_cast<uint64_t>(101));

        for (const auto& nodeJson : data["nodes"]) {
            if (!nodeJson.is_object()) {
                return validationFailure("Story graph contains a non-object node; rejected");
            }

            StoryNode node;
            node.id = nodeJson.value("id", static_cast<uint64_t>(0));
            if (node.id == 0 || parsedNodes.contains(node.id)) {
                return validationFailure(
                    "Story graph contains a missing or duplicate node ID; rejected");
            }
            node.speaker = nodeJson.value("speaker", std::string{});
            node.dialogue = nodeJson.value("dialogue", std::string{});
            node.background = nodeJson.value("background", std::string{});
            node.backgroundX = nodeJson.value("background_x", 0.0f);
            node.backgroundY = nodeJson.value("background_y", 0.0f);
            node.backgroundWidth = nodeJson.value("background_width", 1920.0f);
            node.backgroundHeight = nodeJson.value("background_height", 1080.0f);
            node.character = nodeJson.value("character", std::string{});
            node.characterX = nodeJson.value("character_x", 1440.0f);
            node.characterY = nodeJson.value("character_y", 340.0f);
            node.characterWidth = nodeJson.value("character_width", 360.0f);
            node.characterHeight = nodeJson.value("character_height", 540.0f);
            node.characterScale = nodeJson.value("character_scale", 1.0f);
            node.dialogueBoxX = nodeJson.value("dialogue_box_x", 80.0f);
            node.dialogueBoxY = nodeJson.value("dialogue_box_y", 860.0f);
            node.dialogueBoxWidth = nodeJson.value("dialogue_box_width", 1760.0f);
            node.dialogueBoxHeight = nodeJson.value("dialogue_box_height", 180.0f);

            if (nodeJson.contains("components")) {
                if (!nodeJson["components"].is_array()) {
                    return validationFailure(
                        "Story graph node components must be an array; rejected");
                }
                for (const auto& componentJson : nodeJson["components"]) {
                    std::string error;
                    if (!appendComponent(componentJson, node, error)) {
                        return validationFailure(std::move(error));
                    }
                }
            }

            if (nodeJson.contains("objects") && !nodeJson["objects"].is_array()) {
                return validationFailure("Story graph node objects must be an array; rejected");
            }
            if (nodeJson.contains("objects")) {
                for (const auto& objectJson : nodeJson["objects"]) {
                    if (!objectJson.is_object()) {
                        return validationFailure(
                            "Story graph contains a non-object scene object; rejected");
                    }
                    if (!objectJson.value("is_active", true) ||
                        !objectJson.contains("components")) {
                        continue;
                    }
                    if (!objectJson["components"].is_array()) {
                        return validationFailure(
                            "Story graph object components must be an array; rejected");
                    }
                    for (const auto& componentJson : objectJson["components"]) {
                        std::string error;
                        const std::size_t previousSize = node.components.size();
                        if (!appendComponent(componentJson, node, error)) {
                            return validationFailure(std::move(error));
                        }
                        // Object components with no type were ignored by the legacy loader.
                        if (node.components.back().type.empty()) {
                            node.components.resize(previousSize);
                        }
                    }
                }
            }

            if (nodeJson.contains("next_nodes")) {
                if (!nodeJson["next_nodes"].is_array()) {
                    return validationFailure(
                        "Story graph node next_nodes must be an array; rejected");
                }
                for (const auto& nextJson : nodeJson["next_nodes"]) {
                    if (!nextJson.is_object()) {
                        return validationFailure(
                            "Story graph contains a non-object edge; rejected");
                    }
                    StoryNode::NextNode next;
                    next.nodeId = nextJson.value("id", static_cast<uint64_t>(0));
                    next.label = nextJson.value("label", std::string{});
                    next.optionId = nextJson.value("option_id", std::string{});
                    if (next.nodeId == 0) {
                        return validationFailure(
                            "Story graph contains an edge with no target node ID; rejected");
                    }
                    if (node.nextNodes.size() >= kMaxEdgesPerStoryNode) {
                        return validationFailure(
                            "Story graph node exceeds the maximum edge count; rejected");
                    }
                    node.nextNodes.push_back(std::move(next));
                }
            } else if (nodeJson.contains("next_id")) {
                const uint64_t nextId =
                    nodeJson.value("next_id", static_cast<uint64_t>(0));
                if (nextId != 0) node.nextNodes.push_back({nextId, "", ""});
            }

            parsedNodes.emplace(node.id, std::move(node));
        }

        if (parsedNodes.empty()) {
            return validationFailure(
                "Story graph does not contain any valid nodes; rejected");
        }
        for (const auto& [nodeId, node] : parsedNodes) {
            for (const auto& next : node.nextNodes) {
                if (!parsedNodes.contains(next.nodeId)) {
                    return validationFailure(
                        "Story graph node #" + std::to_string(nodeId) +
                        " references a missing node #" + std::to_string(next.nodeId) +
                        "; rejected");
                }
            }
        }
        if (data.contains("start_node_id") &&
            (parsedStartId == 0 || !parsedNodes.contains(parsedStartId))) {
            return validationFailure(
                "Story graph start_node_id does not reference a node; rejected");
        }

        if (parsedStartId == 0 || !parsedNodes.contains(parsedStartId)) {
            parsedStartId = std::min_element(
                parsedNodes.begin(), parsedNodes.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; })
                                ->first;
        }
        result.document.startNodeId = parsedStartId;
        return result;
    } catch (const nlohmann::json::parse_error& error) {
        return StoryGraphParseResult::failure(
            StoryGraphParseErrorKind::Parse,
            "Story graph JSON parse error: " + std::string(error.what()) + "; rejected");
    } catch (const std::exception& error) {
        return validationFailure(
            "Story graph load error: " + std::string(error.what()) + "; rejected");
    }
}

} // namespace Rowl::Core
