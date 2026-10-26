#include "rowl/core/story_graph_parser.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/scripting/reserved_names.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Rowl::Core {

namespace {

// Same ceilings the saved-variable map applies (game_state.cpp), so a declared
// variable can always be saved.
constexpr std::size_t kMaxDeclaredVariables = 10000;
constexpr std::size_t kMaxDeclaredVariableNameBytes = 256;
constexpr std::size_t kMaxDeclaredVariableValueBytes = 64 * 1024;

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

// F2 (D-O7): two options of one node with the same option_id used to share ONE identity: focus
// (kept by id) stuck on the first copy and digit 2 picked the first again. The second and
// later copies get "<id>#<n>" (in the choice component AND on the n-th edge carrying that id,
// so each button still leads where its own edge points) and the node warns once.
void dedupeOptionIds(StoryNode& node) {
    std::unordered_map<std::string, int> seen;
    std::vector<std::string> renamed;
    for (auto& component : node.components) {
        if (component.type != "choice" || !component.data.is_object()) continue;
        auto options = component.data.find("options");
        if (options == component.data.end() || !options->is_array()) continue;
        for (auto& option : *options) {
            if (!option.is_object()) continue;
            const auto idIt = option.find("option_id");
            if (idIt == option.end() || !idIt->is_string()) continue;
            const std::string id = idIt->get<std::string>();
            if (id.empty()) continue;
            const int count = ++seen[id];
            if (count <= 1) continue;
            option["option_id"] = id + "#" + std::to_string(count);
            renamed.push_back(id);
        }
    }
    if (renamed.empty()) return;
    std::unordered_map<std::string, int> edgeSeen;
    for (auto& edge : node.nextNodes) {
        if (edge.optionId.empty()) continue;
        const int count = ++edgeSeen[edge.optionId];
        if (count > 1 && seen.contains(edge.optionId)) edge.optionId += "#" + std::to_string(count);
    }
    std::string list;
    for (const std::string& id : renamed) list += (list.empty() ? "'" : ", '") + id + "'";
    ROWL_LOG_WARN("Story graph node #" + std::to_string(node.id) + " repeats option_id " + list +
                  "; the repeats were renamed '<id>#2', '<id>#3' ... so every option is its own choice");
}

} // namespace

bool StoryGraphParser::checkFormatVersion(const nlohmann::json& root, std::string& error) {
    if (!root.is_object()) return true;
    const auto it = root.find("format_version");
    if (it == root.end() || !it->is_number()) return true;
    // uint64 disina tasan degerler nlohmann'da double olur; yine de "daha yeni".
    if (it->is_number_float()) {
        const double value = it->get<double>();
        if (!(value > static_cast<double>(kStoryGraphFormatVersion))) return true;
        error = "Story graph format_version is newer than this engine supports (" +
                std::to_string(kStoryGraphFormatVersion) + "); rejected. Update Rowl.";
        return false;
    }
    const int64_t version = it->is_number_unsigned()
        ? static_cast<int64_t>(std::min<uint64_t>(it->get<uint64_t>(), INT64_MAX))
        : it->get<int64_t>();
    if (version <= kStoryGraphFormatVersion) return true;
    error = "Story graph format_version " + std::to_string(version) +
            " is newer than this engine supports (" +
            std::to_string(kStoryGraphFormatVersion) + "); rejected. Update Rowl.";
    return false;
}

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

        if (std::string versionError; !checkFormatVersion(data, versionError)) {
            return validationFailure(std::move(versionError));
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
            if (nodeJson.contains("chapter_id")) {
                if (!nodeJson["chapter_id"].is_string()) {
                    return validationFailure(
                        "Story graph node chapter_id must be a string; rejected");
                }
                node.chapterId = nodeJson["chapter_id"].get<std::string>();
                if (node.chapterId.size() > kMaxGraphIdChars) {
                    return validationFailure(
                        "Story graph node chapter_id exceeds its length limit; rejected");
                }
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

            dedupeOptionIds(node);
            parsedNodes.emplace(node.id, std::move(node));
        }

        if (parsedNodes.empty()) {
            return validationFailure(
                "Story graph does not contain any valid nodes; rejected");
        }
        {
            std::string structureError;
            if (!parseGraphStructure(data, result.document, structureError)) {
                return validationFailure(structureError);
            }
            // parsedNodes aliases result.document.nodes; structure parsing only
            // appended groups/subgraphs/chapters, so references stay valid.
            if (!validateGraphStructure(result.document, structureError)) {
                return validationFailure(structureError);
            }
        }
        if (data.contains("variables")) {
            // F2 (B-7): project-level variable declarations.
            const auto& declared = data["variables"];
            if (!declared.is_object()) {
                return validationFailure("Story graph variables must be an object; rejected");
            }
            if (declared.size() > kMaxDeclaredVariables) {
                return validationFailure("Story graph declares too many variables; rejected");
            }
            for (const auto& item : declared.items()) {
                const std::string& name = item.key();
                if (name.empty() || name.size() > kMaxDeclaredVariableNameBytes) {
                    return validationFailure("Story graph declares a variable with an invalid name; rejected");
                }
                if (Rowl::Scripting::isReservedVariableName(name)) {
                    return validationFailure("Story graph variable '" + name +
                                             "' is a reserved engine name; rejected");
                }
                std::string value;
                if (item.value().is_string()) value = item.value().get<std::string>();
                else if (item.value().is_number() || item.value().is_boolean()) value = item.value().dump();
                else {
                    return validationFailure("Story graph variable '" + name +
                                             "' must be a string, number or boolean; rejected");
                }
                if (value.size() > kMaxDeclaredVariableValueBytes) {
                    return validationFailure("Story graph variable '" + name + "' is too long; rejected");
                }
                result.document.variables.emplace_back(name, std::move(value));
            }
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
