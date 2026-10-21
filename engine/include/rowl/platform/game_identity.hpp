#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Platform {

/// D01a: where the resolved game id came from. Values are part of the C ABI
/// (RowlEngine_GetGameIdentitySource) — never renumber.
enum class GameIdentitySource : int32_t {
    None = 0,      ///< no project mounted yet
    Package = 1,   ///< rowl/game.json inside a mounted .rowlpkg
    Manifest = 2,  ///< loose project.rowlproj project_uuid
    Derived = 3    ///< UUIDv5 over the canonical project path (flagged, never shared)
};

struct GameIdentity {
    std::string id;          ///< canonical lowercase UUID; empty when source == None
    std::string name;        ///< optional human name (package/manifest), may be empty
    GameIdentitySource source = GameIdentitySource::None;
    /// Non-empty when a carrier was present but unusable (malformed UUID,
    /// unreadable JSON, unsupported format). Resolution still falls through.
    std::string diagnostic;
};

/// Returns the lowercase canonical form of a UUID text, or nullopt when it is
/// not a well-formed 8-4-4-4-12 hex UUID.
std::optional<std::string> normalizeGameId(std::string_view text);

/// Deterministic UUIDv5 (fixed Rowl namespace) over the canonical absolute path.
std::string deriveGameIdFromPath(const std::filesystem::path& projectRoot);

/// Resolves the identity of the project at `projectRoot`.
/// Order: rowl/game.json from a mounted .rowlpkg (loose mods/Assets files are
/// deliberately NOT consulted, so a mod cannot spoof the id) -> loose
/// project.rowlproj `project_uuid` -> derived id. `vfs` may be null.
GameIdentity resolveGameIdentity(Rowl::VFS::VFSManager* vfs,
                                 const std::filesystem::path& projectRoot);

} // namespace Rowl::Platform
