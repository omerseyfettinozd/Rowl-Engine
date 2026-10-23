/**
 * player_profile.cpp — D01c-1 native player profile v1 (preferences + read set).
 *
 * Mirrors the managed PlayerProfile/PlayerProfileStore clamp, default and
 * normalization rules (same JSON keys) and adds: game_id binding, a reserved
 * `unlocks` array, opaque round-trip of unknown top-level keys, read-only
 * handling of future versions, quarantine of corrupt files, a bounded read set
 * and debounced atomic writes.
 */
#include "rowl/state/player_profile.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/state/save_durability.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <vector>

namespace Rowl::State {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

namespace {

constexpr float kAutosaveRetryDelaySeconds = 30.0f;

const char* const kKnownKeys[] = {
    "version", "game_id", "language", "read_content_ids", "master_volume", "bgm_volume",
    "voice_volume", "sfx_volume", "ambience_volume", "ui_volume", "text_speed_multiplier",
    "auto_advance_delay", "text_scale", "high_contrast", "reduced_motion", "skip_mode",
    "auto_enabled", "mixer_fade_curve", "sfx_pool_depth", "unlocks"};

bool isKnownKey(const std::string& key) {
    for (const char* known : kKnownKeys) {
        if (key == known) return true;
    }
    return false;
}

std::string trimmedLower(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    std::string out(text.substr(begin, end - begin));
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

float clampFloat(float value, float low, float high) {
    return std::min(std::max(value, low), high);
}

float snapTextScale(float value) {
    static constexpr float kSteps[] = {1.0f, 1.25f, 1.5f};
    if (!std::isfinite(value)) return 1.0f;
    float best = kSteps[0];
    float bestDistance = std::fabs(value - best);
    for (float step : kSteps) {
        const float distance = std::fabs(value - step);
        if (distance < bestDistance) {
            best = step;
            bestDistance = distance;
        }
    }
    return best;
}

std::string normalizeLanguage(std::string_view language) {
    std::string candidate = trimmedLower(language);
    const size_t dash = candidate.find_first_of("-_");
    if (dash != std::string::npos && dash > 0) candidate.resize(dash);
    return (candidate == "en" || candidate == "tr") ? candidate : "en";
}

std::string normalizeFadeCurve(std::string_view name) {
    const std::string lowered = trimmedLower(name);
    if (lowered == "equalpower" || lowered == "equal_power" || lowered == "equal-power" ||
        lowered == "1") {
        return "EqualPower";
    }
    return "Linear";
}

/// Text form of a skip mode, or empty when the JSON value is not one.
/// Unknown strings repair to read_only (managed Sanitized parity).
std::string skipModeFromJson(const Json& value, bool* repaired) {
    if (value.is_string()) {
        const std::string text = trimmedLower(value.get<std::string>());
        if (text == "off" || text == "closed") return "off";
        if (text == "all") return "all";
        return "read_only";
    }
    if (value.is_number_integer()) {
        const long long numeric = value.get<long long>();
        if (numeric == 0) return "off";
        if (numeric == 1) return "read_only";
        if (numeric == 2) return "all";
        if (repaired) *repaired = true;
        return "read_only";
    }
    return {};
}

int clampPoolDepth(double depth) {
    const double truncated = std::trunc(depth);
    return static_cast<int>(std::min(std::max(truncated, 1.0), 16.0));
}

/// float -> JSON number with the shortest decimal that round-trips the float,
/// so 0.1f is written as 0.1 (not 0.10000000149011612).
Json floatJson(float value) {
    char buffer[40];
    for (int precision = 1; precision <= 9; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
        if (std::strtof(buffer, nullptr) == value) break;
    }
    return Json(std::strtod(buffer, nullptr));
}

bool readFloat(const Json& root, const char* key, float& out) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_number()) return false;
    const float value = static_cast<float>(it->get<double>());
    if (!std::isfinite(value)) return false;
    out = value;
    return true;
}

bool readString(const Json& root, const char* key, std::string& out) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_string()) return false;
    out = it->get<std::string>();
    return true;
}

bool readBool(const Json& root, const char* key, bool& out) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_boolean()) return false;
    out = it->get<bool>();
    return true;
}

bool readInt(const Json& root, const char* key, int& out) {
    const auto it = root.find(key);
    if (it == root.end() || !it->is_number()) return false;
    const double real = it->get<double>();
    if (!std::isfinite(real)) return false;
    out = clampPoolDepth(real);
    return true;
}

