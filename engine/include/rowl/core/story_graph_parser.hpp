#pragma once

#include "rowl/core/story_graph.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace Rowl::Core {

enum class StoryGraphParseErrorKind {
    None,
    Parse,
    Validation
};

struct StoryGraphParseResult {
    StoryGraphDocument document;
    StoryGraphParseErrorKind errorKind = StoryGraphParseErrorKind::None;
    std::string message;

    bool succeeded() const { return errorKind == StoryGraphParseErrorKind::None; }

    static StoryGraphParseResult failure(StoryGraphParseErrorKind kind, std::string error) {
        StoryGraphParseResult result;
        result.errorKind = kind;
        result.message = std::move(error);
        return result;
    }
};

/// Parses and validates a story graph without mutating a live Engine.
class StoryGraphParser {
public:
    static StoryGraphParseResult parse(std::string_view jsonContent);
};

} // namespace Rowl::Core
