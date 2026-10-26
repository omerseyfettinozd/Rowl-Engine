#include "rowl/state/save_slot_preview.hpp"

#include "rowl/core/story_graph.hpp"
#include "rowl/state/save_metadata.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace Rowl::State {

namespace {

constexpr uintmax_t kMaxSaveFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSaveNestingDepth = 128;
constexpr size_t kMaxThumbnailBase64Bytes = 1024 * 1024;
constexpr uint32_t kCurrentVersion = 4;  // GameState::CurrentSaveFormatVersion

/// Keeps the top-level scalars of the save object; everything deeper is skipped
/// (its strings are still tokenized by the parser, but nothing is stored).
// Same shape rules as base64Decode (multiple of 4, alphabet, '=' only in the last quantum)
// without producing the bytes.
bool base64Shaped(std::string_view text) {
    if (text.size() % 4 != 0) return false;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool alphabet = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                              c == '+' || c == '/';
        if (alphabet) continue;
        if (c != '=') return false;
        // padding: only the last one or two characters
        if (i + 2 < text.size()) return false;
        if (i + 1 < text.size() && text[i + 1] != '=') return false;
    }
    return true;
}

class PreviewSax {
public:
    using json = nlohmann::json;
    using number_integer_t = json::number_integer_t;
    using number_unsigned_t = json::number_unsigned_t;
    using number_float_t = json::number_float_t;
    using string_t = json::string_t;
    using binary_t = json::binary_t;

    static constexpr const char* kStrippedMarker = "STRIPPED";
    explicit PreviewSax(bool wantThumbnail, size_t strippedBytes = 0)
        : m_wantThumbnail(wantThumbnail), m_strippedBytes(strippedBytes) {}

    bool null() { return scalar(Kind::Null); }
    bool boolean(bool) { return scalar(Kind::Bool); }
    bool number_integer(number_integer_t value) {
        if (!atTop()) return true;
        m_kind = Kind::Signed;
        m_signed = value;
        return store();
    }
    bool number_unsigned(number_unsigned_t value) {
        if (!atTop()) return true;
        m_kind = Kind::Unsigned;
        m_unsigned = value;
        return store();
    }
    bool number_float(number_float_t value, const string_t&) {
        if (!atTop()) return true;
        m_kind = Kind::Float;
        m_float = value;
        return store();
    }
    bool string(string_t& value) {
        if (!atTop()) return true;
        m_kind = Kind::String;
        if (m_key == "thumbnail_png_base64") {
            if (m_strippedBytes > 0 && value == kStrippedMarker) {
                m_thumbnailBytes = m_strippedBytes;  // shape was checked when it was cut out
                m_strippedSeen = true;
            } else {
                m_thumbnailBytes = value.size();
                if (!base64Shaped(value)) {  // GameState::decodeJson refuses a malformed thumbnail
                    m_failed = true;
                    return false;
                }
            }
            if (m_wantThumbnail) m_string = std::move(value);
            m_kind = Kind::Thumbnail;
            return store();
        }
        m_string = std::move(value);
        return store();
    }
    bool binary(binary_t&) { return scalar(Kind::Null); }
    bool start_object(std::size_t) {
        if (m_depth == 0) m_rootIsObject = true;
        return enter(Kind::Container);
    }
    bool end_object() {
        --m_depth;
        return true;
    }
    bool start_array(std::size_t) { return enter(Kind::Container); }
    bool end_array() {
        --m_depth;
        return true;
    }
    bool key(string_t& value) {
        if (m_depth == 1) m_key = value;
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const json::exception&) { return false; }

    // Results.
    bool rootIsObject() const { return m_rootIsObject; }
    bool failed() const { return m_failed; }
    bool versionBad = false;       // present but not an unsigned integer
    bool hasVersion = false;
    uint64_t version = kCurrentVersion;
    uint64_t stepId = 1;
    uint64_t activeNodeId = 101;
    double bgmVolume = 1.0, masterVolume = 1.0, sfxVolume = 1.0, voiceVolume = 1.0;
    double playtime = 0.0;
    std::string savedAt, chapterId, chapterTitle, summary, graphId;
    size_t thumbnailBytes() const { return m_thumbnailBytes; }
    bool strippedSeen() const { return m_strippedSeen; }
    std::string takeThumbnail() { return std::move(m_thumbnail); }

private:
    enum class Kind { Null, Bool, Signed, Unsigned, Float, String, Thumbnail, Container };

    bool atTop() const { return m_depth == 1; }

