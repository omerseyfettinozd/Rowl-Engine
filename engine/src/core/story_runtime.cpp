#include "rowl/core/story_runtime.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace Rowl::Core {

bool StoryRuntime::commit(StoryGraphDocument document) {
    if (document.nodes.empty() ||
        document.nodes.find(document.startNodeId) == document.nodes.end()) {
        recordLoadFailure(
            "Story runtime rejected an invalid graph document; the active graph was preserved.");
        return false;
    }

    m_document = std::move(document);
    m_currentNodeId = m_document.startNodeId;
    m_lastLoadError.clear();
    ++m_revision;
    return true;
}

void StoryRuntime::recordLoadFailure(std::string error) {
    m_lastLoadError = std::move(error);
}

const StoryNode* StoryRuntime::node(uint64_t nodeId) const {
    const auto it = m_document.nodes.find(nodeId);
    return it == m_document.nodes.end() ? nullptr : &it->second;
}

StoryRuntime::AdvanceResult StoryRuntime::advance(uint32_t choiceIndex) {
    if (m_document.nodes.empty()) return AdvanceResult::NoGraph;

    const StoryNode* activeNode = currentNode();
    if (!activeNode) return AdvanceResult::CurrentNodeMissing;
    if (activeNode->nextNodes.empty() || choiceIndex >= activeNode->nextNodes.size()) {
        return AdvanceResult::ChoiceUnavailable;
    }

    const uint64_t targetNodeId = activeNode->nextNodes[choiceIndex].nodeId;
    if (targetNodeId == 0 || !node(targetNodeId)) {
        return AdvanceResult::TargetNodeMissing;
    }

    m_currentNodeId = targetNodeId;
    return AdvanceResult::Advanced;
}

std::optional<StoryRuntime::ChoiceSelection> StoryRuntime::resolveChoice(
    std::string_view optionId) const {
    if (optionId.empty()) return std::nullopt;

    const StoryNode* activeNode = currentNode();
    if (!activeNode) return std::nullopt;

    const auto option = std::find_if(
        activeNode->nextNodes.begin(), activeNode->nextNodes.end(),
        [optionId](const StoryNode::NextNode& candidate) {
            return candidate.optionId == optionId;
        });
    if (option == activeNode->nextNodes.end()) return std::nullopt;

    return ChoiceSelection{
        static_cast<uint32_t>(std::distance(activeNode->nextNodes.begin(), option)),
        option->nodeId};
}

bool StoryRuntime::resetToStart() {
    if (m_document.nodes.empty() ||
        m_document.nodes.find(m_document.startNodeId) == m_document.nodes.end()) {
        return false;
    }

    m_currentNodeId = m_document.startNodeId;
    return true;
}

} // namespace Rowl::Core
