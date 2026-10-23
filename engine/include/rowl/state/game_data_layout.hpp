#pragma once

#include "rowl/platform/game_identity.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Rowl::State {

/// D01b: per-game data layout `<userDataRoot>/games/<game-id>/{saves,profile}`.
struct GameDataDirectories {
    std::filesystem::path gameDir;
    std::filesystem::path saves;
    std::filesystem::path profile;
};

/// Result of one legacy `<projectRoot>/saves/*.json` migration attempt.
struct LegacySaveMigration {
    bool attempted = false;      ///< legacy files were found and a pass ran
    bool alreadyDone = false;    ///< a complete marker existed; nothing was done
    std::string source;          ///< legacy directory (UTF-8)
    std::vector<std::string> copied;
    std::vector<std::string> skipped;  ///< target already had the file (per-game wins)
    std::vector<std::string> errors;
    bool complete() const { return errors.empty(); }
};

/// What SetProjectDirectory ends up using for save/profile data.
struct GameDataLayout {
    std::filesystem::path dataDirectory;     ///< dir holding saves/ (+ profile/)
    std::filesystem::path saveDirectory;     ///< effective save dir
    std::filesystem::path profileDirectory;  ///< per-game profile/, else <project>/profile beside saves
    bool perGame = false;                    ///< false: legacy project-local saves
    std::string diagnostic;                  ///< why we did not use the per-game dir / notes
    std::string migrationJson;               ///< marker content of the last migration pass ("" if none)
};

/// True for a canonical lowercase 8-4-4-4-12 UUID — the only form allowed as a
/// path component (rejects traversal, separators, empty, uppercase).
bool isValidGameDataId(std::string_view gameId);

/// `<userDataRoot>/games/<gameId>/{saves,profile}`; false for an invalid id.
bool makeGameDataDirectories(const std::filesystem::path& userDataRoot,
                             std::string_view gameId, GameDataDirectories& out);

/// Non-creating writability probe: nearest existing ancestor (and `dir` itself
/// when present) must accept a temp entry. Never throws.
bool probeDirectoryWritable(const std::filesystem::path& dir);

/// Copies `<legacySaves>/*.json` into `target.saves` (never deletes/moves, never
/// overwrites an existing target file, temp+rename per file) and records
/// `<gameDir>/migrated.json`. A marker without errors makes later calls no-ops;
/// one with errors is retried (idempotent).
LegacySaveMigration migrateLegacySaves(const std::filesystem::path& legacySaves,
                                       const GameDataDirectories& target);

/// Decides the data layout for a mounted project and runs the migration.
/// Derived ids (no authored id) keep project-local saves: a path-derived id
/// would strand saves when the folder moves and must never be shared silently.
GameDataLayout prepareGameData(const std::filesystem::path& userDataRoot,
                               const Rowl::Platform::GameIdentity& identity,
                               const std::filesystem::path& projectRoot);

} // namespace Rowl::State
