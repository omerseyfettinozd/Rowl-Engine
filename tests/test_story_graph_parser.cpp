/**
 * test_story_graph_parser.cpp — Pure story graph parsing and validation.
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/story_graph_parser.hpp"

void test_story_graph_parser() {
    using Rowl::Core::StoryGraphParseErrorKind;
    using Rowl::Core::StoryGraphParser;

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
}