std::string utcStamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm fields{};
#ifdef _WIN32
    gmtime_s(&fields, &now);
#else
    gmtime_r(&now, &fields);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &fields);
    return buffer;
}

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

} // namespace

const char* playerProfileStatusName(PlayerProfileStatus status) {
    switch (status) {
        case PlayerProfileStatus::Unmounted: return "unmounted";
        case PlayerProfileStatus::Loaded: return "loaded";
        case PlayerProfileStatus::Missing: return "missing";
        case PlayerProfileStatus::Corrupt: return "corrupt";
        case PlayerProfileStatus::UnsupportedVersion: return "unsupported_version";
        case PlayerProfileStatus::WrongGame: return "wrong_game";
    }
    return "unmounted";
}

std::optional<std::string> normalizeContentId(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    std::string_view text = value.substr(begin, end - begin);
    if (text.empty()) return std::nullopt;

    // Guid.TryParse accepts "{...}" and "(...)" wrappers around the D / N forms.
    if (text.size() >= 2 && ((text.front() == '{' && text.back() == '}') ||
                             (text.front() == '(' && text.back() == ')'))) {
        text = text.substr(1, text.size() - 2);
    }
    std::string digits;
    digits.reserve(32);
    if (text.size() == 36) {  // D: 8-4-4-4-12
        for (size_t i = 0; i < text.size(); ++i) {
            const bool dashPosition = (i == 8 || i == 13 || i == 18 || i == 23);
            if (dashPosition) {
                if (text[i] != '-') return std::nullopt;
            } else {
                if (!isHex(text[i])) return std::nullopt;
                digits.push_back(text[i]);
            }
        }
    } else if (text.size() == 32) {  // N: 32 hex digits
        for (char c : text) {
            if (!isHex(c)) return std::nullopt;
            digits.push_back(c);
        }
    } else {
        return std::nullopt;
    }
    for (char& c : digits) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    }
    std::string canonical;
    canonical.reserve(36);
    canonical.append(digits, 0, 8).push_back('-');
    canonical.append(digits, 8, 4).push_back('-');
    canonical.append(digits, 12, 4).push_back('-');
    canonical.append(digits, 16, 4).push_back('-');
    canonical.append(digits, 20, 12);
    return canonical;
}

struct PlayerProfileStore::Extras {
    Json unlocks = Json::array();
    Json other = Json::object();  ///< unknown top-level keys, in file order
};

PlayerProfileStore::PlayerProfileStore() : m_extras(std::make_unique<Extras>()) {}

PlayerProfileStore::~PlayerProfileStore() {
    try {
        flush();
    } catch (...) {
    }
}

void PlayerProfileStore::resetToDefaults() {
    m_preferences = PlayerPreferences{};
    m_readIds.clear();
    m_extras = std::make_unique<Extras>();
    m_fileGameId = m_gameId;
    m_dirty = false;
    m_dirtyAge = 0.0f;
    m_retryDelay = 0.0f;
}

void PlayerProfileStore::open(const fs::path& profileDir, const std::string& gameId) {
    close();
    m_dir = profileDir;
    m_path = profileDir / kPlayerProfileFileName;
    m_gameId = gameId;
    try {
        loadFromDisk();
    } catch (const std::exception& error) {
        resetToDefaults();
        m_status = PlayerProfileStatus::Corrupt;
        m_readOnly = true;
        m_diagnostic = std::string("Player profile load failed unexpectedly (") + error.what() +
                       "); using defaults, profile is read-only.";
    }
    ROWL_LOG_INFO(std::string("Player profile ") + playerProfileStatusName(m_status) + ": " +
                  Rowl::Platform::pathToUtf8(m_path) + (m_diagnostic.empty() ? "" : " — " + m_diagnostic));
}

void PlayerProfileStore::close() {
    flush();
    m_status = PlayerProfileStatus::Unmounted;
    m_diagnostic.clear();
    m_readOnly = true;
    m_dir.clear();
    m_path.clear();
    m_gameId.clear();
    resetToDefaults();
}

