#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Rowl::State {

/// D07-1: the unlockable-content catalog of a game (`Assets/json/extras.json`).
///
/// The file declares WHAT can be unlocked (CG gallery, music room,
/// achievements); the player profile (`unlocks`) records WHAT the player has
/// unlocked. Shape (format 1):
///
///   {"format": 1,
///    "gallery":      [{"id","title","image","thumbnail"?}],
///    "music":        [{"id","title","track"}],
///    "achievements": [{"id","title","description"?,"hidden"?,"icon"?}]}
///
/// `title` / `description` are a plain string or a localized object
/// `{"tr": "...", "en": "..."}`. See docs/EXTRAS_UNLOCKS.md.

inline constexpr int kExtrasFormat = 1;
/// Largest accepted extras.json (bytes); a bigger file is ignored as a whole.
inline constexpr std::size_t kExtrasMaxFileBytes = 256 * 1024;
/// Per list; surplus items are skipped with one diagnostic.
inline constexpr std::size_t kExtrasMaxItemsPerList = 256;
/// Kept diagnostics (the log still gets every line's worth of a first batch).
inline constexpr std::size_t kExtrasMaxDiagnostics = 32;

/// A string that is either plain or per-language. Plain text is stored under
/// the empty language tag and answers every language.
struct LocalizedText {
    std::vector<std::pair<std::string, std::string>> entries;  ///< file order, lowercase tags

    bool empty() const { return entries.empty(); }
    /// `lang` ("tr" / "en"): the exact entry, else the first entry in file order.
    const std::string& resolve(std::string_view lang) const;
};

struct ExtrasGalleryItem {
    std::string id;
    LocalizedText title;
    std::string image;      ///< VFS path of the full image
    std::string thumbnail;  ///< optional VFS path; empty = use `image`
};

struct ExtrasMusicItem {
    std::string id;
    LocalizedText title;
    std::string track;      ///< VFS path of the audio file
};

struct ExtrasAchievementItem {
    std::string id;
    LocalizedText title;
    LocalizedText description;
    bool hidden = false;    ///< description (and title) stay veiled until unlocked
    std::string icon;       ///< T2: optional VFS path of the plate icon (shown once unlocked)
};

struct ExtrasCatalog {
    std::vector<ExtrasGalleryItem> gallery;
    std::vector<ExtrasMusicItem> music;
    std::vector<ExtrasAchievementItem> achievements;
    std::vector<std::string> diagnostics;  ///< one line per skipped item / rejected file
    bool fileFound = false;                ///< an extras.json was read (even an unusable one)

    bool empty() const { return gallery.empty() && music.empty() && achievements.empty(); }
    const ExtrasGalleryItem* findGallery(std::string_view id) const;
    const ExtrasMusicItem* findMusic(std::string_view id) const;
    const ExtrasAchievementItem* findAchievement(std::string_view id) const;
};

/// True for `[a-z0-9_.-]{1,64}`.
bool isValidExtrasId(std::string_view id);

/// Parses an extras.json document. Never throws and never rejects the whole
/// file for one bad item: an invalid item (bad id, missing/invalid path,
/// wrong type) or a repeated id (the second one) is skipped with a diagnostic.
/// A file that is too large, is not a JSON object or does not declare
/// `"format": 1` yields an empty catalog and one diagnostic.
ExtrasCatalog parseExtrasCatalog(std::string_view json);

/// Canonical form a media path is compared by: '\\' -> '/', leading "./" and
/// "/" dropped, then one optional "Assets/" and one optional `stripPrefix`
/// ("images/" or "audio/") removed, ASCII-lowercased. It mirrors the prefixes
/// the texture / audio resolvers probe, so "bogaz.jpg", "images/bogaz.jpg" and
/// "Assets/images/bogaz.jpg" are the same asset. Case-insensitive on purpose
/// (Windows / macOS resolve case-insensitively).
std::string canonicalMediaPath(std::string_view path, std::string_view stripPrefix);

}  // namespace Rowl::State
