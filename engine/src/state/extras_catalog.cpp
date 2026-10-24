/**
 * extras_catalog.cpp — D07-1 parser of `Assets/json/extras.json` (CG gallery,
 * music room, achievements). Pure data: no VFS, no profile, no engine; the
 * engine reads the file and hands the bytes to parseExtrasCatalog.
 */
#include "rowl/state/extras_catalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>

namespace Rowl::State {

namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxTextBytes = 512;
constexpr std::size_t kMaxPathBytes = 260;

const std::string kEmpty;

char lowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = lowerAscii(c);
    return out;
}

void addDiagnostic(ExtrasCatalog& catalog, std::string message) {
    if (catalog.diagnostics.size() < kExtrasMaxDiagnostics) {
        catalog.diagnostics.push_back(std::move(message));
    } else if (catalog.diagnostics.size() == kExtrasMaxDiagnostics) {
        catalog.diagnostics.push_back("further extras.json diagnostics omitted");
    }
}

/// Plain string or {"lang": "text"} object -> LocalizedText. False = wrong shape.
bool readText(const Json& value, LocalizedText& out) {
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.size() > kMaxTextBytes) return false;
        if (!text.empty()) out.entries.emplace_back(std::string(), text);
        return true;
    }
    if (!value.is_object()) return false;
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!it.value().is_string()) return false;
        const std::string text = it.value().get<std::string>();
        if (text.size() > kMaxTextBytes || it.key().size() > 16) return false;
        if (!text.empty()) out.entries.emplace_back(lowered(it.key()), text);
    }
    return true;
}

/// A VFS-relative media path: non-empty, bounded, no control bytes, not
/// absolute, no ".." segment.
bool isSafeMediaPath(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathBytes) return false;
    for (const char c : path) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) return false;
    }
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;  // drive letter
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        std::size_t end = normalized.find('/', begin);
        if (end == std::string::npos) end = normalized.size();
        if (normalized.compare(begin, end - begin, "..") == 0) return false;
        begin = end + 1;
    }
    return true;
}

std::string describe(const char* list, std::size_t index, const Json& item) {
    std::string label = std::string(list) + "[" + std::to_string(index) + "]";
    if (item.is_object()) {
        const auto it = item.find("id");
        if (it != item.end() && it->is_string()) {
            std::string id = it->get<std::string>();
            if (id.size() > 32) id.resize(32);
            label += " '" + id + "'";
        }
    }
    return label;
}

/// Shared walk over one list: type/limit handling, id validation and
/// duplicate detection; `build` fills the list-specific fields and returns an
/// empty string on success, else the reason the item is skipped.
template <typename Item, typename Build>
void readList(ExtrasCatalog& catalog, const Json& root, const char* key, std::vector<Item>& out,
              Build&& build) {
    const auto listIt = root.find(key);
    if (listIt == root.end()) return;
    if (!listIt->is_array()) {
        addDiagnostic(catalog, std::string("extras.json '") + key + "' is not an array; ignored");
        return;
    }
    std::unordered_set<std::string> seen;
    std::size_t index = 0;
    bool limitReported = false;
    for (const Json& item : *listIt) {
        const std::size_t position = index++;
        if (out.size() >= kExtrasMaxItemsPerList) {
            if (!limitReported) {
                addDiagnostic(catalog, std::string("extras.json '") + key + "' holds more than " +
                                           std::to_string(kExtrasMaxItemsPerList) +
                                           " items; the rest are skipped");
                limitReported = true;
            }
            continue;
        }
        const std::string label = describe(key, position, item);
        if (!item.is_object()) {
            addDiagnostic(catalog, "extras.json " + label + " is not an object; skipped");
            continue;
        }
        const auto idIt = item.find("id");
        if (idIt == item.end() || !idIt->is_string() || !isValidExtrasId(idIt->get<std::string>())) {
            addDiagnostic(catalog, "extras.json " + label +
                                       " has a missing or invalid id (use [a-z0-9_.-], 1-64); skipped");
            continue;
        }
        const std::string id = idIt->get<std::string>();
        if (!seen.insert(id).second) {
            addDiagnostic(catalog, "extras.json " + label + " repeats an earlier id; skipped");
            continue;
        }
        Item entry;
        entry.id = id;
        const auto titleIt = item.find("title");
        if (titleIt != item.end() && !readText(*titleIt, entry.title)) {
            addDiagnostic(catalog, "extras.json " + label + " has an invalid title; skipped");
            continue;
        }
        const std::string reason = build(item, entry);
        if (!reason.empty()) {
            addDiagnostic(catalog, "extras.json " + label + " " + reason + "; skipped");
            continue;
        }
        out.push_back(std::move(entry));
    }
}