void PlayerProfileStore::quarantineCorruptFile() {
    const std::string prefix = "player-profile.corrupt-";
    auto listQuarantine = [&] {
        std::vector<fs::path> found;
        std::error_code listEc;
        for (fs::directory_iterator it(m_dir, listEc), endIt; !listEc && it != endIt; it.increment(listEc)) {
            const std::string name = Rowl::Platform::pathToUtf8(it->path().filename());
            std::error_code entryEc;
            if (name.rfind(prefix, 0) == 0 && name.size() > 5 &&
                name.compare(name.size() - 5, 5, ".json") == 0 && it->is_regular_file(entryEc) &&
                !entryEc) {
                found.push_back(it->path());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    };

    // The name must sort AFTER every existing quarantine file so that name order
    // is creation order (the pruning below relies on it): a same-second or
    // freed-by-pruning name gets a zero-padded "_NNN" suffix ("_" > ".").
    const std::vector<fs::path> existing = listQuarantine();
    const std::string newest = existing.empty() ? std::string() : Rowl::Platform::pathToUtf8(existing.back().filename());
    const std::string stamp = utcStamp();
    std::string name = prefix + stamp + ".json";
    std::error_code ec;
    for (int attempt = 1; attempt < 1000 && (name <= newest || fs::exists(m_dir / name, ec)); ++attempt) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "_%03d", attempt);
        name = prefix + stamp + suffix + ".json";
    }
    ec.clear();
    const fs::path target = m_dir / name;
    fs::rename(m_path, target, ec);
    if (ec) {
        m_diagnostic += " The corrupt file could not be quarantined (" + ec.message() + ").";
        return;
    }
    m_diagnostic += " Original kept as " + name + ".";

    // Only the oldest quarantine files go; the one just created always stays.
    const std::vector<fs::path> quarantined = listQuarantine();
    size_t remaining = quarantined.size();
    for (const auto& candidate : quarantined) {
        if (remaining <= kPlayerProfileMaxQuarantine) break;
        if (candidate == target) continue;
        std::error_code removeEc;
        fs::remove(candidate, removeEc);
        --remaining;
    }
}

