// engine_node_conditions.cpp — D06-1: the node-level "condition" component.
//
// The editor writes {"type":"condition","data":{"expression":"gold >= 10",
// "fail_target_node_id":"42"}} on a node. Entering the node (a real entry:
// advance, choice, new game, graph mount - never load/rewind/scene restore)
// evaluates the node's condition components first, in order. The first false
// one decides: with a valid fail target the node is NOT played - the cursor
// moves to the target before any entry effect (variable, sfx/bgm/voice,
// script, transition, camera, history row, read mark, dialogue) runs, and the
// target's own entry takes over (its own conditions included).
//
// The evaluator is the choice-option one (choiceConditionHolds: Lua
// condition path, fail-closed). Its error policy is therefore the choice
// policy as well: a Lua syntax/runtime/budget error counts as "false".

#include "rowl/core/engine.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/scripting/lua_sandbox.hpp"

#include <cstdint>
#include <string>

namespace Rowl::Core {

namespace {

// A redirect chain longer than this is a cycle (A -> B -> A, or a node that
// fails onto itself): the node reached at the limit plays normally.
constexpr int kMaxConditionRedirects = 16;

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The editor writes the id as a string, hand-written graphs as a number.
// Node ids are positive integers; anything else is "no valid target".
bool parseTargetNodeId(const nlohmann::json& value, uint64_t& out) {
    if (value.is_number_unsigned()) {
        out = value.get<uint64_t>();
        return out != 0;
    }
    if (value.is_number_integer()) {
        const int64_t signedId = value.get<int64_t>();
        if (signedId <= 0) return false;
        out = static_cast<uint64_t>(signedId);
        return true;
    }
    if (value.is_string()) {
        const std::string text = trimmed(value.get<std::string>());
        if (text.empty() || text.size() > 19) return false;  // 19 digits fit uint64
        uint64_t parsed = 0;
        for (const char ch : text) {
            if (ch < '0' || ch > '9') return false;
            parsed = parsed * 10u + static_cast<uint64_t>(ch - '0');
        }
        out = parsed;
        return out != 0;
    }
    return false;
}

}  // namespace

void Engine::resolveNodeEntryConditions() {
    const auto warnOnce = [this](uint64_t nodeId, const std::string& message) {
        // One line per node and graph: a redirect that fires on every pass
        // through a mis-authored node must not flood the log.
        if (m_conditionWarnedNodes.insert(nodeId).second) ROWL_LOG_WARN(message);
    };

    for (int redirects = 0;; ++redirects) {
        const StoryNode* node = m_storyRuntime.currentNode();
        if (node == nullptr) return;

        const ComponentData* failing = nullptr;
        std::string failingExpression;
        for (const auto& component : node->components) {
            if (component.type != "condition" || !component.enabled) continue;
            if (!component.data.is_object()) continue;
            const auto expressionIt = component.data.find("expression");
            if (expressionIt == component.data.end() || !expressionIt->is_string()) continue;
            const std::string expression = trimmed(expressionIt->get<std::string>());
            if (expression.empty()) continue;  // nothing to evaluate: open
            if (choiceConditionHolds(expression)) continue;
            if (m_luaSandbox && !m_luaSandbox->getLastError().empty()) {
                warnOnce(node->id, "Node #" + std::to_string(node->id) + " condition '" + expression +
                                       "' failed to evaluate (" + m_luaSandbox->getLastError() +
                                       "); treated as false");
            }
            failing = &component;
            failingExpression = expression;
            break;
        }
        if (failing == nullptr) return;  // every condition holds: the node plays

        uint64_t target = 0;
        const auto targetIt = failing->data.find("fail_target_node_id");
        const bool targetParsed =
            targetIt != failing->data.end() && parseTargetNodeId(*targetIt, target);
        if (!targetParsed || m_storyRuntime.node(target) == nullptr) {
            warnOnce(node->id, "Node #" + std::to_string(node->id) + " condition '" + failingExpression +
                                   "' is false but has no valid fail_target_node_id; "
                                   "the node plays normally");
            return;
        }
        if (redirects >= kMaxConditionRedirects) {
            warnOnce(node->id, "Node condition redirects exceeded " +
                                   std::to_string(kMaxConditionRedirects) +
                                   " steps (cycle through node #" + std::to_string(node->id) +
                                   "); the node plays normally");
            return;
        }
        ROWL_LOG_INFO("Node #" + std::to_string(node->id) + " condition '" + failingExpression +
                      "' is false; redirecting to node #" + std::to_string(target));
        m_storyRuntime.setCurrentNodeId(target);
        m_lastAdvanceRedirected = true;
    }
}

}  // namespace Rowl::Core
