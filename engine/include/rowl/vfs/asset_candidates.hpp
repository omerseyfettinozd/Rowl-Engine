#pragma once

// F2 (B-8): ONE place that decides where an image named in the story graph may live.
//
// A graph names "bogaz_gunduz.jpg"; the file sits at images/bogaz_gunduz.jpg (or under
// Assets/). The renderer tried that list, but the prefetch only asked the VFS for the bare
// name - it never warmed a single picture and reported them all missing - and every probe
// of a name that is not there made the VFS print a "File not found" warning (two per
// picture in the renderer). Both now go through this list and probe with exists() first.

#include "rowl/vfs/vfs.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Rowl::VFS {

/// Slash-normalised spelling of an asset path (backslashes become '/').
inline std::string normalizeAssetPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

/// The VFS paths an image reference may resolve to, in the order they are tried.
inline std::vector<std::string> imageAssetCandidates(const std::string& normalizedPath) {
    const size_t lastSlash = normalizedPath.find_last_of('/');
    const std::string bareName =
        lastSlash == std::string::npos ? normalizedPath : normalizedPath.substr(lastSlash + 1);
    return {
        normalizedPath,
        bareName,
        "images/" + bareName,
        "images/" + normalizedPath,
        "Assets/images/" + bareName,
        "Assets/images/" + normalizedPath,
        "Assets/" + bareName,
        "Assets/" + normalizedPath,
    };
}

/// First candidate the VFS has (empty when none). Probes with exists(), so a name that is
/// not there costs no warning.
inline std::string resolveImageAsset(VFSManager& vfs, const std::string& path) {
    for (const std::string& candidate : imageAssetCandidates(normalizeAssetPath(path))) {
        if (vfs.exists(candidate)) return candidate;
    }
    return std::string();
}

}  // namespace Rowl::VFS