    static bool isScalarKey(const std::string& k) {
        return k == "version" || k == "step_id" || k == "active_node_id" || k == "bgm_volume" ||
               k == "master_volume" || k == "sfx_volume" || k == "voice_volume" ||
               k == "playtime_seconds" || k == "saved_at" || k == "chapter_id" ||
               k == "chapter_title" || k == "summary" || k == "graph_id" ||
               k == "thumbnail_png_base64";
    }

    bool enter(Kind kind) {
        m_kind = kind;
        // A container where the save format has a scalar is a corrupt save.
        if (m_depth == 1 && isScalarKey(m_key)) {
            m_failed = true;
            return false;
        }
        if (++m_depth > kMaxSaveNestingDepth) {
            m_failed = true;
            return false;
        }
        return true;
    }

    bool scalar(Kind kind) {
        if (!atTop()) return true;
        m_kind = kind;
        return store();
    }

    double asNumber() const {
        switch (m_kind) {
            case Kind::Signed: return static_cast<double>(m_signed);
            case Kind::Unsigned: return static_cast<double>(m_unsigned);
            case Kind::Float: return m_float;
            default: return 0.0;
        }
    }
    bool isNumber() const { return m_kind == Kind::Signed || m_kind == Kind::Unsigned || m_kind == Kind::Float; }

    // nlohmann's value("key", default) falls back to the default on a type
    // mismatch only for null; a wrong type throws -> the full decoder reports it
    // as corrupt. Mirror that: a wrongly typed known key marks the preview failed.
    bool store() {
        const std::string& k = m_key;
        if (k == "version") {
            hasVersion = true;
            if (m_kind != Kind::Unsigned) versionBad = true;
            else version = m_unsigned;
        } else if (k == "step_id" || k == "active_node_id") {
            if (m_kind != Kind::Unsigned) { m_failed = true; return false; }
            (k == "step_id" ? stepId : activeNodeId) = m_unsigned;
        } else if (k == "bgm_volume" || k == "master_volume" || k == "sfx_volume" || k == "voice_volume") {
            if (!isNumber()) { m_failed = true; return false; }
            double& target = k == "bgm_volume" ? bgmVolume : k == "master_volume" ? masterVolume
                           : k == "sfx_volume" ? sfxVolume : voiceVolume;
            target = asNumber();
        } else if (k == "playtime_seconds") {
            if (!isNumber()) { m_failed = true; return false; }
            playtime = asNumber();
        } else if (k == "saved_at" || k == "chapter_id" || k == "chapter_title" || k == "summary" || k == "graph_id") {
            if (m_kind != Kind::String) { m_failed = true; return false; }
            std::string& target = k == "saved_at" ? savedAt : k == "chapter_id" ? chapterId
                                : k == "chapter_title" ? chapterTitle : k == "summary" ? summary : graphId;
            target = std::move(m_string);
        } else if (k == "thumbnail_png_base64") {
            if (m_kind != Kind::Thumbnail) { m_failed = true; return false; }
            if (m_wantThumbnail) m_thumbnail = std::move(m_string);
        }
        return true;
    }

    bool m_wantThumbnail;
    size_t m_strippedBytes = 0;
    bool m_strippedSeen = false;
    bool m_rootIsObject = false;
    bool m_failed = false;
    size_t m_depth = 0;
    std::string m_key;
    Kind m_kind = Kind::Null;
    number_integer_t m_signed = 0;
    number_unsigned_t m_unsigned = 0;
    number_float_t m_float = 0.0;
    std::string m_string;
    std::string m_thumbnail;
    size_t m_thumbnailBytes = 0;
};

bool readFile(const std::filesystem::path& file, std::string& out) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size == 0 || size > kMaxSaveFileBytes) return false;
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) return false;
    out.resize(static_cast<size_t>(size));
    input.read(out.data(), static_cast<std::streamsize>(out.size()));  // one bulk read, not a char iterator
    out.resize(static_cast<size_t>(std::max<std::streamsize>(0, input.gcount())));
    return !out.empty();
}

bool rangeOk(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }

}  // namespace

