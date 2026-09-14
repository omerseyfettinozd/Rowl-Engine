#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Rowl::Core {

// Forward declarations to avoid pulling the full document header here.
struct StoryGraphDocument;

/// Graph vNext (format v5) structure contract.
///
/// - GraphGroup is editor-canvas-only metadata (title, color, rect, members).
///   It carries no runtime semantics and never affects loading or playback.
/// - GraphSubgraph is a modular sub-flow contract: flow may enter the member
///   set only through the entry node and leave it only through an exit node.
/// - GraphChapter is a runtime section marker: save/load boundaries, backlog
///   clustering and player-profile progress resolve chapters through the
///   chapter id assigned to each node (StoryNode::chapterId).
///
/// Format v4 documents predate this contract: they carry no groups,
/// subgraphs or chapters and no per-node chapter ids. Such documents load as
/// a single implicit chapter with every node unassigned.
struct GraphGroup {
    std::string id;
    std::string title;
    std::string color;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::vector<uint64_t> nodeIds;
};

struct GraphSubgraph {
    std::string id;
    std::string title;
    uint64_t entryNodeId = 0;
    std::vector<uint64_t> exitNodeIds;
    std::vector<uint64_t> nodeIds;
};

struct GraphChapter {
    std::string id;
    std::string title;
    int order = 0;
    std::string summary;
    uint64_t startNodeId = 0;
    bool hasStartNodeId = false;
};

inline constexpr std::size_t kMaxGraphGroups = 4'096;
inline constexpr std::size_t kMaxGraphSubgraphs = 1'024;
inline constexpr std::size_t kMaxGraphChapters = 1'024;
inline constexpr std::size_t kMaxGraphMembersPerEntry = 10'000;
inline constexpr std::size_t kMaxGraphPortsPerSubgraph = 1'024;
inline constexpr std::size_t kMaxGraphIdChars = 128;
inline constexpr std::size_t kMaxGraphTitleChars = 512;
inline constexpr std::size_t kMaxGraphSummaryChars = 4'096;

/// Parses the optional v5 "groups" / "subgraphs" / "chapters" arrays into an
/// already node-validated document. Missing arrays mean "no structure" (the
/// v4 case) and succeed. Returns false with a human-readable error otherwise.
bool parseGraphStructure(const nlohmann::json& data, StoryGraphDocument& document,
                         std::string& error);

/// Validates the vNext structural contract against the parsed document:
/// duplicate ids, dangling node references, subgraph entry/exit membership,
/// disjoint subgraph membership, entry/exit port discipline, entry-to-exit
/// reachability, orphan node chapter ids and subgraph call-graph cycles.
/// Returns false with a human-readable error on the first violation.
bool validateGraphStructure(const StoryGraphDocument& document, std::string& error);

} // namespace Rowl::Core