/// Required string path field of an item; returns the failure reason or "".
std::string readPath(const Json& item, const char* field, bool required, std::string& out) {
    const auto it = item.find(field);
    if (it == item.end()) {
        return required ? std::string("is missing '") + field + "'" : std::string();
    }
    if (!it->is_string()) return std::string("has a non-string '") + field + "'";
    out = it->get<std::string>();
    if (!isSafeMediaPath(out)) return std::string("has an unusable '") + field + "' path";
    return {};
}

}  // namespace

const std::string& LocalizedText::resolve(std::string_view lang) const {
    for (const auto& [tag, text] : entries) {
        if (tag.empty() || tag == lang) return text;
    }
    return entries.empty() ? kEmpty : entries.front().second;
}

const ExtrasGalleryItem* ExtrasCatalog::findGallery(std::string_view id) const {
    for (const auto& item : gallery) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

const ExtrasMusicItem* ExtrasCatalog::findMusic(std::string_view id) const {
    for (const auto& item : music) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

const ExtrasAchievementItem* ExtrasCatalog::findAchievement(std::string_view id) const {
    for (const auto& item : achievements) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool isValidExtrasId(std::string_view id) {
    if (id.empty() || id.size() > kMaxIdLength) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' ||
                        c == '-';
        if (!ok) return false;
    }
    return true;
}

std::string canonicalMediaPath(std::string_view path, std::string_view stripPrefix) {
    std::string text(path);
    std::replace(text.begin(), text.end(), '\\', '/');
    for (char& c : text) c = lowerAscii(c);
    std::string_view view = text;
    while (view.size() >= 2 && view.substr(0, 2) == "./") view.remove_prefix(2);
    while (!view.empty() && view.front() == '/') view.remove_prefix(1);
    const auto strip = [&view](std::string_view prefix) {
        if (!prefix.empty() && view.size() > prefix.size() && view.substr(0, prefix.size()) == prefix) {
            view.remove_prefix(prefix.size());
        }
    };
    strip("assets/");
    std::string loweredPrefix = lowered(stripPrefix);
    strip(loweredPrefix);
    return std::string(view);
}

ExtrasCatalog parseExtrasCatalog(std::string_view json) {
    ExtrasCatalog catalog;
    catalog.fileFound = true;
    if (json.size() > kExtrasMaxFileBytes) {
        addDiagnostic(catalog, "extras.json is larger than " + std::to_string(kExtrasMaxFileBytes) +
                                   " bytes; ignored");
        return catalog;
    }
    const Json root = Json::parse(json.begin(), json.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        addDiagnostic(catalog, "extras.json is not a valid JSON object; ignored");
        return catalog;
    }
    const auto formatIt = root.find("format");
    if (formatIt == root.end() || !formatIt->is_number_integer() ||
        formatIt->get<long long>() != kExtrasFormat) {
        addDiagnostic(catalog, "extras.json must declare \"format\": " + std::to_string(kExtrasFormat) +
                                   "; ignored");
        return catalog;
    }

    readList(catalog, root, "gallery", catalog.gallery,
             [](const Json& item, ExtrasGalleryItem& entry) {
                 std::string reason = readPath(item, "image", true, entry.image);
                 if (!reason.empty()) return reason;
                 return readPath(item, "thumbnail", false, entry.thumbnail);
             });
    readList(catalog, root, "music", catalog.music, [](const Json& item, ExtrasMusicItem& entry) {
        return readPath(item, "track", true, entry.track);
    });
    readList(catalog, root, "achievements", catalog.achievements,
             [](const Json& item, ExtrasAchievementItem& entry) {
                 const auto descIt = item.find("description");
                 if (descIt != item.end() && !readText(*descIt, entry.description)) {
                     return std::string("has an invalid description");
                 }
                 const auto hiddenIt = item.find("hidden");
                 if (hiddenIt != item.end()) {
                     if (!hiddenIt->is_boolean()) return std::string("has a non-boolean 'hidden'");
                     entry.hidden = hiddenIt->get<bool>();
                 }
                 return std::string();
             });
    return catalog;
}

}  // namespace Rowl::State
