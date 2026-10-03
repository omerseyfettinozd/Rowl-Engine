#pragma once

#include <string_view>
#include <unordered_set>

namespace Rowl::Scripting {

inline bool isReservedVariableName(std::string_view key) {
    static const std::unordered_set<std::string_view> kReserved = {
        "rowl", "_G", "_ENV", "_VERSION",
        "math", "string", "table", "coroutine", "utf8",
        "package", "io", "os", "debug",
        "dofile", "loadfile", "load", "collectgarbage", "require", "module",
        "print", "warn", "assert", "error", "pcall", "xpcall", "select", "type",
        "pairs", "ipairs", "next", "tostring", "tonumber", "setmetatable",
        "getmetatable", "rawget", "rawset", "rawequal", "rawlen"
    };
    return key.empty() || kReserved.find(key) != kReserved.end();
}

} // namespace Rowl::Scripting