void PlayerProfileStore::loadFromDisk() {
    resetToDefaults();
    const std::string pathText = Rowl::Platform::pathToUtf8(m_path);
    auto corrupt = [&](std::string why, bool quarantine) {
        resetToDefaults();
        m_status = PlayerProfileStatus::Corrupt;
        m_diagnostic = "Player profile '" + pathText + "' " + std::move(why) + "; using defaults.";
        if (quarantine) {
            m_readOnly = false;
            quarantineCorruptFile();
        } else {
            // Could not even read it: do not overwrite what we cannot see.
            m_readOnly = true;
            m_diagnostic += " The profile is read-only until the file becomes readable.";
        }
    };

    std::error_code ec;
    const bool exists = fs::exists(m_path, ec);
    if (!ec && !exists) {
        m_status = PlayerProfileStatus::Missing;
        m_readOnly = false;
        m_diagnostic = "No player profile at '" + pathText + "'; using defaults.";
        return;
    }
    if (ec || !fs::is_regular_file(m_path, ec) || ec) {
        corrupt("is not a readable regular file", false);
        return;
    }
    const std::uintmax_t size = fs::file_size(m_path, ec);
    if (ec) {
        corrupt("could not be inspected (" + ec.message() + ")", false);
        return;
    }
    if (size > kPlayerProfileMaxFileBytes) {
        corrupt("is larger than " + std::to_string(kPlayerProfileMaxFileBytes) + " bytes", true);
        return;
    }
    std::string raw;
    {
        std::ifstream in(m_path, std::ios::binary);
        if (!in) {
            corrupt("could not be opened for reading", false);
            return;
        }
        raw.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (in.bad()) {
            corrupt("could not be read", false);
            return;
        }
    }
    Json root = Json::parse(raw, nullptr, false);
    if (root.is_discarded()) {
        corrupt("is not valid JSON", true);
        return;
    }
    if (!root.is_object()) {
        corrupt("does not contain a JSON object", true);
        return;
    }

    long long version = kPlayerProfileVersion;
    if (const auto it = root.find("version"); it != root.end()) {
        if (!it->is_number_integer() || it->get<long long>() > INT32_MAX ||
            it->get<long long>() < INT32_MIN) {
            corrupt("carries a non-integer version", true);
            return;
        }
        version = it->get<long long>();
    }
    if (version > kPlayerProfileVersion) {
        resetToDefaults();
        m_status = PlayerProfileStatus::UnsupportedVersion;
        m_readOnly = true;
        m_diagnostic = "Player profile '" + pathText + "' targets schema v" + std::to_string(version) +
                       " (reader v" + std::to_string(kPlayerProfileVersion) +
                       "); using defaults, the file is left untouched and the profile is read-only.";
        return;
    }

    std::vector<std::string> notes;
    if (const auto it = root.find("game_id"); it != root.end()) {
        if (it->is_string()) {
            const std::string fileGame = it->get<std::string>();
            if (!m_gameId.empty()) {
                if (!fileGame.empty() && fileGame != m_gameId) {
                    resetToDefaults();
                    m_status = PlayerProfileStatus::WrongGame;
                    m_readOnly = true;
                    m_diagnostic = "Player profile '" + pathText + "' belongs to game '" + fileGame +
                                   "', not '" + m_gameId +
                                   "'; using defaults, the file is left untouched and the profile "
                                   "is read-only.";
                    return;
                }
            } else {
                m_fileGameId = fileGame;
            }
        } else {
            notes.push_back("Non-string game_id ignored.");
        }
    }

    // Preferences: wrong type / missing -> default; then the managed sanitizer.
    PlayerPreferences prefs;
    std::string text;
    float number = 0.0f;
    bool flag = false;
    int integer = 0;
    if (readString(root, "language", text)) prefs.language = normalizeLanguage(text);
    auto volume = [&](const char* key, float& field) {
        if (readFloat(root, key, number)) field = clampFloat(number, 0.0f, 1.0f);
    };
    volume("master_volume", prefs.masterVolume);
    volume("bgm_volume", prefs.bgmVolume);
    volume("voice_volume", prefs.voiceVolume);
    volume("sfx_volume", prefs.sfxVolume);
    volume("ambience_volume", prefs.ambienceVolume);
    volume("ui_volume", prefs.uiVolume);
    if (readFloat(root, "text_speed_multiplier", number)) {
        prefs.textSpeedMultiplier = clampFloat(number, 0.25f, 4.0f);
    }
    if (readFloat(root, "auto_advance_delay", number)) {
        prefs.autoAdvanceDelay = clampFloat(number, 0.0f, 60.0f);
    }
    if (readFloat(root, "text_scale", number)) prefs.textScale = snapTextScale(number);
    if (readBool(root, "high_contrast", flag)) prefs.highContrast = flag;
    if (readBool(root, "reduced_motion", flag)) prefs.reducedMotion = flag;
    if (const auto it = root.find("skip_mode"); it != root.end()) {
        bool repaired = false;
        const std::string mode = skipModeFromJson(*it, &repaired);
        if (mode.empty() || repaired) {
            notes.push_back("Unrecognized skip_mode repaired to read_only.");
        }
        if (!mode.empty()) prefs.skipMode = mode;
    }
    if (readBool(root, "auto_enabled", flag)) prefs.autoEnabled = flag;
    if (readString(root, "mixer_fade_curve", text)) prefs.mixerFadeCurve = normalizeFadeCurve(text);
    if (readInt(root, "sfx_pool_depth", integer)) prefs.sfxPoolDepth = integer;
    m_preferences = prefs;

    // Read set.
    if (const auto it = root.find("read_content_ids"); it != root.end()) {
        if (!it->is_array()) {
            notes.push_back("Non-array read_content_ids ignored.");
        } else {
            size_t dropped = 0;
            for (const auto& item : *it) {
                std::optional<std::string> id;
                if (item.is_string()) id = normalizeContentId(item.get<std::string>());
                if (id) {
                    m_readIds.insert(std::move(*id));
                } else {
                    ++dropped;
                }
            }
            if (dropped > 0) {
                notes.push_back(std::to_string(dropped) + " malformed read id(s) ignored.");
            }
            if (m_readIds.size() > m_readLimit) {
                notes.push_back("Read set holds " + std::to_string(m_readIds.size()) +
                                " ids (limit " + std::to_string(m_readLimit) +
                                "); existing ids are kept, new ids are refused.");
            }
        }
    }

    // Everything else is preserved on write.
    if (const auto it = root.find("unlocks"); it != root.end()) {
        if (it->is_array()) {
            m_extras->unlocks = *it;
        } else {
            notes.push_back("Non-array unlocks reset to an empty array.");
        }
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!isKnownKey(it.key())) m_extras->other[it.key()] = it.value();
    }

    m_status = PlayerProfileStatus::Loaded;
    m_readOnly = false;
    for (const auto& note : notes) {
        if (!m_diagnostic.empty()) m_diagnostic += ' ';
        m_diagnostic += note;
    }
    // A stray temp from an interrupted write must not accumulate beside a good file.
    cleanupStraySlotTemp(m_path);
}

void PlayerProfileStore::markDirty() {
    if (!m_dirty) {
        m_dirty = true;
        m_dirtyAge = 0.0f;
    }
}

