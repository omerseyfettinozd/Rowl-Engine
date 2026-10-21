/**
 * test_game_identity.cpp — D01a: game identity carrier.
 *
 * Package rowl/game.json wins over a loose manifest (and over a mod-shadowed
 * copy); a loose project_uuid is the second source; otherwise a stable
 * path-derived id flagged Derived. Malformed carriers fall through with a
 * diagnostic; two roots never share an id.
 */
#include "rowl_test_harness.hpp"
#include "rowl/platform/game_identity.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Rowl::Platform::GameIdentitySource;

void identityRequire(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[game-identity] " << message << std::endl;
        exit(1);
    }
}

uint64_t fnv(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Minimal raw (flags=0) v1 package: header, payloads, index. No manifest.
void writePackage(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& files) {
    fs::create_directories(path.parent_path());
    std::string payload;
    std::string index;
    uint64_t offset = sizeof(Rowl::VFS::RowlPkgHeader);
    for (const auto& [name, content] : files) {
        Rowl::VFS::RowlPkgEntryRaw entry{fnv(name), static_cast<uint32_t>(name.size()), offset,
                                         content.size(), content.size(), 0};
        index.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
        index.append(name);
        payload.append(content);
        offset += content.size();
    }
    Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, static_cast<uint32_t>(files.size()), offset};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    out.write(index.data(), static_cast<std::streamsize>(index.size()));
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string carrier(const std::string& id, const std::string& name = "Probe") {
    return "{\"content_format\":1,\"format\":1,\"game_id\":\"" + id + "\",\"name\":\"" + name + "\"}\n";
}

Rowl::Platform::GameIdentity resolveViaVfs(const fs::path& root) {
    Rowl::VFS::VFSManager vfs;
    vfs.initialize();
    vfs.remountProject(root.string());
    return Rowl::Platform::resolveGameIdentity(&vfs, root);
}

std::string capiString(RowlEngineHandle handle, bool diagnostic) {
    uint32_t required = 0;
    auto query = diagnostic ? RowlEngine_GetGameIdentityDiagnosticUtf8 : RowlEngine_GetGameIdUtf8;
    identityRequire(query(handle, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    identityRequire(query(handle, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

} // namespace

void test_game_identity() {
    TEST_SECTION("Game Identity Carrier (D01a)");

    const fs::path base = fs::temp_directory_path() / "rowl_game_identity_probe";
    std::error_code ec;
    fs::remove_all(base, ec);
    const std::string idA = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90";
    const std::string idB = "11111111-2222-4333-8444-555555555555";
    const std::string idC = "99999999-8888-4777-8666-555555555555";

    // 1) Package carrier wins over a loose manifest and over a mod-shadowed copy.
    const fs::path packaged = base / "packaged";
    writePackage(packaged / "Assets" / "packages" / "game.rowlpkg",
                 {{"json/full_story_graph.json", "{}"}, {"rowl/game.json", carrier(idA, "Pkg Game")}});
    writeText(packaged / "project.rowlproj", "{\"project_uuid\":\"" + idB + "\"}");
    writeText(packaged / "mods" / "rowl" / "game.json", carrier(idC));
    {
        const auto identity = resolveViaVfs(packaged);
        identityRequire(identity.source == GameIdentitySource::Package, "package carrier not used");
        identityRequire(identity.id == idA, "package id mismatch: " + identity.id);
        identityRequire(identity.name == "Pkg Game", "package name lost");
    }
    TEST_PASS("Package carrier wins over manifest and mod-shadowed copy");

    // 2) Loose manifest (uppercase normalized).
    const fs::path loose = base / "loose";
    writeText(loose / "project.rowlproj", "{\"name\":\"Loose\",\"project_uuid\":\"" + std::string("0B6F1C2E-7D3A-4E58-9C41-2A8F5D6E7B90") + "\"}");
    {
        const auto identity = resolveViaVfs(loose);
        identityRequire(identity.source == GameIdentitySource::Manifest && identity.id == idA,
                        "loose manifest id not normalized/used");
    }
    TEST_PASS("Loose manifest id resolved and lowercased");

    // 3) Derived: stable, flagged, differs per root; matches the reference UUIDv5.
    const fs::path bare1 = base / "bare1";
    const fs::path bare2 = base / "bare2";
    fs::create_directories(bare1);
    fs::create_directories(bare2);
    const auto d1 = resolveViaVfs(bare1);
    const auto d1Again = resolveViaVfs(bare1);
    const auto d2 = resolveViaVfs(bare2);
    identityRequire(d1.source == GameIdentitySource::Derived, "bare root must be Derived");
    identityRequire(d1.id == d1Again.id, "derived id is not stable");
    identityRequire(d1.id != d2.id, "two roots shared a derived id");
    identityRequire(Rowl::Platform::normalizeGameId(d1.id).has_value() && d1.id[14] == '5',
                    "derived id is not a UUIDv5");
    identityRequire(Rowl::Platform::deriveGameIdFromPath("/nonexistent-rowl-test/x") ==
                        "303a7856-775e-59ae-942f-bdd81c8e5b0e",
                    "derived id deviates from the reference UUIDv5 (python uuid5)");
    TEST_PASS("Derived id is stable, flagged, per-root and reference-exact");

    // 4) Malformed carriers fall through with a diagnostic.
    const fs::path badPkg = base / "bad_package";
    writePackage(badPkg / "Assets" / "packages" / "game.rowlpkg",
                 {{"rowl/game.json", carrier("not-a-uuid")}});
    writeText(badPkg / "project.rowlproj", "{\"project_uuid\":\"" + idB + "\"}");
    {
        const auto identity = resolveViaVfs(badPkg);
        identityRequire(identity.source == GameIdentitySource::Manifest && identity.id == idB,
                        "bad package carrier did not fall back to the manifest");
        identityRequire(!identity.diagnostic.empty(), "bad package carrier left no diagnostic");
    }
    const fs::path badAll = base / "bad_all";
    writeText(badAll / "project.rowlproj", "{\"project_uuid\":\"12345\"}");
    {
        const auto identity = resolveViaVfs(badAll);
        identityRequire(identity.source == GameIdentitySource::Derived &&
                            !identity.diagnostic.empty(),
                        "malformed manifest uuid must derive with a diagnostic");
    }
    const fs::path corrupt = base / "corrupt";
    writeText(corrupt / "project.rowlproj", "{ not json");
    identityRequire(resolveViaVfs(corrupt).source == GameIdentitySource::Derived,
                    "corrupt manifest must not be fatal");
    TEST_PASS("Malformed carriers are rejected with a diagnostic and fall back");

    // 5) Public C API: resolved at SetProjectDirectory, per root.
    RowlEngineHandle handle = RowlEngine_Create();
    identityRequire(handle != nullptr && RowlEngine_Init(handle, 320, 180, 0) == 1, "engine init failed");
    identityRequire(RowlEngine_GetGameIdentitySource(handle) == ROWL_GAME_IDENTITY_NONE &&
                        capiString(handle, false).empty(),
                    "identity must be empty before a project is mounted");
    RowlEngine_SetProjectDirectory(handle, packaged.string().c_str());
    identityRequire(RowlEngine_GetGameIdentitySource(handle) == ROWL_GAME_IDENTITY_PACKAGE &&
                        capiString(handle, false) == idA,
                    "C API did not report the package id");
    RowlEngine_SetProjectDirectory(handle, badPkg.string().c_str());
    identityRequire(RowlEngine_GetGameIdentitySource(handle) == ROWL_GAME_IDENTITY_MANIFEST &&
                        capiString(handle, false) == idB && !capiString(handle, true).empty(),
                    "C API did not report manifest id + diagnostic");
    RowlEngine_SetProjectDirectory(handle, bare2.string().c_str());
    identityRequire(RowlEngine_GetGameIdentitySource(handle) == ROWL_GAME_IDENTITY_DERIVED &&
                        capiString(handle, false) == d2.id,
                    "C API did not report the derived id");
    uint32_t required = 1;
    char tiny[4] = {'x', 'x', 'x', 'x'};
    identityRequire(RowlEngine_GetGameIdUtf8(handle, tiny, 4, &required) == ROWL_RESULT_BUFFER_TOO_SMALL &&
                        required == 37 && tiny[0] == '\0',
                    "undersized buffer contract violated");
    uint64_t caps = 0;
    RowlEngine_GetCapabilities(&caps);
    identityRequire((caps & ROWL_ENGINE_CAPABILITY_GAME_IDENTITY) != 0, "capability bit missing");
    RowlEngine_Destroy(handle);
    identityRequire(RowlEngine_GetGameIdentitySource(handle) == ROWL_GAME_IDENTITY_NONE,
                    "dead handle must report NONE");
    TEST_PASS("C API reports identity per mounted root with the caller-buffer contract");

    fs::remove_all(base, ec);
}