namespace {

/// The thumbnail is ~300 KB of base64 in a file whose rest is ~2 KB, and a JSON lexer walks
/// a string character by character. Cut the base64 run out first (checking its shape on the
/// way) and leave a marker, so the SAX pass only sees the small remainder. Returns the
/// original run length, 0 when there was nothing safe to cut (the full text is parsed then).
size_t stripThumbnail(std::string_view text, std::string& out, size_t* runStart = nullptr) {
    static constexpr std::string_view kKey = "\"thumbnail_png_base64\"";
    const size_t keyAt = text.find(kKey);
    if (keyAt == std::string_view::npos) return 0;
    size_t i = keyAt + kKey.size();
    auto skipSpace = [&] { while (i < text.size() && (text[i] == ' ' || text[i] == '\n' || text[i] == '\r' || text[i] == '\t')) ++i; };
    skipSpace();
    if (i >= text.size() || text[i] != ':') return 0;
    ++i;
    skipSpace();
    if (i >= text.size() || text[i] != '"') return 0;
    const size_t start = ++i;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool alphabet = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                              c == '+' || c == '/' || c == '=';
        if (!alphabet) break;
        ++i;
    }
    if (i >= text.size() || text[i] != '"' || i == start) return 0;  // other characters / empty: parse normally
    if (!base64Shaped(text.substr(start, i - start))) return 0;
    if (runStart) *runStart = start;
    out.reserve(text.size() - (i - start) + 16);
    out.append(text.substr(0, start));
    out.append(PreviewSax::kStrippedMarker);
    out.append(text.substr(i));
    return i - start;
}

}  // namespace

SaveSlotPreview parseSaveSlotPreview(std::string_view text) {
    SaveSlotPreview preview;
    if (text.empty() || text.size() > kMaxSaveFileBytes) return preview;
    std::string stripped;
    const size_t strippedBytes = stripThumbnail(text, stripped);
    if (strippedBytes > 0) text = stripped;
    PreviewSax sax(false, strippedBytes);
    bool ok = false;
    try {
        ok = nlohmann::json::sax_parse(text, &sax);
    } catch (...) {
        ok = false;
    }
    if (!ok || sax.failed() || !sax.rootIsObject() || sax.versionBad) return preview;
    preview.version = static_cast<uint32_t>(std::min<uint64_t>(sax.version, 0xFFFFFFFFu));
    if (sax.version != 1 && sax.version != 2 && sax.version != 3 && sax.version != kCurrentVersion) {
        preview.status = SlotPreviewStatus::Incompatible;
        return preview;
    }
    if (!rangeOk(sax.bgmVolume) || !rangeOk(sax.masterVolume) || !rangeOk(sax.sfxVolume) ||
        !rangeOk(sax.voiceVolume)) return preview;
    if (sax.stepId == 0 || sax.activeNodeId == 0) return preview;
    if (!std::isfinite(sax.playtime) || sax.playtime < 0.0) return preview;
    if (sax.chapterId.size() > 1024 || sax.chapterTitle.size() > 1024 || sax.summary.size() > 4096) return preview;
    if (sax.graphId.size() > Rowl::Core::kMaxGraphIdentityBytes) return preview;
    if (sax.thumbnailBytes() > kMaxThumbnailBase64Bytes) return preview;
    preview.status = SlotPreviewStatus::Valid;
    preview.activeNodeId = sax.activeNodeId;
    preview.playtimeSeconds = sax.playtime;
    preview.savedAt = std::move(sax.savedAt);
    preview.chapterTitle = std::move(sax.chapterTitle);
    preview.summary = std::move(sax.summary);
    preview.graphIdentity = std::move(sax.graphId);
    preview.hasThumbnail = sax.thumbnailBytes() > 0;
    return preview;
}

SaveSlotPreview readSaveSlotPreview(const std::filesystem::path& file) {
    std::string text;
    if (!readFile(file, text)) return {};
    return parseSaveSlotPreview(text);
}

std::string readSaveSlotThumbnail(const std::filesystem::path& file) {
    std::string text;
    if (!readFile(file, text)) return {};
    std::string png;
    // Fast path: cut the base64 run out, let the SAX pass confirm it is the TOP-LEVEL
    // thumbnail key, then decode the run straight from the file text.
    std::string stripped;
    size_t runStart = 0;
    const size_t runBytes = stripThumbnail(text, stripped, &runStart);
    if (runBytes > 0) {
        PreviewSax sax(false, runBytes);
        bool ok = false;
        try {
            ok = nlohmann::json::sax_parse(std::string_view(stripped), &sax);
        } catch (...) {
            ok = false;
        }
        if (!ok || sax.failed() || !sax.rootIsObject()) return {};
        if (sax.strippedSeen()) {
            if (runBytes > kMaxThumbnailBase64Bytes) return {};
            return base64Decode(std::string(text, runStart, runBytes), png) ? png : std::string();
        }
    }
    // Slow path (an odd file: the key nested, escaped, or spread over odd whitespace).
    PreviewSax sax(true);
    try {
        if (!nlohmann::json::sax_parse(std::string_view(text), &sax)) return {};
    } catch (...) {
        return {};
    }
    if (sax.failed() || !sax.rootIsObject()) return {};
    const std::string base64 = sax.takeThumbnail();
    if (base64.empty() || base64.size() > kMaxThumbnailBase64Bytes || !base64Decode(base64, png)) return {};
    return png;
}

}  // namespace Rowl::State
