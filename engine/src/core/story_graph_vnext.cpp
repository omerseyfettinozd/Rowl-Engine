#include "rowl/core/story_graph_vnext.hpp"

#include "rowl/core/story_graph.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Rowl::Core {

namespace {

bool readId(const nlohmann::json& value, std::string& out, std::string& error,
            const char* context) {
    if (!value.is_string()) {
        error = std::string(context) + " id must be a string; rejected";
        return false;
    }
    out = value.get<std::string>();
    if (out.empty() || out.size() > kMaxGraphIdChars) {
        error = std::string(context) + " id must be 1..128 characters; rejected";
        return false;
    }
    return true;
}

bool readCappedString(const nlohmann::json& object, const char* key, std::string& out,
                      std::size_t maxChars, std::string& error, const char* context) {
    if (!object.contains(key)) {
        out.clear();
        return true;
    }
    const auto& value = object.at(key);
    if (!value.is_string()) {
        error = std::string(context) + " '" + key + "' must be a string; rejected";
        return false;
    }
    out = value.get<std::string>();
    if (out.size() > maxChars) {
        error = std::string(context) + " '" + key + "' exceeds its length limit; rejected";
        return false;
    }
    return true;
}

bool readNodeIdArray(const nlohmann::json& object, const char* key,
                     std::vector<uint64_t>& out, std::string& error,
                     const char* context, bool required) {
    out.clear();
    if (!object.contains(key)) {
        if (required) {
            error = std::string(context) + " is missing '" + key + "'; rejected";
            return false;
        }
        return true;
    }
    const auto& array = object.at(key);
    if (!array.is_array()) {
        error = std::string(context) + " '" + key + "' must be an array; rejected";
        return false;
    }
    if (array.size() > kMaxGraphMembersPerEntry) {
        error = std::string(context) + " '" + key + "' exceeds its entry limit; rejected";
        return false;
    }
    for (const auto& item : array) {
        if (!item.is_number_unsigned() || item.get<uint64_t>() == 0) {
            error = std::string(context) + " '" + key + "' must list nonzero node ids; rejected";
            return false;
        }
        out.push_back(item.get<uint64_t>());
    }
    return true;
}

bool readDouble(const nlohmann::json& object, const char* key, double& out,
                std::string& error, const char* context) {
    if (!object.contains(key)) {
        out = 0.0;
        return true;
    }
    const auto& value = object.at(key);
    if (!value.is_number()) {
        error = std::string(context) + " '" + key + "' must be a number; rejected";
        return false;
    }
    out = value.get<double>();
    return true;
}

bool checkKnownNodes(const std::vector<uint64_t>& ids,
                     const std::unordered_map<uint64_t, StoryNode>& nodes,
                     std::string& error, const char* context) {
    for (uint64_t id : ids) {
        if (!nodes.contains(id)) {
            error = std::string(context) + " references missing node #" +
                    std::to_string(id) + "; rejected";
            return false;
        }
    }
    return true;
}

const GraphSubgraph* ownerOf(uint64_t nodeId,
                             const std::unordered_map<uint64_t, const GraphSubgraph*>& owner) {
    const auto it = owner.find(nodeId);
    return it == owner.end() ? nullptr : it->second;
}

} // namespace

