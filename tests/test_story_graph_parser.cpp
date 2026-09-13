/**
 * test_story_graph_parser.cpp — Pure story graph parsing and validation.
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/core/story_runtime.hpp"

#include <utility>

void test_story_graph_parser() {
    using Rowl::Core::StoryGraphParseErrorKind;
    using Rowl::Core::StoryGraphParser;
    using Rowl::Core::StoryRuntime;

    TEST_SECTION("Story Graph Parser Isolation");

    const auto valid = StoryGraphParser::parse(R"({
        "format_version": 4,
        "start_node_id": 101,
        "nodes": [
            {
                "id": 101,
                "speaker": "Narrator",
                "objects": [{
                    "is_active": true,
                    "components": [{
                        "type": "dialogue",
                        "id": "intro",
                        "enabled": true,
                        "data": {"text": "Hello"}
                    }]
                }],
                "next_nodes": [{"id": 202, "label": "Continue", "option_id": "next"}]
            },
            {"id": 202, "dialogue": "Done"}
        ]
    })");
    if (!valid.succeeded() || valid.document.startNodeId != 101 ||
        valid.document.nodes.size() != 2) {
        std::cerr << "Valid story graph was not parsed" << std::endl;
        exit(1);
    }
    const auto& start = valid.document.nodes.at(101);
    if (start.components.size() != 1 || start.components.front().type != "dialogue" ||
        start.nextNodes.size() != 1 || start.nextNodes.front().optionId != "next") {
        std::cerr << "Parsed story graph lost component or edge data" << std::endl;
        exit(1);
    }
    TEST_PASS("Valid graph produces a runtime-neutral document");

    const auto fallbackStart = StoryGraphParser::parse(
        R"({"nodes":[{"id":202},{"id":150}]})");
    if (!fallbackStart.succeeded() || fallbackStart.document.startNodeId != 150) {
        std::cerr << "Missing implicit node 101 did not select the lowest node ID" << std::endl;
        exit(1);
    }
    TEST_PASS("Legacy graph start fallback remains deterministic");

    const auto invalidEdge = StoryGraphParser::parse(
        R"({"start_node_id":101,"nodes":[{"id":101,"next_nodes":[{"id":999}]}]})");
    if (invalidEdge.succeeded() ||
        invalidEdge.errorKind != StoryGraphParseErrorKind::Validation ||
        invalidEdge.message.find("missing node #999") == std::string::npos) {
        std::cerr << "Dangling graph edge was not rejected with a validation result" << std::endl;
        exit(1);
    }
    TEST_PASS("Semantic validation rejects dangling edges");

    const auto malformed = StoryGraphParser::parse("{ definitely not json }");
    if (malformed.succeeded() || malformed.errorKind != StoryGraphParseErrorKind::Parse) {
        std::cerr << "Malformed JSON was not classified as a parse error" << std::endl;
        exit(1);
    }
    TEST_PASS("Syntax failures remain distinct from validation failures");

    StoryRuntime runtime;
    auto initialDocument = StoryGraphParser::parse(
        R"({"start_node_id":101,"nodes":[{"id":101,"next_nodes":[)"
        R"({"id":202,"option_id":"next"}]},{"id":202}]})");
    if (!initialDocument.succeeded() ||
        !runtime.commit(std::move(initialDocument.document))) {
        std::cerr << "Valid document was not committed to StoryRuntime" << std::endl;
        exit(1);
    }
    runtime.setCurrentNodeId(202);

    Rowl::Core::StoryGraphDocument invalidDocument;
    invalidDocument.startNodeId = 999;
    Rowl::Core::StoryNode invalidNode;
    invalidNode.id = 303;
    invalidDocument.nodes.emplace(invalidNode.id, std::move(invalidNode));
    if (runtime.commit(std::move(invalidDocument)) || runtime.size() != 2 ||
        runtime.startNodeId() != 101 || runtime.currentNodeId() != 202) {
        std::cerr << "Rejected document changed committed StoryRuntime state" << std::endl;
        exit(1);
    }
    TEST_PASS("StoryRuntime commits graph ownership transactionally");

    if (!runtime.resetToStart() || runtime.currentNodeId() != 101) {
        std::cerr << "StoryRuntime did not restore the committed start node" << std::endl;
        exit(1);
    }
    TEST_PASS("StoryRuntime owns the start and current node cursor");

    const auto choice = runtime.resolveChoice("next");
    if (!choice || choice->index != 0 || choice->targetNodeId != 202 ||
        runtime.advance(choice->index) != StoryRuntime::AdvanceResult::Advanced ||
        runtime.currentNodeId() != choice->targetNodeId) {
        std::cerr << "StoryRuntime did not resolve and advance the selected choice" << std::endl;
        exit(1);
    }
    const auto terminalNodeId = runtime.currentNodeId();
    if (runtime.advance() != StoryRuntime::AdvanceResult::ChoiceUnavailable ||
        runtime.currentNodeId() != terminalNodeId || runtime.resolveChoice("missing")) {
        std::cerr << "Unavailable navigation changed the StoryRuntime cursor" << std::endl;
        exit(1);
    }
    TEST_PASS("StoryRuntime owns advance and stable choice resolution");
}
