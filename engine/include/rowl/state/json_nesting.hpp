#pragma once

#include <cstddef>
#include <string_view>

namespace Rowl::State {

/// True when no bracket nest in `text` runs deeper than `maxDepth`.
/// nlohmann::json::parse recurses once per nesting level and has no depth limit
/// of its own, so a hostile document of megabytes of "[[[..." (or {"a":{"a":…)
/// overflows the thread stack with an uncatchable SIGSEGV before any size or
/// field budget can answer. Linear, string-aware pre-scan: quotes and backslash
/// escapes are skipped so brackets inside text never count. Shared by the save,
/// profile and theme loaders; each passes its own depth budget.
inline bool jsonNestingWithinBudget(std::string_view text, std::size_t maxDepth) {
    std::size_t depth = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\') {
                ++i;  // skip the escaped byte, whatever it is
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > maxDepth) return false;
        } else if (c == ']' || c == '}') {
            if (depth > 0) --depth;
        }
    }
    return true;
}

} // namespace Rowl::State
