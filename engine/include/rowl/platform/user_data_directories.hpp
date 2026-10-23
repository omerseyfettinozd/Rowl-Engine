#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Rowl::Platform {

struct UserDataDirectories {
    std::filesystem::path saves;
    std::filesystem::path profiles;
};

/// Lossless conversion helpers for the public UTF-8 C ABI and native
/// std::filesystem::path representation (wide on Windows).
std::filesystem::path pathFromUtf8(std::string_view value);
std::string pathToUtf8(const std::filesystem::path& value);

/// Builds the stable save/profile layout below an already selected per-user
/// data root. Kept separate so Unicode path handling can be tested without
/// mutating the process environment.
UserDataDirectories makeUserDataDirectories(
    const std::filesystem::path& userDataRoot);

/// Resolves the current user's writable data root using native platform
/// conventions. Linux follows XDG_DATA_HOME (then the passwd/HOME directory);
/// Windows uses the Unicode Known Folder API for LocalAppData.
UserDataDirectories resolveUserDataDirectories();

/// D01b: the per-user data root itself (e.g. $XDG_DATA_HOME/rowl-engine);
/// per-game data lives below <root>/games/<game-id>.
std::filesystem::path resolveUserDataRoot();

} // namespace Rowl::Platform
