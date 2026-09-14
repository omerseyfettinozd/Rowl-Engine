/**
 * test_graph_vnext.cpp — Graph vNext (format v5) structure contract:
 * groups / subgraphs / chapters parsing, validation and v4 back-compat.
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/core/story_runtime.hpp"

#include <utility>

namespace {

const char* kValidV5 = R"({
    "format_version": 5,
    "start_node_id": 101,
    "nodes": [
        {"id": 101, "chapter_id": "ch1", "next_nodes": [{"id": 102}]},
        {"id": 102, "chapter_id": "ch1", "next_nodes": [{"id": 103}]},
        {"id": 103, "chapter_id": "ch2"}
    ],
    "groups": [
        {"id": "g1", "title": "Act", "color": "#3B82F6",
         "x": 0, "y": 0, "width": 9, "height": 9, "node_ids": [101, 102]}
    ],
    "subgraphs": [
        {"id": "sg1", "title": "Trial",
         "entry_node_id": 101, "exit_node_ids": [102], "node_ids": [101, 102]}
    ],
    "chapters": [
        {"id": "ch1", "title": "Arrivals", "order": 0, "summary": "Meet."},
        {"id": "ch2", "title": "Departures", "order": 1, "summary": "Leave.", "start_node_id": 103}
    ]
})";

void expectRejected(const char* json, const char* caseName) {
    const auto result = Rowl::Core::StoryGraphParser::parse(json);
    if (result.succeeded() ||
        result.errorKind != Rowl::Core::StoryGraphParseErrorKind::Validation) {
        std::cerr << "vNext violation was not a validation error: " << caseName
                  << " (" << result.message << ")" << std::endl;
        exit(1);
    }
}

} // namespace

void test_graph_vnext() {
    using Rowl::Core::StoryGraphParser;
    using Rowl::Core::StoryRuntime;

    TEST_SECTION("Graph vNext Structure Contract");

    const auto valid = StoryGraphParser::parse(kValidV5);
    if (!valid.succeeded()) {
        std::cerr << "Valid v5 graph was rejected: " << valid.message << std::endl;
        exit(1);
    }
    if (valid.document.groups.size() != 1 || valid.document.subgraphs.size() != 1 ||
        valid.document.chapters.size() != 2) {
        std::cerr << "Valid v5 graph lost its structure sections" << std::endl;
        exit(1);
    }
    if (valid.document.nodes.at(101).chapterId != "ch1" ||
        valid.document.nodes.at(103).chapterId != "ch2" ||
        !valid.document.chapters[1].hasStartNodeId ||
        valid.document.chapters[1].startNodeId != 103) {
        std::cerr << "Valid v5 graph lost chapter assignments" << std::endl;
        exit(1);
    }
    StoryRuntime runtime;
    if (!runtime.commit(std::move(valid.document)) ||
        runtime.currentChapterId() != "ch1") {
        std::cerr << "Runtime did not resolve the start node chapter" << std::endl;
        exit(1);
    }
    TEST_PASS("v5 groups/subgraphs/chapters parse and resolve at runtime");

    const auto legacy = StoryGraphParser::parse(
        R"({"format_version": 4, "start_node_id": 7, "nodes": [{"id": 7}]})");
    if (!legacy.succeeded() || !legacy.document.groups.empty() ||
        !legacy.document.subgraphs.empty() || !legacy.document.chapters.empty() ||
        !legacy.document.nodes.at(7).chapterId.empty()) {
        std::cerr << "v4 graph did not default to a single implicit chapter" << std::endl;
        exit(1);
    }
    TEST_PASS("v4 graphs load group-free as one implicit chapter");

    for (const char* sample : {
             "samples/first_light/Assets/json/full_story_graph.json",
             "samples/second_signal/Assets/json/full_story_graph.json"}) {
        std::ifstream input(sample);
        if (!input.is_open()) {
            std::cerr << "Golden sample missing: " << sample << std::endl;
            exit(1);
        }
        const std::string content((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
        const auto golden = StoryGraphParser::parse(content);
        if (!golden.succeeded() || !golden.document.subgraphs.empty() ||
            !golden.document.chapters.empty()) {
            std::cerr << "Golden sample broke vNext back-compat: " << sample
                      << " (" << golden.message << ")" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Golden v4 samples load unchanged under the vNext parser");

    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}],
        "subgraphs": [{"id": "a", "entry_node_id": 1}, {"id": "a", "entry_node_id": 1}]})",
                   "duplicate subgraph id");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}],
        "groups": [{"id": "g", "node_ids": [404]}]})",
                   "group references missing node");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}],
        "subgraphs": [{"id": "s", "entry_node_id": 404, "node_ids": [1]}]})",
                   "subgraph entry references missing node");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}],
        "subgraphs": [{"id": "s", "entry_node_id": 1, "node_ids": []}]})",
                   "subgraph entry is not a member");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}, {"id": 2, "next_nodes": [{"id": 3}]}, {"id": 3}],
        "subgraphs": [{"id": "s", "entry_node_id": 1, "exit_node_ids": [1], "node_ids": [1, 2]}]})",
                   "edge leaves from a non-exit node");
    expectRejected(R"({"start_node_id": 1,
        "nodes": [{"id": 1, "next_nodes": [{"id": 3}]}, {"id": 2, "next_nodes": [{"id": 3}]}, {"id": 3}],
        "subgraphs": [{"id": "s", "entry_node_id": 2, "exit_node_ids": [3], "node_ids": [2, 3]}]})",
                   "edge enters at a non-entry node");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}, {"id": 2}],
        "subgraphs": [{"id": "s", "entry_node_id": 1, "exit_node_ids": [2], "node_ids": [1, 2]}]})",
                   "exit unreachable from entry");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1, "chapter_id": "ghost"}],
        "chapters": [{"id": "c"}]})",
                   "orphan node chapter id");
    expectRejected(R"({"start_node_id": 1,
        "nodes": [{"id": 1, "next_nodes": [{"id": 2}]}, {"id": 2, "next_nodes": [{"id": 1}]}],
        "subgraphs": [{"id": "l", "entry_node_id": 1, "exit_node_ids": [1], "node_ids": [1]},
                      {"id": "r", "entry_node_id": 2, "exit_node_ids": [2], "node_ids": [2]}]})",
                   "subgraph call cycle");
    expectRejected(R"({"start_node_id": 1, "nodes": [{"id": 1}], "groups": "nope"})",
                   "malformed groups section");
    TEST_PASS("vNext violation matrix rejects with validation errors");
}
