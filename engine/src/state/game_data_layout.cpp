#include "rowl/state/game_data_layout.hpp"

#include "rowl/platform/user_data_directories.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#define ROWL_GETPID _getpid
#else
#include <unistd.h>
#define ROWL_GETPID getpid
#endif

namespace Rowl::State {
namespace {

namespace fs = std::filesystem;
constexpr const char* kMarkerName = "migrated.json";

std::string utcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string markerJson(const LegacySaveMigration& migration) {
    nlohmann::json doc;
    doc["format"] = 1;
    doc["source"] = migration.source;
    doc["time"] = utcNow();
    doc["copied"] = migration.copied;
    doc["skipped"] = migration.skipped;
    doc["errors"] = migration.errors;
    return doc.dump(2) + "\n";
}

std::string tempSuffix() {
    static std::atomic<unsigned> seq{0};
    return ".tmp." + std::to_string(ROWL_GETPID()) + "." + std::to_string(seq.fetch_add(1));
}

bool writeFileAtomic(const fs::path& path, const std::string& text) {
    const fs::path temp = path.string() + tempSuffix();
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        out.flush();
        if (!out) {
            std::error_code ec;
            fs::remove(temp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

} // namespace

bool isValidGameDataId(std::string_view id) {
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool makeGameDataDirectories(const fs::path& userDataRoot, std::string_view gameId,
                             GameDataDirectories& out) {
    if (!isValidGameDataId(gameId) || userDataRoot.empty()) return false;
    out.gameDir = userDataRoot.lexically_normal() / "games" / std::string(gameId);
    out.saves = out.gameDir / "saves";
    out.profile = out.gameDir / "profile";
    return true;
}

bool probeDirectoryWritable(const fs::path& dir) {
    std::error_code ec;
    fs::path anchor = dir;
    while (!anchor.empty()) {
        if (fs::exists(anchor, ec) && !ec) break;
        if (ec) return false;
        anchor = anchor.parent_path();
    }
    if (anchor.empty() || !fs::is_directory(anchor, ec) || ec) return false;
    static std::atomic<unsigned> probeSeq{0};
    const auto probeName = ".rowl_write_probe_" + std::to_string(probeSeq.fetch_add(1)) + ".tmp";
    if (!fs::create_directory(anchor / probeName, ec) || ec) return false;
    fs::remove(anchor / probeName, ec);
    if (fs::exists(dir, ec) && !ec) {
        if (!fs::is_directory(dir, ec) || ec) return false;
        const auto inner = dir / probeName;
        if (!fs::create_directory(inner, ec) || ec) return false;
        fs::remove(inner, ec);
    }
    return true;
}

LegacySaveMigration migrateLegacySaves(const fs::path& legacySaves,
                                       const GameDataDirectories& target) {
    LegacySaveMigration result;
    std::error_code ec;
    if (legacySaves.empty() || !fs::is_directory(legacySaves, ec) || ec) return result;
    ec.clear();
    if (fs::exists(target.saves, ec) && fs::equivalent(legacySaves, target.saves, ec)) return result;
    ec.clear();

    const fs::path marker = target.gameDir / kMarkerName;
    if (fs::is_regular_file(marker, ec) && !ec) {
        try {
            std::ifstream in(marker, std::ios::binary);
            const auto doc = nlohmann::json::parse(in);
            if (doc.value("source", std::string()) == Rowl::Platform::pathToUtf8(legacySaves) &&
                doc.contains("errors") && doc["errors"].is_array() && doc["errors"].empty()) {
                result.alreadyDone = true;
                result.source = doc.value("source", std::string());
                return result;
            }
        } catch (...) { /* unreadable marker: redo the (idempotent) pass */ }
    }
    ec.clear();

    std::vector<fs::path> candidates;
    for (fs::directory_iterator it(legacySaves, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryEc;
        if (it->is_regular_file(entryEc) && !entryEc && it->path().extension() == ".json") {
            candidates.push_back(it->path());
        }
    }
    if (candidates.empty() && !ec) return result;

    result.attempted = true;
    result.source = Rowl::Platform::pathToUtf8(legacySaves);
    if (ec) result.errors.push_back("cannot list legacy saves: " + ec.message());
    ec.clear();

    if (!candidates.empty()) {
        fs::create_directories(target.saves, ec);
        if (ec) {
            result.errors.push_back("cannot create " + Rowl::Platform::pathToUtf8(target.saves) +
                                    ": " + ec.message());
            candidates.clear();
        }
    }
    for (const auto& file : candidates) {
        const std::string name = Rowl::Platform::pathToUtf8(file.filename());
        const fs::path dest = target.saves / file.filename();
        std::error_code e;
        const bool present = fs::exists(dest, e);
        if (e) {
            result.errors.push_back(name + ": " + e.message());
            continue;
        }
        if (present) {
            result.skipped.push_back(name);
            continue;
        }
        const fs::path temp = dest.string() + tempSuffix();
        fs::copy_file(file, temp, fs::copy_options::none, e);
        if (!e) fs::rename(temp, dest, e);
        if (e) {
            std::error_code rm;
            fs::remove(temp, rm);
            result.errors.push_back(name + ": " + e.message());
            continue;
        }
        result.copied.push_back(name);
    }

    fs::create_directories(target.gameDir, ec);
    if (!writeFileAtomic(marker, markerJson(result))) {
        result.errors.push_back(std::string("cannot write ") + kMarkerName);
    }
    return result;
}

GameDataLayout prepareGameData(const fs::path& userDataRoot,
                               const Rowl::Platform::GameIdentity& identity,
                               const fs::path& projectRoot) {
    using Rowl::Platform::GameIdentitySource;
    GameDataLayout layout;
    const fs::path legacy = projectRoot / "saves";
    layout.dataDirectory = projectRoot;
    layout.saveDirectory = legacy;
    // D01c: the profile follows the saves, so two project-local games never
    // share the global profiles/ dir.
    layout.profileDirectory = projectRoot / "profile";

    if (identity.source == GameIdentitySource::Derived) {
        layout.diagnostic = "game id is path-derived (no authored id): saves stay in the project folder";
        return layout;
    }
    GameDataDirectories dirs;
    if (!makeGameDataDirectories(userDataRoot, identity.id, dirs)) {
        layout.diagnostic = "invalid game id for a data directory: using project-local saves";
        return layout;
    }
    if (!probeDirectoryWritable(dirs.saves) || !probeDirectoryWritable(dirs.profile)) {
        layout.diagnostic = "user data directory is not writable (" +
                            Rowl::Platform::pathToUtf8(dirs.gameDir) +
                            "): falling back to project-local saves";
        return layout;
    }
    layout.perGame = true;
    layout.dataDirectory = dirs.gameDir;
    layout.saveDirectory = dirs.saves;
    layout.profileDirectory = dirs.profile;

    const auto migration = migrateLegacySaves(legacy, dirs);
    if (migration.attempted) {
        layout.migrationJson = markerJson(migration);
        if (!migration.complete()) {
            layout.diagnostic = "legacy save migration incomplete (" +
                                std::to_string(migration.errors.size()) +
                                " error(s)); originals were left untouched, see migrated.json";
        }
    }
    return layout;
}

} // namespace Rowl::State
