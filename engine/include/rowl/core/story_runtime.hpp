#pragma once

#include "rowl/core/story_graph.hpp"

#include <cstdint>

namespace Rowl::Core {

/// Owns the committed story graph and its runtime node cursor.
///
/// A candidate document replaces the active graph only when its start-node
/// invariant is valid. This keeps graph, start-node and current-node ownership
/// together instead of exposing a partially updated runtime state.
class StoryRuntime {
public:
    bool commit(StoryGraphDocument document);

    bool empty() const { return m_document.nodes.empty(); }
    std::size_t size() const { return m_document.nodes.size(); }
    const std::unordered_map<uint64_t, StoryNode>& nodes() const {
        return m_document.nodes;
    }

    uint64_t startNodeId() const { return m_document.startNodeId; }
    uint64_t currentNodeId() const { return m_currentNodeId; }
    void setCurrentNodeId(uint64_t nodeId) { m_currentNodeId = nodeId; }

    /// Restores the committed start node. Returns false when no graph exists.
    bool resetToStart();

private:
    StoryGraphDocument m_document;
    uint64_t m_currentNodeId = 101;
};

} // namespace Rowl::Core
