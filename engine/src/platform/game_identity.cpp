#include "rowl/platform/game_identity.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/vfs/vfs.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <vector>

namespace Rowl::Platform {
namespace {

constexpr const char* kPackageCarrierPath = "rowl/game.json";
constexpr int kSupportedCarrierFormat = 1;
constexpr std::size_t kMaxCarrierBytes = 64 * 1024;
// Fixed namespace for path-derived ids. Never change: it would re-key every
// legacy/dev game that relies on the derived fallback.
constexpr std::array<uint8_t, 16> kDerivedNamespace = {
    0x52, 0x6f, 0x77, 0x6c, 0x45, 0x6e, 0x67, 0x69,
    0x6e, 0x65, 0x47, 0x61, 0x6d, 0x65, 0x49, 0x64};

// Minimal SHA-1 (RFC 3174); used only for UUIDv5, not for security.
std::array<uint8_t, 20> sha1(const std::vector<uint8_t>& message) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    std::vector<uint8_t> data = message;
    const uint64_t bitLength = static_cast<uint64_t>(message.size()) * 8u;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<uint8_t>(bitLength >> shift));
    const auto rotl = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
    for (std::size_t chunk = 0; chunk < data.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(data[chunk + i * 4]) << 24) | (uint32_t(data[chunk + i * 4 + 1]) << 16) |
                   (uint32_t(data[chunk + i * 4 + 2]) << 8) | uint32_t(data[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::array<uint8_t, 20> out{};
    const uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<uint8_t>(hs[i] >> (24 - j * 8));
    return out;
}

std::string uuidV5(const std::array<uint8_t, 16>& ns, std::string_view name) {
    std::vector<uint8_t> input(ns.begin(), ns.end());
    input.insert(input.end(), name.begin(), name.end());
    const auto digest = sha1(input);
    std::array<uint8_t, 16> bytes{};
    std::memcpy(bytes.data(), digest.data(), 16);
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x50);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) text.push_back('-');
        text.push_back(kHex[bytes[i] >> 4]);
        text.push_back(kHex[bytes[i] & 0x0F]);
    }
    return text;
}

std::string safeName(const nlohmann::json& doc) {
    const auto it = doc.find("name");
    if (it == doc.end() || !it->is_string()) return {};
    std::string name = it->get<std::string>();
    if (name.size() > 256) name.resize(256);
    return name;
}

} // namespace

std::optional<std::string> normalizeGameId(std::string_view text) {
    if (text.size() != 36) return std::nullopt;
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return std::nullopt;
            out.push_back('-');
        } else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
            out.push_back(c);
        } else if (c >= 'A' && c <= 'F') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            return std::nullopt;
        }
    }
    return out;
}

std::string deriveGameIdFromPath(const std::filesystem::path& projectRoot) {
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) canonical = std::filesystem::absolute(projectRoot, ec).lexically_normal();
    return uuidV5(kDerivedNamespace, "rowl:project-path:" + pathToUtf8(canonical));
}

GameIdentity resolveGameIdentity(Rowl::VFS::VFSManager* vfs,
                                 const std::filesystem::path& projectRoot) {
    GameIdentity identity;
    const auto addDiagnostic = [&](const std::string& message) {
        if (!identity.diagnostic.empty()) identity.diagnostic += "; ";
        identity.diagnostic += message;
    };

    // 1) Package carrier. Only real package sources are asked: loose
    //    mods/Assets directories can shadow ordinary assets but never identity.
    if (vfs) {
        for (const auto& [prefix, source] : vfs->getMountPoints()) {
            (void)prefix;
            if (!dynamic_cast<Rowl::VFS::RowlPkgDataSource*>(source.get())) continue;
            const auto bytes = source->tryRead(kPackageCarrierPath);
            if (!bytes) continue;
            if (bytes->size() > kMaxCarrierBytes) {
                addDiagnostic("rowl/game.json is larger than the accepted limit");
                continue;
            }
            try {
                const auto doc = nlohmann::json::parse(bytes->begin(), bytes->end());
                if (!doc.is_object() || !doc.contains("format") ||
                    !doc["format"].is_number_integer() ||
                    doc["format"].get<int>() != kSupportedCarrierFormat) {
                    addDiagnostic("rowl/game.json has an unsupported format");
                    continue;
                }
                const auto id = doc.contains("game_id") && doc["game_id"].is_string()
                                    ? normalizeGameId(doc["game_id"].get<std::string>())
                                    : std::nullopt;
                if (!id) {
                    addDiagnostic("rowl/game.json game_id is not a valid UUID");
                    continue;
                }
                identity.id = *id;
                identity.name = safeName(doc);
                identity.source = GameIdentitySource::Package;
                return identity;
            } catch (const std::exception&) {
                addDiagnostic("rowl/game.json is not valid JSON");
            }
        }
    }

    // 2) Loose project manifest (informational/unversioned: never fatal).
    const auto manifestPath = projectRoot / "project.rowlproj";
    std::error_code ec;
    if (std::filesystem::is_regular_file(manifestPath, ec) && !ec &&
        std::filesystem::file_size(manifestPath, ec) <= 1024 * 1024 && !ec) {
        try {
            std::ifstream stream(manifestPath, std::ios::binary);
            const auto doc = nlohmann::json::parse(stream);
            if (doc.is_object() && doc.contains("project_uuid")) {
                const auto id = doc["project_uuid"].is_string()
                                    ? normalizeGameId(doc["project_uuid"].get<std::string>())
                                    : std::nullopt;
                if (id) {
                    identity.id = *id;
                    identity.name = safeName(doc);
                    identity.source = GameIdentitySource::Manifest;
                    return identity;
                }
                addDiagnostic("project.rowlproj project_uuid is not a valid UUID");
            }
        } catch (const std::exception&) {
            addDiagnostic("project.rowlproj is not valid JSON");
        }
    }

    // 3) Derived fallback: stable per location, flagged so callers never
    //    mistake it for an authored identity.
    identity.id = deriveGameIdFromPath(projectRoot);
    identity.source = GameIdentitySource::Derived;
    if (!identity.diagnostic.empty())
        ROWL_LOG_WARN("Game identity fell back to a path-derived id: " + identity.diagnostic);
    return identity;
}

} // namespace Rowl::Platform