ProfileMutation PlayerProfileStore::markRead(std::string_view contentId) {
    if (m_status == PlayerProfileStatus::Unmounted) return ProfileMutation::Unmounted;
    if (m_readOnly) return ProfileMutation::ReadOnly;
    auto id = normalizeContentId(contentId);
    if (!id) return ProfileMutation::Invalid;
    if (m_readIds.find(*id) != m_readIds.end()) return ProfileMutation::Unchanged;
    if (m_readIds.size() >= m_readLimit) {
        const std::string note = "Read set is full (" + std::to_string(m_readLimit) +
                                 " ids): new ids are not recorded.";
        if (m_diagnostic.find(note) == std::string::npos) {
            if (!m_diagnostic.empty()) m_diagnostic += ' ';
            m_diagnostic += note;
        }
        return ProfileMutation::Full;
    }
    m_readIds.insert(std::move(*id));
    markDirty();
    return ProfileMutation::Ok;
}

bool PlayerProfileStore::isRead(std::string_view contentId) const {
    if (m_status == PlayerProfileStatus::Unmounted) return false;
    const auto id = normalizeContentId(contentId);
    return id && m_readIds.find(*id) != m_readIds.end();
}

ProfileMutation PlayerProfileStore::setPreferencesJson(std::string_view json, std::string* errorOut) {
    auto invalid = [&](std::string why) {
        if (errorOut) *errorOut = std::move(why);
        return ProfileMutation::Invalid;
    };
    if (m_status == PlayerProfileStatus::Unmounted) return ProfileMutation::Unmounted;
    if (m_readOnly) return ProfileMutation::ReadOnly;
    const Json patch = Json::parse(json.begin(), json.end(), nullptr, false);
    if (patch.is_discarded() || !patch.is_object()) {
        return invalid("preferences must be a JSON object");
    }

    PlayerPreferences next = m_preferences;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const std::string& key = it.key();
        const Json& value = it.value();
        auto number = [&](float& field, float low, float high) {
            if (!value.is_number()) return false;
            const float parsed = static_cast<float>(value.get<double>());
            if (!std::isfinite(parsed)) return false;
            field = clampFloat(parsed, low, high);
            return true;
        };
        bool ok = true;
        if (key == "language") {
            ok = value.is_string();
            if (ok) next.language = normalizeLanguage(value.get<std::string>());
        } else if (key == "master_volume") ok = number(next.masterVolume, 0.0f, 1.0f);
        else if (key == "bgm_volume") ok = number(next.bgmVolume, 0.0f, 1.0f);
        else if (key == "voice_volume") ok = number(next.voiceVolume, 0.0f, 1.0f);
        else if (key == "sfx_volume") ok = number(next.sfxVolume, 0.0f, 1.0f);
        else if (key == "ambience_volume") ok = number(next.ambienceVolume, 0.0f, 1.0f);
        else if (key == "ui_volume") ok = number(next.uiVolume, 0.0f, 1.0f);
        else if (key == "text_speed_multiplier") ok = number(next.textSpeedMultiplier, 0.25f, 4.0f);
        else if (key == "auto_advance_delay") ok = number(next.autoAdvanceDelay, 0.0f, 60.0f);
        else if (key == "text_scale") {
            float raw = 1.0f;
            ok = number(raw, -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
            if (ok) next.textScale = snapTextScale(raw);
        } else if (key == "high_contrast") {
            ok = value.is_boolean();
            if (ok) next.highContrast = value.get<bool>();
        } else if (key == "reduced_motion") {
            ok = value.is_boolean();
            if (ok) next.reducedMotion = value.get<bool>();
        } else if (key == "skip_mode") {
            bool repaired = false;
            const std::string mode = skipModeFromJson(value, &repaired);
            ok = !mode.empty();
            if (ok) next.skipMode = mode;
        } else if (key == "auto_enabled") {
            ok = value.is_boolean();
            if (ok) next.autoEnabled = value.get<bool>();
        } else if (key == "mixer_fade_curve") {
            ok = value.is_string();
            if (ok) next.mixerFadeCurve = normalizeFadeCurve(value.get<std::string>());
        } else if (key == "sfx_pool_depth") {
            ok = value.is_number() && std::isfinite(value.get<double>());
            if (ok) next.sfxPoolDepth = clampPoolDepth(value.get<double>());
        } else {
            return invalid("unknown preference key '" + key + "'");
        }
        if (!ok) return invalid("preference '" + key + "' has an invalid type or value");
    }

    const PlayerPreferences& cur = m_preferences;
    const bool same = next.language == cur.language && next.masterVolume == cur.masterVolume &&
                      next.bgmVolume == cur.bgmVolume && next.voiceVolume == cur.voiceVolume &&
                      next.sfxVolume == cur.sfxVolume && next.ambienceVolume == cur.ambienceVolume &&
                      next.uiVolume == cur.uiVolume &&
                      next.textSpeedMultiplier == cur.textSpeedMultiplier &&
                      next.autoAdvanceDelay == cur.autoAdvanceDelay && next.textScale == cur.textScale &&
                      next.highContrast == cur.highContrast && next.reducedMotion == cur.reducedMotion &&
                      next.skipMode == cur.skipMode && next.autoEnabled == cur.autoEnabled &&
                      next.mixerFadeCurve == cur.mixerFadeCurve && next.sfxPoolDepth == cur.sfxPoolDepth;
    if (same) return ProfileMutation::Unchanged;
    m_preferences = std::move(next);
    markDirty();
    return ProfileMutation::Ok;
}