bool parseGraphStructure(const nlohmann::json& data, StoryGraphDocument& document,
                         std::string& error) {
    if (data.contains("groups")) {
        const auto& groups = data.at("groups");
        if (!groups.is_array()) {
            error = "Story graph groups must be an array; rejected";
            return false;
        }
        if (groups.size() > kMaxGraphGroups) {
            error = "Story graph exceeds the maximum group count; rejected";
            return false;
        }
        for (const auto& item : groups) {
            if (!item.is_object()) {
                error = "Story graph contains a non-object group; rejected";
                return false;
            }
            GraphGroup group;
            if (!item.contains("id") ||
                !readId(item.at("id"), group.id, error, "Story graph group") ||
                !readCappedString(item, "title", group.title, kMaxGraphTitleChars, error,
                                 "Story graph group") ||
                !readCappedString(item, "color", group.color, kMaxGraphTitleChars, error,
                                 "Story graph group") ||
                !readDouble(item, "x", group.x, error, "Story graph group") ||
                !readDouble(item, "y", group.y, error, "Story graph group") ||
                !readDouble(item, "width", group.width, error, "Story graph group") ||
                !readDouble(item, "height", group.height, error, "Story graph group") ||
                !readNodeIdArray(item, "node_ids", group.nodeIds, error,
                                 "Story graph group", false)) {
                return false;
            }
            document.groups.push_back(std::move(group));
        }
    }

    if (data.contains("subgraphs")) {
        const auto& subgraphs = data.at("subgraphs");
        if (!subgraphs.is_array()) {
            error = "Story graph subgraphs must be an array; rejected";
            return false;
        }
        if (subgraphs.size() > kMaxGraphSubgraphs) {
            error = "Story graph exceeds the maximum subgraph count; rejected";
            return false;
        }
        for (const auto& item : subgraphs) {
            if (!item.is_object()) {
                error = "Story graph contains a non-object subgraph; rejected";
                return false;
            }
            GraphSubgraph subgraph;
            if (!item.contains("id") ||
                !readId(item.at("id"), subgraph.id, error, "Story graph subgraph") ||
                !readCappedString(item, "title", subgraph.title, kMaxGraphTitleChars, error,
                                 "Story graph subgraph")) {
                return false;
            }
            if (!item.contains("entry_node_id") || !item.at("entry_node_id").is_number_unsigned() ||
                item.at("entry_node_id").get<uint64_t>() == 0) {
                error = "Story graph subgraph '" + subgraph.id +
                        "' needs a nonzero entry_node_id; rejected";
                return false;
            }
            subgraph.entryNodeId = item.at("entry_node_id").get<uint64_t>();
            if (!readNodeIdArray(item, "exit_node_ids", subgraph.exitNodeIds, error,
                                 ("Story graph subgraph '" + subgraph.id + "'").c_str(), false) ||
                !readNodeIdArray(item, "node_ids", subgraph.nodeIds, error,
                                 ("Story graph subgraph '" + subgraph.id + "'").c_str(), false)) {
                return false;
            }
            if (subgraph.exitNodeIds.size() > kMaxGraphPortsPerSubgraph) {
                error = "Story graph subgraph '" + subgraph.id +
                        "' exceeds its exit port limit; rejected";
                return false;
            }
            document.subgraphs.push_back(std::move(subgraph));
        }
    }

    if (data.contains("chapters")) {
        const auto& chapters = data.at("chapters");
        if (!chapters.is_array()) {
            error = "Story graph chapters must be an array; rejected";
            return false;
        }
        if (chapters.size() > kMaxGraphChapters) {
            error = "Story graph exceeds the maximum chapter count; rejected";
            return false;
        }
        for (const auto& item : chapters) {
            if (!item.is_object()) {
                error = "Story graph contains a non-object chapter; rejected";
                return false;
            }
            GraphChapter chapter;
            if (!item.contains("id") ||
                !readId(item.at("id"), chapter.id, error, "Story graph chapter") ||
                !readCappedString(item, "title", chapter.title, kMaxGraphTitleChars, error,
                                 "Story graph chapter") ||
                !readCappedString(item, "summary", chapter.summary, kMaxGraphSummaryChars,
                                 error, "Story graph chapter")) {
                return false;
            }
            if (item.contains("order")) {
                if (!item.at("order").is_number_integer()) {
                    error = "Story graph chapter '" + chapter.id +
                            "' order must be an integer; rejected";
                    return false;
                }
                chapter.order = item.at("order").get<int>();
            }
            if (item.contains("start_node_id")) {
                if (!item.at("start_node_id").is_number_unsigned() ||
                    item.at("start_node_id").get<uint64_t>() == 0) {
                    error = "Story graph chapter '" + chapter.id +
                            "' start_node_id must be a nonzero node id; rejected";
                    return false;
                }
                chapter.startNodeId = item.at("start_node_id").get<uint64_t>();
                chapter.hasStartNodeId = true;
            }
            document.chapters.push_back(std::move(chapter));
        }
    }
    return true;
}

