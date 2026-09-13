#include "rowl/core/story_runtime.hpp"

#include <utility>

namespace Rowl::Core {

bool StoryRuntime::commit(StoryGraphDocument document) {
    if (document.nodes.empty() ||
        document.nodes.find(document.startNodeId) == document.nodes.end()) {
        return false;
    }

    m_document = std::move(document);
    m_currentNodeId = m_document.startNodeId;
    return true;
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
