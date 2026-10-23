#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace Rowl::State {

/// D01c-1: native player profile v1 (preferences + read set).
///
/// One file per game: `<profileDir>/player-profile.json` where the profile
/// directory is Engine::getProfileDirectoryPath() (per-game once a project
/// with an authored id is mounted). The JSON keys are the SAME as the managed
/// `PlayerProfileStore` (editor/Services/PlayerProfileStore.cs) writes, plus
/// `game_id` and a reserved `unlocks` array, so both sides read one file.
/// See docs/DATA_FORMATS_AND_MIGRATION.md ("Player profile v1").

inline constexpr const char* kPlayerProfileFileName = "player-profile.json";
inline constexpr int kPlayerProfileVersion = 1;
/// Upper bound on the read set. Existing ids are never dropped; once the set
/// holds this many ids, NEW ids are refused with a diagnostic.
inline constexpr std::size_t kPlayerProfileMaxReadIds = 1'000'000;
/// A profile file larger than this is treated as corrupt (it would be 6x the
/// size of a full read set); keeps the loader memory-bounded.
inline constexpr std::uintmax_t kPlayerProfileMaxFileBytes = 256ull * 1024 * 1024;
/// Quarantined `player-profile.corrupt-*.json` files kept next to the profile.
inline constexpr std::size_t kPlayerProfileMaxQuarantine = 3;
/// Automatic flush debounce (seconds of engine time after the first change).
inline constexpr float kPlayerProfileFlushDelaySeconds = 2.0f;

enum class PlayerProfileStatus {
    Unmounted,          ///< no project mounted yet; nothing loaded, nothing writable
    Loaded,             ///< file parsed (fields sanitized)
    Missing,            ///< no file yet: defaults (first run, not an error)
    Corrupt,            ///< unreadable/invalid file: defaults (quarantined when possible)
    UnsupportedVersion, ///< file written by a newer schema: defaults, store read-only
    WrongGame,          ///< file's game_id differs from the mounted game: defaults, read-only
};

const char* playerProfileStatusName(PlayerProfileStatus status);

/// Sanitized preference values (defaults match the managed PlayerProfile).
struct PlayerPreferences {
    std::string language = "en";            ///< "en" | "tr"
    float masterVolume = 1.0f;              ///< [0,1]
    float bgmVolume = 1.0f;
    float voiceVolume = 1.0f;
    float sfxVolume = 1.0f;
    float ambienceVolume = 1.0f;
    float uiVolume = 1.0f;
    float textSpeedMultiplier = 1.0f;       ///< [0.25, 4]
    float autoAdvanceDelay = 2.0f;          ///< [0, 60] seconds
    float textScale = 1.0f;                 ///< snapped to 1 / 1.25 / 1.5
    bool highContrast = false;
    bool reducedMotion = false;
    std::string skipMode = "read_only";     ///< "off" | "read_only" | "all"
    bool autoEnabled = false;
    std::string mixerFadeCurve = "Linear";  ///< "Linear" | "EqualPower"
    int sfxPoolDepth = 8;                   ///< [1, 16]
};

/// Canonical lowercase 8-4-4-4-12 form of a UUID, or nullopt. Port of the
/// managed ContentIdService.Normalize for the D/N/B/P Guid.TryParse formats
/// (the rarely used "X" hex-struct format is not accepted). Whitespace around
/// the value is trimmed; empty/whitespace is invalid.
std::optional<std::string> normalizeContentId(std::string_view value);

enum class ProfileMutation {
    Ok,
    Unchanged,  ///< valid, but nothing changed (id already read / same values)
    Invalid,    ///< malformed id / unknown key / wrong type; nothing was changed
    ReadOnly,   ///< store is read-only (future version, wrong game, unreadable file)
    Unmounted,  ///< no project mounted
    Full,       ///< read-set limit reached; the new id was refused
};

/// Owner-thread object (the engine's thread): no internal locking.
class PlayerProfileStore {
public:
    PlayerProfileStore();
    ~PlayerProfileStore();  ///< best-effort flush of pending changes
    PlayerProfileStore(const PlayerProfileStore&) = delete;
    PlayerProfileStore& operator=(const PlayerProfileStore&) = delete;

    /// Flushes pending changes of the previous profile, then loads
    /// `<profileDir>/player-profile.json`. `gameId` non-empty = the directory
    /// is per game: a `game_id` in the file that differs yields WrongGame and
    /// writes stamp it. Empty = shared/legacy directory: no enforcement.
    /// Never throws; the outcome is status()/diagnostic().
    void open(const std::filesystem::path& profileDir, const std::string& gameId);

    /// Flushes and returns to Unmounted.
    void close();

    /// Writes pending changes atomically. True when nothing was pending, the
    /// store is read-only, or the write succeeded; false on I/O failure (the
    /// change stays pending and `*errorOut` explains).
    bool flush(std::string* errorOut = nullptr);

    /// Debounced autosave driven by engine frames: flushes once the profile
    /// has been dirty for kPlayerProfileFlushDelaySeconds of accumulated dt.
    /// After a failed write the next automatic attempt waits 30 s.
    void tick(float deltaSeconds);

    PlayerProfileStatus status() const { return m_status; }
    const std::string& diagnostic() const { return m_diagnostic; }
    bool readOnly() const { return m_readOnly; }
    bool dirty() const { return m_dirty; }
    const std::filesystem::path& path() const { return m_path; }
    const PlayerPreferences& preferences() const { return m_preferences; }
    std::size_t readCount() const { return m_readIds.size(); }

    ProfileMutation markRead(std::string_view contentId);
    bool isRead(std::string_view contentId) const;

    /// Merges known preference keys from a JSON object (values are clamped /
    /// snapped like the managed sanitizer). All-or-nothing: an unknown key, a
    /// wrong JSON type or a malformed document changes nothing and yields
    /// Invalid with `*errorOut` naming the problem.
    ProfileMutation setPreferencesJson(std::string_view json, std::string* errorOut = nullptr);

    /// Full current profile (the exact document flush() would write).
    std::string toJson() const;
    /// {status, diagnostic, read_only, path, read_count, game_id, dirty}
    std::string statusJson() const;

    /// Test seam: lowers the read-set limit (default kPlayerProfileMaxReadIds).
    void setReadLimitForTesting(std::size_t limit) { m_readLimit = limit; }

private:
    void resetToDefaults();
    void loadFromDisk();
    void quarantineCorruptFile();
    void markDirty();
    std::string serialize() const;

    std::filesystem::path m_dir;
    std::filesystem::path m_path;
    std::string m_gameId;       ///< non-empty: enforce + stamp
    std::string m_fileGameId;   ///< game_id to write (== m_gameId when enforcing)
    PlayerProfileStatus m_status = PlayerProfileStatus::Unmounted;
    std::string m_diagnostic;
    bool m_readOnly = true;
    bool m_dirty = false;
    float m_dirtyAge = 0.0f;
    float m_retryDelay = 0.0f;  ///< extra wait added after a failed autosave
    PlayerPreferences m_preferences;
    std::unordered_set<std::string> m_readIds;
    std::size_t m_readLimit = kPlayerProfileMaxReadIds;
    struct Extras;               ///< unknown top-level keys + `unlocks` (preserved opaquely)
    std::unique_ptr<Extras> m_extras;
};

} // namespace Rowl::State