bool validateGraphStructure(const StoryGraphDocument& document, std::string& error) {
    const auto& nodes = document.nodes;

    auto checkDuplicateIds = [&](const auto& entries, const char* kind) {
        std::unordered_set<std::string> seen;
        for (const auto& entry : entries) {
            if (!seen.insert(entry.id).second) {
                error = std::string("Story graph contains a duplicate ") + kind + " id '" +
                        entry.id + "'; rejected";
                return false;
            }
        }
        return true;
    };
    if (!checkDuplicateIds(document.groups, "group") ||
        !checkDuplicateIds(document.subgraphs, "subgraph") ||
        !checkDuplicateIds(document.chapters, "chapter")) {
        return false;
    }

    for (const auto& group : document.groups) {
        if (!checkKnownNodes(group.nodeIds, nodes, error,
                             ("Story graph group '" + group.id + "'").c_str())) {
            return false;
        }
    }

    std::unordered_map<uint64_t, const GraphSubgraph*> owner;
    std::unordered_set<std::string> chapterIds;
    for (const auto& chapter : document.chapters) chapterIds.insert(chapter.id);

    for (const auto& subgraph : document.subgraphs) {
        const std::string context = "Story graph subgraph '" + subgraph.id + "'";
        if (!nodes.contains(subgraph.entryNodeId)) {
            error = context + " entry references missing node #" +
                    std::to_string(subgraph.entryNodeId) + "; rejected";
            return false;
        }
        if (!checkKnownNodes(subgraph.nodeIds, nodes, error, context.c_str()) ||
            !checkKnownNodes(subgraph.exitNodeIds, nodes, error, context.c_str())) {
            return false;
        }
        std::unordered_set<uint64_t> members(subgraph.nodeIds.begin(), subgraph.nodeIds.end());
        if (!members.contains(subgraph.entryNodeId)) {
            error = context + " entry node #" + std::to_string(subgraph.entryNodeId) +
                    " is not a member; rejected";
            return false;
        }
        for (uint64_t exitId : subgraph.exitNodeIds) {
            if (!members.contains(exitId)) {
                error = context + " exit node #" + std::to_string(exitId) +
                        " is not a member; rejected";
                return false;
            }
        }
        for (uint64_t memberId : members) {
            const auto existing = owner.find(memberId);
            if (existing != owner.end()) {
                error = "Story graph node #" + std::to_string(memberId) +
                        " belongs to subgraphs '" + existing->second->id + "' and '" +
                        subgraph.id + "'; rejected";
                return false;
            }
            owner[memberId] = &subgraph;
        }
    }

    // Port discipline: flow crosses a subgraph boundary only through ports.
    for (const auto& [nodeId, node] : nodes) {
        const GraphSubgraph* sourceOwner = ownerOf(nodeId, owner);
        for (const auto& next : node.nextNodes) {
            const auto targetIt = nodes.find(next.nodeId);
            if (targetIt == nodes.end()) continue; // Missing targets fail earlier.
            const GraphSubgraph* targetOwner = ownerOf(next.nodeId, owner);
            if (sourceOwner == targetOwner) continue;
            if (sourceOwner != nullptr) {
                const auto& exits = sourceOwner->exitNodeIds;
                if (std::find(exits.begin(), exits.end(), nodeId) == exits.end()) {
                    error = "Story graph edge #" + std::to_string(nodeId) + " -> #" +
                            std::to_string(next.nodeId) + " leaves subgraph '" +
                            sourceOwner->id + "' from a non-exit node; rejected";
                    return false;
                }
            }
            if (targetOwner != nullptr && next.nodeId != targetOwner->entryNodeId) {
                error = "Story graph edge #" + std::to_string(nodeId) + " -> #" +
                        std::to_string(next.nodeId) + " enters subgraph '" +
                        targetOwner->id + "' at a non-entry node; rejected";
                return false;
            }
        }
    }

    // Entry-to-exit reachability inside each member set.
    for (const auto& subgraph : document.subgraphs) {
        std::unordered_set<uint64_t> members(subgraph.nodeIds.begin(), subgraph.nodeIds.end());
        std::unordered_set<uint64_t> reached{subgraph.entryNodeId};
        std::vector<uint64_t> frontier{subgraph.entryNodeId};
        while (!frontier.empty()) {
            uint64_t current = frontier.back();
            frontier.pop_back();
            const auto nodeIt = nodes.find(current);
            if (nodeIt == nodes.end()) continue;
            for (const auto& next : nodeIt->second.nextNodes) {
                if (members.contains(next.nodeId) && reached.insert(next.nodeId).second) {
                    frontier.push_back(next.nodeId);
                }
            }
        }
        for (uint64_t exitId : subgraph.exitNodeIds) {
            if (!reached.contains(exitId)) {
                error = "Story graph subgraph '" + subgraph.id + "' exit node #" +
                        std::to_string(exitId) +
                        " is unreachable from its entry; rejected";
                return false;
            }
        }
    }

    // Orphan chapter references and chapter entry hints.
    for (const auto& [nodeId, node] : nodes) {
        if (!node.chapterId.empty() && !chapterIds.contains(node.chapterId)) {
            error = "Story graph node #" + std::to_string(nodeId) + " references unknown chapter '" +
                    node.chapterId + "'; rejected";
            return false;
        }
    }
    for (const auto& chapter : document.chapters) {
        if (chapter.hasStartNodeId && !nodes.contains(chapter.startNodeId)) {
            error = "Story graph chapter '" + chapter.id + "' start references missing node #" +
                    std::to_string(chapter.startNodeId) + "; rejected";
            return false;
        }
    }

    // Subgraph call-graph cycles: direct member-to-member boundary crossings
    // are calls, and a call cycle has no call-stack semantics at runtime.
    std::unordered_map<std::string, std::unordered_set<std::string>> calls;
    for (const auto& [nodeId, node] : nodes) {
        const GraphSubgraph* sourceOwner = ownerOf(nodeId, owner);
        if (sourceOwner == nullptr) continue;
        for (const auto& next : node.nextNodes) {
            const GraphSubgraph* targetOwner = ownerOf(next.nodeId, owner);
            if (targetOwner != nullptr && targetOwner != sourceOwner) {
                calls[sourceOwner->id].insert(targetOwner->id);
            }
        }
    }
    std::unordered_map<std::string, int> visit; // 0 = unvisited, 1 = on stack, 2 = done
    std::vector<std::string> stack;
    std::function<bool(const std::string&)> visitSubgraph = [&](const std::string& id) {
        visit[id] = 1;
        stack.push_back(id);
        const auto it = calls.find(id);
        if (it != calls.end()) {
            for (const auto& callee : it->second) {
                if (visit[callee] == 1) {
                    error = "Story graph subgraph call cycle detected: ";
                    bool recording = false;
                    for (const auto& frame : stack) {
                        if (frame == callee) recording = true;
                        if (recording) error += "'" + frame + "' -> ";
                    }
                    error += "'" + callee + "'; rejected";
                    return false;
                }
                if (visit[callee] == 0 && !visitSubgraph(callee)) return false;
            }
        }
        stack.pop_back();
        visit[id] = 2;
        return true;
    };
    for (const auto& subgraph : document.subgraphs) {
        if (visit[subgraph.id] == 0 && !visitSubgraph(subgraph.id)) return false;
    }

    return true;
}

} // namespace Rowl::Core