std::string PlayerProfileStore::serialize() const {
    Json out = Json::object();
    out["version"] = kPlayerProfileVersion;
    if (!m_fileGameId.empty()) out["game_id"] = m_fileGameId;
    out["language"] = m_preferences.language;
    std::vector<std::string> sorted(m_readIds.begin(), m_readIds.end());
    std::sort(sorted.begin(), sorted.end());
    out["read_content_ids"] = std::move(sorted);
    out["master_volume"] = floatJson(m_preferences.masterVolume);
    out["bgm_volume"] = floatJson(m_preferences.bgmVolume);
    out["voice_volume"] = floatJson(m_preferences.voiceVolume);
    out["sfx_volume"] = floatJson(m_preferences.sfxVolume);
    out["ambience_volume"] = floatJson(m_preferences.ambienceVolume);
    out["ui_volume"] = floatJson(m_preferences.uiVolume);
    out["text_speed_multiplier"] = floatJson(m_preferences.textSpeedMultiplier);
    out["auto_advance_delay"] = floatJson(m_preferences.autoAdvanceDelay);
    out["text_scale"] = floatJson(m_preferences.textScale);
    out["high_contrast"] = m_preferences.highContrast;
    out["reduced_motion"] = m_preferences.reducedMotion;
    out["skip_mode"] = m_preferences.skipMode;
    out["auto_enabled"] = m_preferences.autoEnabled;
    out["mixer_fade_curve"] = m_preferences.mixerFadeCurve;
    out["sfx_pool_depth"] = m_preferences.sfxPoolDepth;
    out["unlocks"] = m_extras->unlocks;
    for (auto it = m_extras->other.begin(); it != m_extras->other.end(); ++it) {
        out[it.key()] = it.value();
    }
    return out.dump(2, ' ', false, Json::error_handler_t::replace);
}

std::string PlayerProfileStore::toJson() const {
    return serialize();
}

std::string PlayerProfileStore::statusJson() const {
    nlohmann::json out;
    out["status"] = playerProfileStatusName(m_status);
    out["diagnostic"] = m_diagnostic;
    out["read_only"] = m_readOnly;
    out["path"] = Rowl::Platform::pathToUtf8(m_path);
    out["read_count"] = m_readIds.size();
    out["game_id"] = m_fileGameId;
    out["dirty"] = m_dirty;
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool PlayerProfileStore::flush(std::string* errorOut) {
    if (!m_dirty || m_readOnly || m_status == PlayerProfileStatus::Unmounted) return true;
    std::string error;
    std::error_code ec;
    fs::create_directories(m_dir, ec);
    bool ok = !ec;
    if (!ok) {
        error = "cannot create '" + Rowl::Platform::pathToUtf8(m_dir) + "': " + ec.message();
    } else {
        ok = writeSlotFileAtomically(m_path, serialize(), &error);
    }
    if (ok) {
        m_dirty = false;
        m_dirtyAge = 0.0f;
        m_retryDelay = 0.0f;
        return true;
    }
    m_dirtyAge = 0.0f;
    m_retryDelay = kAutosaveRetryDelaySeconds;
    if (errorOut) *errorOut = std::move(error);
    return false;
}

void PlayerProfileStore::tick(float deltaSeconds) {
    if (!m_dirty || m_readOnly || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) return;
    m_dirtyAge += deltaSeconds;
    if (m_dirtyAge >= kPlayerProfileFlushDelaySeconds + m_retryDelay) flush();
}

} // namespace Rowl::State
