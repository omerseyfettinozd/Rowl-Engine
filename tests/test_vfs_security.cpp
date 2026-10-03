/**
 * test_vfs_security.cpp — VFS mounts, package security, traversal defense.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#include "rowl/vfs/detail/rowl_sha256.hpp"

#include <future>
#include <optional>

void test_vfs_security() {
    TEST_SECTION("VFS Isolation & Package Validation");

    const auto uniqueSuffix = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto testRoot = std::filesystem::temp_directory_path() /
        ("rowl_vfs_security_" + uniqueSuffix);
    const auto mountRoot = testRoot / "project";
    std::filesystem::create_directories(mountRoot);

    {
        std::ofstream(mountRoot / "inside.txt") << "inside";
        std::ofstream(testRoot / "outside.txt") << "outside";
    }

    Rowl::VFS::LooseDirectorySource source(mountRoot.string());
    if (!source.exists("inside.txt") || source.read("inside.txt").empty()) {
        std::cerr << "VFS failed to read a valid in-root asset" << std::endl;
        exit(1);
    }
    const std::string unicodeRelative =
        "\xC4\xB1\xC5\x9F\xC4\xB1\x6B\x6C\xC4\xB1\x5F\x72\xC3\xB6\x6C\x65\x2E\x6A\x70\x67";
    const auto unicodeNativePath = mountRoot /
        std::filesystem::path(std::u8string(unicodeRelative.begin(), unicodeRelative.end()));
    std::ofstream(unicodeNativePath, std::ios::binary) << "unicode";
    if (!source.exists(unicodeRelative) || source.read(unicodeRelative) !=
            std::vector<uint8_t>{'u', 'n', 'i', 'c', 'o', 'd', 'e'}) {
        std::cerr << "VFS failed to resolve a UTF-8 in-root asset name" << std::endl;
        exit(1);
    }
    if (source.exists("../outside.txt") || !source.read("../outside.txt").empty()) {
        std::cerr << "VFS allowed a parent-directory traversal" << std::endl;
        exit(1);
    }
    std::error_code symlinkError;
    std::filesystem::create_symlink(testRoot / "outside.txt", mountRoot / "linked-outside.txt", symlinkError);
    if (symlinkError || source.exists("linked-outside.txt") || !source.read("linked-outside.txt").empty()) {
        std::cerr << "VFS allowed a symlink to escape its mount root" << std::endl;
        exit(1);
    }
    TEST_PASS("Loose-directory mounts preserve UTF-8 names and reject traversal/symlink escapes");

    const auto oversizedLooseAsset = mountRoot / "oversized.bin";
    std::ofstream(oversizedLooseAsset, std::ios::binary).close();
    std::filesystem::resize_file(oversizedLooseAsset, 128ULL * 1024 * 1024 + 1);
    if (source.exists("oversized.bin") && !source.read("oversized.bin").empty()) {
        std::cerr << "VFS loaded an oversized loose asset" << std::endl;
        exit(1);
    }
    TEST_PASS("Loose-directory mounts reject oversized assets");

    const auto malformedPackage = testRoot / "malformed.rowlpkg";
    {
        Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 0, 4096};
        std::ofstream output(malformedPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    Rowl::VFS::RowlPkgDataSource package(malformedPackage.string());
    if (package.isValid()) {
        std::cerr << "Package with out-of-bounds index was accepted" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader rejects out-of-bounds index offsets");

    const auto impossibleCountPackage = testRoot / "impossible_count.rowlpkg";
    {
        Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 100'000,
                                        sizeof(Rowl::VFS::RowlPkgHeader)};
        std::ofstream output(impossibleCountPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    if (Rowl::VFS::RowlPkgDataSource(impossibleCountPackage.string()).isValid()) {
        std::cerr << "Package accepted an impossible file count before index validation" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader rejects impossible index file counts before allocation");

    // Package metadata is an untrusted boundary. These cases ensure an archive
    // cannot smuggle paths, corrupt payload ranges, or spoof an index entry.
    const auto writePackage = [&](const std::string& name, Rowl::VFS::RowlPkgHeader header,
                                  const Rowl::VFS::RowlPkgEntryRaw& entry,
                                  const std::string& path, const std::string& payload = "x") {
        const auto packagePath = testRoot / name;
        std::ofstream output(packagePath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        output.write(path.data(), static_cast<std::streamsize>(path.size()));
        return packagePath;
    };
    const auto fnv1a64 = [](const std::string& value) {
        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    constexpr uint64_t headerSize = sizeof(Rowl::VFS::RowlPkgHeader);
    const std::string safePath = "dir/safe.txt";
    const uint64_t indexOffset = headerSize + 1;
    Rowl::VFS::RowlPkgHeader validHeader{{'R', 'O', 'W', 'L'}, 1, 1, indexOffset};
    Rowl::VFS::RowlPkgEntryRaw validEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                          headerSize, 1, 1, 0};

    const auto validPackage = writePackage("valid.rowlpkg", validHeader, validEntry, safePath);
    Rowl::VFS::RowlPkgDataSource validSource(validPackage.string());
    if (!validSource.isValid() || validSource.read("dir\\safe.txt") != std::vector<uint8_t>{'x'} ||
        !validSource.read(safePath).size()) {
        std::cerr << "Valid package did not round-trip through normalized lookup" << std::endl;
        exit(1);
    }
    std::atomic<bool> concurrentReadFailed{false};
    std::vector<std::thread> readers;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        readers.emplace_back([&] {
            for (int readIndex = 0; readIndex < 100; ++readIndex) {
                if (validSource.read("dir\\safe.txt") != std::vector<uint8_t>{'x'}) {
                    concurrentReadFailed.store(true);
                    return;
                }
            }
        });
    }
    for (auto& reader : readers) reader.join();
    if (concurrentReadFailed.load()) {
        std::cerr << "Concurrent package reads returned inconsistent data" << std::endl;
        exit(1);
    }

    // Zstd entries are decoded on demand through openStream(). Exercise small
    // reads and a backward seek so consumers such as Vorbis can parse package
    // assets without first materializing the entire decompressed entry.
    std::string streamingPayload(200'000, '\0');
    for (size_t index = 0; index < streamingPayload.size(); ++index) {
        streamingPayload[index] = static_cast<char>((index * 37u + index / 17u) % 251u);
    }
    std::vector<uint8_t> compressed(ZSTD_compressBound(streamingPayload.size()));
    const size_t compressedSize = ZSTD_compress(compressed.data(), compressed.size(),
                                                streamingPayload.data(), streamingPayload.size(), 1);
    if (ZSTD_isError(compressedSize)) {
        std::cerr << "Could not create Zstd package fixture" << std::endl;
        exit(1);
    }
    compressed.resize(compressedSize);
    const std::string streamingPath = "audio/streamed.ogg";
    const uint64_t streamingIndexOffset = headerSize + compressed.size();
    Rowl::VFS::RowlPkgHeader streamingHeader{{'R', 'O', 'W', 'L'}, 1, 1, streamingIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw streamingEntry{
        fnv1a64(streamingPath), static_cast<uint32_t>(streamingPath.size()), headerSize,
        compressed.size(), streamingPayload.size(), 1};
    const auto streamingPackage = testRoot / "streaming.rowlpkg";
    {
        std::ofstream output(streamingPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&streamingHeader), sizeof(streamingHeader));
        output.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        output.write(reinterpret_cast<const char*>(&streamingEntry), sizeof(streamingEntry));
        output.write(streamingPath.data(), static_cast<std::streamsize>(streamingPath.size()));
    }
    Rowl::VFS::RowlPkgDataSource streamingSource(streamingPackage.string());
    auto streamingAsset = streamingSource.openStream(streamingPath);
    std::array<char, 97> streamingChunk{};
    const auto firstStreamingRead = streamingAsset ? streamingAsset->read(streamingChunk.data(), streamingChunk.size()).gcount() : 0;
    if (!streamingSource.isValid() || !streamingAsset ||
        firstStreamingRead != static_cast<std::streamsize>(streamingChunk.size()) ||
        std::string_view(streamingChunk.data(), streamingChunk.size()) != std::string_view(streamingPayload.data(), streamingChunk.size())) {
        std::cerr << "Incremental Zstd package stream did not decode its first chunk" << std::endl;
        exit(1);
    }
    streamingAsset->seekg(150'000);
    streamingAsset->read(streamingChunk.data(), streamingChunk.size());
    if (!*streamingAsset || std::string_view(streamingChunk.data(), streamingChunk.size()) !=
        std::string_view(streamingPayload.data() + 150'000, streamingChunk.size())) {
        std::cerr << "Incremental Zstd package stream did not support seekable reads" << std::endl;
        exit(1);
    }
    TEST_PASS("Package Zstd entries decode incrementally with seekable streams");

    // D18a-runtime: flags=1 entries whose embedded manifest record carries a
    // compressed_sha256 are verified against the stored compressed bytes.
    // The binary format stays v1: the hash travels in rowl/manifest.json.
    // v1-shaped packages (no manifest, or manifest without the key) keep
    // loading exactly as before; a substituted or corrupted payload with a
    // key present is rejected.
    const std::string hashedEntryPath = "audio/streamed.ogg";
    const std::vector<uint8_t> expectedPayload(streamingPayload.begin(), streamingPayload.end());
    std::vector<uint8_t> verifiedCompressed(ZSTD_compressBound(streamingPayload.size()));
    verifiedCompressed.resize(ZSTD_compress(verifiedCompressed.data(), verifiedCompressed.size(),
                                            streamingPayload.data(), streamingPayload.size(), 1));
    const auto sha256Hex = [](const std::vector<uint8_t>& bytes) {
        uint8_t digest[32];
        Rowl::VFS::Detail::RowlSha256 context;
        Rowl::VFS::Detail::rowlSha256Init(&context);
        if (!bytes.empty()) {
            Rowl::VFS::Detail::rowlSha256Update(&context, bytes.data(), bytes.size());
        }
        Rowl::VFS::Detail::rowlSha256Final(&context, digest);
        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string hex;
        for (const uint8_t byte : digest) {
            hex += kHexDigits[byte >> 4];
            hex += kHexDigits[byte & 0xF];
        }
        return hex;
    };
    const std::string verifiedDigest = sha256Hex(verifiedCompressed);
    const std::string manifestWithKey =
        "{\"format\":1,\"files\":[{\"compressed_sha256\":\"" + verifiedDigest +
        "\",\"path\":\"" + hashedEntryPath + "\",\"size\":" +
        std::to_string(streamingPayload.size()) + "}]}";
    const std::string manifestWithoutKey =
        "{\"format\":1,\"files\":[{\"path\":\"" + hashedEntryPath + "\",\"size\":" +
        std::to_string(streamingPayload.size()) + "}]}";
    const std::string manifestMalformedKey =
        "{\"format\":1,\"files\":[{\"compressed_sha256\":\"deadbeef\",\"path\":\"" +
        hashedEntryPath + "\"}]}";
    const auto writeZstdPackage = [&](const std::string& name,
                                      const std::vector<uint8_t>& compressed,
                                      const std::optional<std::string>& manifestJson) {
        const std::string manifestPath = "rowl/manifest.json";
        const uint64_t entryOffset = headerSize;
        const uint64_t manifestOffset = entryOffset + compressed.size();
        const uint64_t indexOffset = manifestOffset + (manifestJson ? manifestJson->size() : 0);
        const uint32_t fileCount = manifestJson ? 2 : 1;
        Rowl::VFS::RowlPkgHeader pkgHeader{{'R', 'O', 'W', 'L'}, 1, fileCount, indexOffset};
        std::string blob(reinterpret_cast<const char*>(&pkgHeader), sizeof(pkgHeader));
        blob.append(reinterpret_cast<const char*>(compressed.data()),
                    static_cast<size_t>(compressed.size()));
        if (manifestJson) blob.append(*manifestJson);
        Rowl::VFS::RowlPkgEntryRaw entryRecord{
            fnv1a64(hashedEntryPath), static_cast<uint32_t>(hashedEntryPath.size()), entryOffset,
            compressed.size(), streamingPayload.size(), 1};
        blob.append(reinterpret_cast<const char*>(&entryRecord), sizeof(entryRecord));
        blob.append(hashedEntryPath);
        if (manifestJson) {
            Rowl::VFS::RowlPkgEntryRaw manifestRecord{
                fnv1a64(manifestPath), static_cast<uint32_t>(manifestPath.size()), manifestOffset,
                manifestJson->size(), manifestJson->size(), 0};
            blob.append(reinterpret_cast<const char*>(&manifestRecord), sizeof(manifestRecord));
            blob.append(manifestPath);
        }
        const auto packagePath = testRoot / name;
        std::ofstream output(packagePath, std::ios::binary);
        output.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        return packagePath;
    };

    // 1. No embedded manifest at all: a v1 package keeps loading untouched.
    const auto legacyNoManifest = writeZstdPackage("legacy_no_manifest.rowlpkg",
                                                   verifiedCompressed, std::nullopt);
    Rowl::VFS::RowlPkgDataSource legacyNoManifestSource(legacyNoManifest.string());
    if (!legacyNoManifestSource.isValid() ||
        legacyNoManifestSource.read(hashedEntryPath) != expectedPayload) {
        std::cerr << "A v1 package without an embedded manifest stopped loading" << std::endl;
        exit(1);
    }

    // 2. Manifest present but the record carries no compressed_sha256 key:
    // pre-D18a packages keep the legacy skip (warn-open, unverified).
    const auto legacyWithoutKey = writeZstdPackage("legacy_without_key.rowlpkg",
                                                   verifiedCompressed, manifestWithoutKey);
    Rowl::VFS::RowlPkgDataSource legacyWithoutKeySource(legacyWithoutKey.string());
    if (!legacyWithoutKeySource.isValid() ||
        legacyWithoutKeySource.read(hashedEntryPath) != expectedPayload) {
        std::cerr << "A manifest record without compressed_sha256 stopped loading" << std::endl;
        exit(1);
    }

    // 3. Key present and the payload matches: verification passes silently.
    const auto verifiedPackage = writeZstdPackage("verified_hash.rowlpkg",
                                                  verifiedCompressed, manifestWithKey);
    Rowl::VFS::RowlPkgDataSource verifiedSource(verifiedPackage.string());
    if (!verifiedSource.isValid() ||
        verifiedSource.read(hashedEntryPath) != expectedPayload) {
        std::cerr << "A manifest-verified zstd payload failed to load" << std::endl;
        exit(1);
    }

    // 4. Silent substitution: a DIFFERENT but valid zstd stream (same
    // decompressed size) replaces the payload — decompression alone would
    // succeed, so the manifest hash is the only guard. It must reject.
    std::vector<uint8_t> substitutedPayload(streamingPayload.size());
    for (size_t index = 0; index < substitutedPayload.size(); ++index) {
        substitutedPayload[index] = static_cast<char>((streamingPayload[index] + 7) % 251);
    }
    std::vector<uint8_t> substitutedCompressed(ZSTD_compressBound(substitutedPayload.size()));
    substitutedCompressed.resize(ZSTD_compress(substitutedCompressed.data(), substitutedCompressed.size(),
                                               substitutedPayload.data(), substitutedPayload.size(), 1));
    const auto substitutedPackage = writeZstdPackage("substituted_hash.rowlpkg",
                                                     substitutedCompressed, manifestWithKey);
    Rowl::VFS::RowlPkgDataSource substitutedSource(substitutedPackage.string());
    if (!substitutedSource.isValid() || !substitutedSource.read(hashedEntryPath).empty()) {
        std::cerr << "A substituted payload passed manifest sha256 verification" << std::endl;
        exit(1);
    }

    // 4b. AYIRICI-KANONİKLİK KAPISI (Windows sha256 bypass düzeltmesi).
    // Yukarıdaki test yalnızca WINDOWS'TA kırmızıydı: index tablosunun
    // anahtarları normalizePackagePath'ten geliyor ve o fonksiyon
    // u8string() ile platformun YEREL ayırıcısını döndürüyordu ('\' MSVC'de),
    // dolayısıyla "rowl/manifest.json" sabitiyle eşleşmiyor, hiçbir digest
    // bağlanmıyor ve kapı sessizce açık kalıyordu. Linux'ta aynı ayırıcı
    // olduğu için o yol burada YAKALANAMAZDI.
    //
    // Bu vaka onu platformdan bağımsız kılar: manifest'in `path` alanı
    // ters eğik çizgiyle yazılır. Düzeltme OLMADAN bu anahtar normalize
    // edilmez, index anahtarı ("audio/streamed.ogg") ile eşleşmez, digest
    // bağlanmaz ve DEĞİŞTİRİLMİŞ payload sessizce sunulur -> burada KIRMIZI.
    // Düzeltmeyle anahtar aynı kanonikleştiriciden geçer, eşleşir ve
    // değiştirilmiş payload reddedilir -> YEŞİL. Yani aynı hata sınıfı
    // artık Linux CI'da da kırmızıya düşer.
    const std::string manifestBackslashKey =
        "{\"format\":1,\"files\":[{\"compressed_sha256\":\"" + verifiedDigest +
        "\",\"path\":\"audio\\\\streamed.ogg\",\"size\":" +
        std::to_string(streamingPayload.size()) + "}]}";
    const auto backslashKeyPackage = writeZstdPackage("backslash_key.rowlpkg",
                                                     substitutedCompressed, manifestBackslashKey);
    Rowl::VFS::RowlPkgDataSource backslashKeySource(backslashKeyPackage.string());
    if (!backslashKeySource.isValid() ||
        !backslashKeySource.read(hashedEntryPath).empty()) {
        std::cerr << "A backslash-separated manifest path bypassed sha256 "
                     "verification (index keys are not canonical)"
                  << std::endl;
        exit(1);
    }

    // 5. A present-but-malformed key can never verify: fail closed.
    const auto malformedKeyPackage = writeZstdPackage("malformed_key.rowlpkg",
                                                      verifiedCompressed, manifestMalformedKey);
    Rowl::VFS::RowlPkgDataSource malformedKeySource(malformedKeyPackage.string());
    if (!malformedKeySource.isValid() || !malformedKeySource.read(hashedEntryPath).empty()) {
        std::cerr << "A malformed compressed_sha256 key verified a payload" << std::endl;
        exit(1);
    }
    TEST_PASS("Manifest sha256 verification gates flags=1 payloads while v1 warn-open holds");

    const std::string traversalPath = "../outside.txt";
    Rowl::VFS::RowlPkgEntryRaw traversalEntry{fnv1a64(traversalPath), static_cast<uint32_t>(traversalPath.size()),
                                               headerSize, 1, 1, 0};
    const auto traversalPackage = writePackage("traversal.rowlpkg", validHeader, traversalEntry, traversalPath);
    Rowl::VFS::RowlPkgDataSource traversalSource(traversalPackage.string());
    if (traversalSource.isValid()) {
        std::cerr << "Package accepted a traversal path" << std::endl;
        exit(1);
    }

    // Hash collisions/spoofing cannot redirect an asset: canonical path, not
    // the legacy hash field, is the sole lookup key.
    Rowl::VFS::RowlPkgEntryRaw badHashEntry{0, static_cast<uint32_t>(safePath.size()), headerSize, 1, 1, 0};
    const auto badHashPackage = writePackage("bad_hash.rowlpkg", validHeader, badHashEntry, safePath);
    Rowl::VFS::RowlPkgDataSource badHashSource(badHashPackage.string());
    if (!badHashSource.isValid() || badHashSource.read(safePath) != std::vector<uint8_t>{'x'}) {
        std::cerr << "Package path lookup incorrectly depends on legacy hash metadata" << std::endl;
        exit(1);
    }

    Rowl::VFS::RowlPkgEntryRaw indexPayloadEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                                  indexOffset, 1, 1, 0};
    const auto indexPayloadPackage = writePackage("index_payload.rowlpkg", validHeader, indexPayloadEntry, safePath);
    Rowl::VFS::RowlPkgDataSource indexPayloadSource(indexPayloadPackage.string());
    if (indexPayloadSource.isValid()) {
        std::cerr << "Package allowed an entry to read index bytes as payload" << std::endl;
        exit(1);
    }

    Rowl::VFS::RowlPkgEntryRaw decompressionBombEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                                       headerSize, 1, 4'096, 1};
    const auto decompressionBombPackage = writePackage("decompression_bomb.rowlpkg", validHeader,
                                                       decompressionBombEntry, safePath);
    if (Rowl::VFS::RowlPkgDataSource(decompressionBombPackage.string()).isValid()) {
        std::cerr << "Package accepted an unreasonable decompression ratio" << std::endl;
        exit(1);
    }

    const auto overlappingPackage = testRoot / "overlapping.rowlpkg";
    const std::string firstPath = "first.txt";
    const std::string secondPath = "second.txt";
    Rowl::VFS::RowlPkgHeader overlappingHeader{{'R', 'O', 'W', 'L'}, 1, 2, headerSize + 2};
    Rowl::VFS::RowlPkgEntryRaw firstEntry{fnv1a64(firstPath), static_cast<uint32_t>(firstPath.size()),
                                          headerSize, 2, 2, 0};
    Rowl::VFS::RowlPkgEntryRaw secondEntry{fnv1a64(secondPath), static_cast<uint32_t>(secondPath.size()),
                                           headerSize + 1, 1, 1, 0};
    {
        std::ofstream output(overlappingPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&overlappingHeader), sizeof(overlappingHeader));
        output.write("xy", 2);
        output.write(reinterpret_cast<const char*>(&firstEntry), sizeof(firstEntry));
        output.write(firstPath.data(), static_cast<std::streamsize>(firstPath.size()));
        output.write(reinterpret_cast<const char*>(&secondEntry), sizeof(secondEntry));
        output.write(secondPath.data(), static_cast<std::streamsize>(secondPath.size()));
    }
    Rowl::VFS::RowlPkgDataSource overlappingSource(overlappingPackage.string());
    if (overlappingSource.isValid()) {
        std::cerr << "Package accepted overlapping payload ranges" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader validates paths, compression metadata, and payload bounds");

    // Keep a small deterministic corpus of malformed binary inputs in the
    // regular test gate. These are deliberately not saved as fixtures: the
    // generator makes every run repeatable while covering many file lengths
    // and byte arrangements that hand-written examples tend to miss.
    uint32_t packageFuzzState = 0xC0FFEE42u;
    const auto nextPackageFuzzByte = [&packageFuzzState] {
        packageFuzzState = packageFuzzState * 1664525u + 1013904223u;
        return static_cast<uint8_t>(packageFuzzState >> 24u);
    };
    for (uint32_t caseIndex = 0; caseIndex < 128; ++caseIndex) {
        const auto fuzzPath = testRoot / ("fuzz_" + std::to_string(caseIndex) + ".rowlpkg");
        std::vector<uint8_t> bytes(1 + (nextPackageFuzzByte() % 512));
        for (auto& byte : bytes) byte = nextPackageFuzzByte();
        // A deliberately broken magic value makes invalidity an invariant;
        // the remaining bytes still exercise short and arbitrary-file paths.
        bytes.front() = 'X';
        std::ofstream output(fuzzPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (Rowl::VFS::RowlPkgDataSource(fuzzPath.string()).isValid()) {
            std::cerr << "Malformed package fuzz input was accepted: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Deterministic malformed-package fuzz corpus is safely rejected");

    // A valid magic prefix must not rescue a truncated archive, and a forged
    // zstd entry must fail decode safely instead of crashing the reader.
    {
        const auto truncatedMagic = testRoot / "truncated_magic.rowlpkg";
        {
            std::ofstream output(truncatedMagic, std::ios::binary);
            output.write("ROWL", 4);
            output.put(static_cast<char>(0x01));
        }
        if (Rowl::VFS::RowlPkgDataSource(truncatedMagic.string()).isValid()) {
            std::cerr << "Truncated package with valid magic was accepted" << std::endl;
            exit(1);
        }
        const std::string fakePath = "audio/fake.ogg";
        const std::string fakePayload("NOT-ZSTD-DATA-AT-ALL........");
        Rowl::VFS::RowlPkgHeader fakeHeader{{'R', 'O', 'W', 'L'}, 1, 1,
                                            headerSize + fakePayload.size()};
        Rowl::VFS::RowlPkgEntryRaw fakeEntry{fnv1a64(fakePath),
                                             static_cast<uint32_t>(fakePath.size()),
                                             headerSize, fakePayload.size(),
                                             fakePayload.size(), 1};
        const auto fakePackage = writePackage("fake_zstd.rowlpkg", fakeHeader, fakeEntry,
                                              fakePath, fakePayload);
        Rowl::VFS::RowlPkgDataSource fakeSource(fakePackage.string());
        if (!fakeSource.read(fakePath).empty()) {
            std::cerr << "Forged zstd payload decoded instead of failing safely" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Truncated archives and forged zstd payloads fail safely");

    // A selected project is an asset boundary: source files and project
    // metadata must not become readable merely because they share its root.
    const auto isolatedProject = testRoot / "isolated_project";
    std::filesystem::create_directories(isolatedProject / "Assets" / "images");
    std::ofstream(isolatedProject / "project-secret.txt") << "not-an-asset";
    std::ofstream(isolatedProject / "Assets" / "images" / "allowed.txt") << "asset";
    Rowl::VFS::VFSManager vfs;
    vfs.remountProject(isolatedProject.string());
    if (vfs.exists("project-secret.txt") || !vfs.readBytes("project-secret.txt").empty() ||
        !vfs.exists("images/allowed.txt") ||
        vfs.readString("Assets/images/allowed.txt") != "asset") {
        std::cerr << "Project remount exposed non-asset files or hid declared assets" << std::endl;
        exit(1);
    }

    // A packaged release keeps its base content in game.rowlpkg, while mods
    // may replace any entry at the same relative VFS path.  This verifies the
    // ordinary lookup path rather than only the explicit mods/ namespace.
    const auto packagedProject = testRoot / "packaged_project";
    std::filesystem::create_directories(packagedProject / "Assets" / "packages");
    std::filesystem::create_directories(packagedProject / "mods" / "dir");
    std::filesystem::copy_file(validPackage,
                               packagedProject / "Assets" / "packages" / "game.rowlpkg",
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream(packagedProject / "mods" / "dir" / "safe.txt") << "mod";
    vfs.remountProject(packagedProject.string());
    if (vfs.readString("dir/safe.txt") != "mod" ||
        vfs.readString("mods/dir/safe.txt") != "mod") {
        std::cerr << "Project mods did not override the matching package asset" << std::endl;
        exit(1);
    }
    std::filesystem::remove(packagedProject / "mods" / "dir" / "safe.txt");
    if (vfs.readString("dir/safe.txt") != "x") {
        std::cerr << "Packaged base asset was unavailable after removing a mod override" << std::endl;
        exit(1);
    }
    TEST_PASS("Mods override package assets at matching relative VFS paths");

    // A2a-fix3 (sessiz-tarama regresyonu): Windows CI'da probe "present"
    // demesine rağmen tarama tek kelime etmeden ölüyordu (tek sessiz çıkış:
    // no_such_file erken-dönüşü ya da yutulan istisna). Tarama artık
    // canonicalize + try/catch kalkanında; bu iki düşman form throw etmeden,
    // loose asset'leri gizlemeden geçmeli.
    const auto hostileProject = testRoot / "hostile_packages_project";
    std::filesystem::create_directories(hostileProject / "Assets");
    std::ofstream(hostileProject / "Assets" / "packages") << "not-a-directory";
    std::ofstream(hostileProject / "Assets" / "loose.txt") << "loose";
    vfs.remountProject(hostileProject.string());
    if (vfs.readString("loose.txt") != "loose") {
        std::cerr << "Remount with a file-as-packages-dir hid loose assets" << std::endl;
        exit(1);
    }
    const auto bareProject = testRoot / "bare_project";
    std::filesystem::create_directories(bareProject / "Assets");
    vfs.remountProject(bareProject.string());
    TEST_PASS("Hostile package-scan forms fail loudly without throwing");

    // Fail-open closure: an EMPTY project root used to return true — mounts
    // silently cleared and the caller told "success". A blank path is a
    // caller bug, not a legitimate clear request: it must be rejected like
    // any other missing root, leave the manager in a clean bare state, and
    // a valid remount afterwards must still work.
    if (!vfs.remountProject(bareProject.string()) || vfs.getMountPoints().empty()) {
        std::cerr << "A valid project root must remount successfully with mounts" << std::endl;
        exit(1);
    }
    if (vfs.remountProject("")) {
        std::cerr << "Empty project root remount must be rejected" << std::endl;
        exit(1);
    }
    if (!vfs.getMountPoints().empty()) {
        std::cerr << "Rejected empty-root remount left stale mounts behind" << std::endl;
        exit(1);
    }
    if (!vfs.remountProject(packagedProject.string()) || vfs.readString("dir/safe.txt") != "x") {
        std::cerr << "A valid remount after an empty-root rejection failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Empty project root remount is rejected and leaves a clean bare VFS");

    // A2a-fix4 (girdi-izolasyonu): patlayan/okunamayan TEK girdi taramayı
    // öldürmemeli — geçerli paket yine mount'lanmalı. Sarkan symlink
    // (POSIX'te girdi-hatası verir) + geçerli paket yanyana; Windows'ta
    // symlink ayrıcalığı yoksa .rowlpkg uzantılı dizin aynı izolasyonu dener.
    const auto isolateProject = testRoot / "isolate_packages_project";
    std::filesystem::create_directories(isolateProject / "Assets" / "packages");
    std::error_code linkError;
    std::filesystem::create_symlink("rowl-missing-target",
                                    isolateProject / "Assets" / "packages" / "broken.rowlpkg",
                                    linkError);
    if (linkError) {
        std::filesystem::create_directories(
            isolateProject / "Assets" / "packages" / "dir.rowlpkg", linkError);
    }
    std::filesystem::copy_file(validPackage,
                               isolateProject / "Assets" / "packages" / "game.rowlpkg",
                               std::filesystem::copy_options::overwrite_existing);
    vfs.remountProject(isolateProject.string());
    if (vfs.readString("dir/safe.txt") != "x") {
        std::cerr << "A hostile packages sibling killed the scan: valid package not mounted"
                  << std::endl;
        exit(1);
    }
    TEST_PASS("One hostile packages entry cannot kill the scan");


    // ---------------------------------------------------------------------
    // P2-7 / P2-8 — package verification-layer locks.
    //
    // P2-7: the manifest sha256 gate only ever ran for flags=1 entries, so a
    // corrupted RAW (flags=0) payload was served silently while its zstd twin
    // failed hard. The packer writes a `sha256` key for EVERY record; the
    // reader had a single key (`compressed_sha256`) that only exists for
    // zstd records. Both classes are gated now.
    //
    // P2-8: tryOpenStream materialized the whole entry and ran a full
    // ZSTD_decompress purely as a verification pass, then DISCARDED every byte
    // and handed the caller a stream that decoded the entry a second time.
    // The gate is a hash of the bytes as stored, so decompression was never
    // part of it. These locks pin both halves: a raw entry is gated, and a
    // stream open neither decodes nor materializes before the consumer reads.
    //
    // The "neither decodes nor materializes" half is measured through
    // pkgEntryMaterializedBytes()/pkgEntryDecodedBytes(), which live INSIDE
    // readEntry(). The zstd stream counters cannot see that pass at all — they
    // only move in ZstdEntryStreamBuf::fill() — so a mutant that restored the
    // materializing readEntry() call stayed green on them. pkgEntryDigestBytes()
    // adds the complementary lock: the gate must still run, exactly once, over
    // exactly the stored range.
    // ---------------------------------------------------------------------
    {
        const auto sha256Hex = [](const std::vector<uint8_t>& bytes) {
            Rowl::VFS::Detail::RowlSha256 context;
            Rowl::VFS::Detail::rowlSha256Init(&context);
            if (!bytes.empty()) {
                Rowl::VFS::Detail::rowlSha256Update(&context, bytes.data(), bytes.size());
            }
            uint8_t digest[32];
            Rowl::VFS::Detail::rowlSha256Final(&context, digest);
            static constexpr char kHexDigits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(64);
            for (const uint8_t byte : digest) {
                hex += kHexDigits[byte >> 4];
                hex += kHexDigits[byte & 0xF];
            }
            return hex;
        };
        const auto fnv1a64 = [](const std::string& value) {
            uint64_t hash = 14695981039346656037ULL;
            for (const unsigned char byte : value) {
                hash ^= byte;
                hash *= 1099511628211ULL;
            }
            return hash;
        };

        // flags=0 (raw) package with an embedded manifest, mirroring what
        // tools/package_assets.py emits for an over-compressible blob.
        const auto writeRawPackage = [&](const std::string& name,
                                         const std::vector<uint8_t>& payload,
                                         const std::string& manifestSha256) {
            const std::string entryName = "raw/entry.bin";
            const std::string manifestName = "rowl/manifest.json";
            const std::string manifest =
                "{\"format\":1,\"files\":[{\"compressed_size\":" +
                std::to_string(payload.size()) + ",\"flags\":0,\"path\":\"" + entryName +
                "\",\"sha256\":\"" + manifestSha256 + "\",\"size\":" +
                std::to_string(payload.size()) + "}]}";
            const uint64_t headerSize = sizeof(Rowl::VFS::RowlPkgHeader);
            const uint64_t entryOffset = headerSize;
            const uint64_t manifestOffset = entryOffset + payload.size();
            const uint64_t indexOffset = manifestOffset + manifest.size();
            Rowl::VFS::RowlPkgHeader header{
                {'R', 'O', 'W', 'L'}, 1, 2, indexOffset};
            std::string blob(reinterpret_cast<const char*>(&header), sizeof(header));
            blob.append(reinterpret_cast<const char*>(payload.data()), payload.size());
            blob.append(manifest);
            Rowl::VFS::RowlPkgEntryRaw entryRecord{
                fnv1a64(entryName), static_cast<uint32_t>(entryName.size()), entryOffset,
                payload.size(), payload.size(), 0};
            blob.append(reinterpret_cast<const char*>(&entryRecord), sizeof(entryRecord));
            blob.append(entryName);
            Rowl::VFS::RowlPkgEntryRaw manifestRecord{
                fnv1a64(manifestName), static_cast<uint32_t>(manifestName.size()),
                manifestOffset, manifest.size(), manifest.size(), 0};
            blob.append(reinterpret_cast<const char*>(&manifestRecord), sizeof(manifestRecord));
            blob.append(manifestName);
            const auto packagePath = testRoot / name;
            std::ofstream output(packagePath, std::ios::binary);
            output.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            return entryName;
        };

        std::vector<uint8_t> rawPayload(4096);
        // Fixtures are written under testRoot, which an earlier section may
        // already have torn down. Recreate it so the package writes below land
        // on disk instead of silently producing an empty (headerless) file.
        std::filesystem::create_directories(testRoot);
        for (size_t index = 0; index < rawPayload.size(); ++index) {
            rawPayload[index] = static_cast<uint8_t>(index % 251);
        }
        const std::string rawDigest = sha256Hex(rawPayload);

        // Correct manifest digest: the raw entry must be ACCEPTED. This is the
        // false-positive lock — a gate that rejected every raw entry would be
        // "secure" and useless.
        const auto goodRaw = writeRawPackage("p2_7_good_raw.rowlpkg", rawPayload, rawDigest);
        Rowl::VFS::RowlPkgDataSource goodRawSource((testRoot / "p2_7_good_raw.rowlpkg").string());
        if (!goodRawSource.isValid() || goodRawSource.tryRead(goodRaw) != rawPayload) {
            std::cerr << "P2-7: a raw entry with a correct manifest sha256 was rejected "
                         "(false positive)"
                      << std::endl;
            exit(1);
        }

        // Wrong manifest digest: the raw entry must be REJECTED on both the
        // read and the stream path. Before the fix the read returned the
        // corrupted payload and only the zstd class was ever gated.
        std::vector<uint8_t> corruptedRaw = rawPayload;
        corruptedRaw[5] = static_cast<uint8_t>(corruptedRaw[5] ^ 0xFF);
        const auto badRaw = writeRawPackage("p2_7_bad_raw.rowlpkg", corruptedRaw, rawDigest);
        Rowl::VFS::RowlPkgDataSource badRawSource((testRoot / "p2_7_bad_raw.rowlpkg").string());
        const auto badRawRead = badRawSource.tryRead(badRaw);
        const auto badRawStream = badRawSource.tryOpenStream(badRaw);
        if (!badRawSource.isValid() || badRawRead.has_value() || badRawStream) {
            std::cerr << "P2-7: a corrupted flags=0 entry escaped manifest sha256 "
                         "verification (raw entries were never gated)"
                      << std::endl;
            exit(1);
        }
        TEST_PASS("P2-7 raw (flags=0) entries obey the manifest sha256 gate on read and stream");

        // P2-8 ikinci yarısı: raw akış yolu tek bir geçişle doğrulanmalı. Raw
        // dalı readEntry'e düşer ve readEntry kapıyı KENDİSİ çalıştırır;
        // tryOpenStream de verifyEntryDigest çağırırsa aynı saklı aralık İKİ
        // KEZ hashlenir (ölçülen 37x yavaşlama — saf SHA-256 32 MiB ~233 ms,
        // gözlenen ~486 ms tam olarak iki geçiş). Bu sayaç UYGULAMADAN
        // BAĞIMSIZ: kapının okuduğu toplam baytı sayar, iki yolun ikisinde de.
        {
            Rowl::VFS::RowlPkgDataSource rawStreamSource(
                (testRoot / "p2_7_good_raw.rowlpkg").string());
            const uint64_t readBase = Rowl::VFS::pkgEntryDigestBytes();
            auto rawStream = rawStreamSource.tryOpenStream(goodRaw);
            const uint64_t readDelta = Rowl::VFS::pkgEntryDigestBytes() - readBase;
            if (!rawStream) {
                std::cerr << "P2-8: a valid raw stream failed to open" << std::endl;
                exit(1);
            }
            if (readDelta != rawPayload.size()) {
                std::cerr << "P2-8: opening a raw stream hashed its stored bytes "
                             << readDelta << " times for a " << rawPayload.size()
                          << " byte entry (must be exactly one manifest gate pass)"
                      << std::endl;
                exit(1);
            }
            rawStream.reset();

            // The read path must be single-pass too.
            const uint64_t tryReadBase = Rowl::VFS::pkgEntryDigestBytes();
            if (!rawStreamSource.tryRead(goodRaw).has_value()) {
                std::cerr << "P2-8: a valid raw read failed" << std::endl;
                exit(1);
            }
            if (Rowl::VFS::pkgEntryDigestBytes() - tryReadBase != rawPayload.size()) {
                std::cerr << "P2-8: reading a raw entry hashed its stored bytes more than once"
                          << std::endl;
                exit(1);
            }
        }
        TEST_PASS("P2-8 the manifest digest gate hashes each stored byte exactly once per open");

        // P2-8: opening a manifest-gated stream must not decode or materialize
        // the entry before the consumer reads. Baselines are taken around the
        // openStream call only (see test_audio_streaming.cpp for the same
        // contract on the seek path).
        std::vector<uint8_t> streamingPayload(512 * 1024);
        for (size_t index = 0; index < streamingPayload.size(); ++index) {
            streamingPayload[index] = static_cast<uint8_t>((index * 37u + index / 17u) % 251u);
        }
        std::vector<uint8_t> compressed(ZSTD_compressBound(streamingPayload.size()));
        const size_t compressedSize = ZSTD_compress(
            compressed.data(), compressed.size(), streamingPayload.data(),
            streamingPayload.size(), 1);
        compressed.resize(compressedSize);
        {
            const std::string entryName = "audio/gated.ogg";
            const std::string manifestName = "rowl/manifest.json";
            std::string manifest = "{\"format\":1,\"files\":[{\"compressed_sha256\":\"" +
                                   sha256Hex(compressed) + "\",\"compressed_size\":" +
                                   std::to_string(compressed.size()) + ",\"flags\":1,\"path\":\"" +
                                   entryName + "\",\"sha256\":\"" + sha256Hex(streamingPayload) +
                                   "\",\"size\":" + std::to_string(streamingPayload.size()) +
                                   "}]}";
            const uint64_t headerSize = sizeof(Rowl::VFS::RowlPkgHeader);
            const uint64_t entryOffset = headerSize;
            const uint64_t manifestOffset = entryOffset + compressed.size();
            const uint64_t indexOffset = manifestOffset + manifest.size();
            Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 2, indexOffset};
            std::string blob(reinterpret_cast<const char*>(&header), sizeof(header));
            blob.append(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            blob.append(manifest);
            Rowl::VFS::RowlPkgEntryRaw entryRecord{
                fnv1a64(entryName), static_cast<uint32_t>(entryName.size()), entryOffset,
                compressed.size(), streamingPayload.size(), 1};
            blob.append(reinterpret_cast<const char*>(&entryRecord), sizeof(entryRecord));
            blob.append(entryName);
            Rowl::VFS::RowlPkgEntryRaw manifestRecord{
                fnv1a64(manifestName), static_cast<uint32_t>(manifestName.size()),
                manifestOffset, manifest.size(), manifest.size(), 0};
            blob.append(reinterpret_cast<const char*>(&manifestRecord), sizeof(manifestRecord));
            blob.append(manifestName);
            const auto packagePath = testRoot / "p2_8_gated_stream.rowlpkg";
            std::ofstream output(packagePath, std::ios::binary);
            output.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            output.close();
            if (!output) {
                std::cerr << "P2-8 fixture write failed for " << packagePath
                          << " (blob=" << blob.size() << " bytes)" << std::endl;
                exit(1);
            }

            Rowl::VFS::RowlPkgDataSource gatedSource(packagePath.string());
            const uint64_t openBaseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
            const uint64_t openBaseCompressed = Rowl::VFS::zstdEntryStreamCompressedBytes();
            const uint64_t openBaseDecompressed = Rowl::VFS::zstdEntryStreamDecompressedBytes();
            // P2-8'in GERÇEK ölçümü. zstd akış sayaçları yalnız
            // ZstdEntryStreamBuf::fill() içinde artar; tryOpenStream'in geri
            // alınıp readEntry()'i bir doğrulama geçişi olarak çağırdığı
            // (P2-8'in kaldırdığı) materyalize decode O SAYAÇLARA hiç
            // dokunmadığı için bu iki ölçüm onu göremezdi — mutant yeşil
            // kalıyordu. readEntry'in kendi geçişini sayan bu sayaçlar onu
            // görür: materyalize edilen giriş genişliği ve decode edilen
            // bayt. Akış açılışı ikisini de ARTIRMAMALIDIR.
            const uint64_t openBaseMaterialized = Rowl::VFS::pkgEntryMaterializedBytes();
            const uint64_t openBaseEntryDecoded = Rowl::VFS::pkgEntryDecodedBytes();
            const uint64_t openBaseDigestBytes = Rowl::VFS::pkgEntryDigestBytes();
            auto stream = gatedSource.openStream(entryName);
            if (!stream || !stream->good()) {
                std::cerr << "P2-8: a manifest-verified stream failed to open" << std::endl;
                exit(1);
            }
            // The whole point of P2-8: opening a gated stream must set up the
            // decoder and nothing else. A full materializing verification pass
            // would inflate the decompressed counter before the first read.
            if (Rowl::VFS::zstdEntryStreamDecompressedBytes() != openBaseDecompressed ||
                Rowl::VFS::zstdEntryStreamCompressedBytes() != openBaseCompressed) {
                std::cerr << "P2-8: openStream decoded or read the entry during verification "
                             "(the verified bytes are not the bytes the consumer reads)"
                          << std::endl;
                exit(1);
            }
            // …ve — akış sayaçlarının göremediği — readEntry'in materyalize
            // decode geçişini de reddet. Ölçülen geçiş olsa 32 MiB'lık bir
            // girişte ~16 MiB ölü geçici bellek ve tam bir ZSTD_decompress
            // demektir.
            const uint64_t openMaterialized = Rowl::VFS::pkgEntryMaterializedBytes();
            const uint64_t openEntryDecoded = Rowl::VFS::pkgEntryDecodedBytes();
            if (openMaterialized != openBaseMaterialized ||
                openEntryDecoded != openBaseEntryDecoded) {
                std::cerr << "P2-8: openStream ran a materializing decode pass inside readEntry "
                             "(materialized +"
                          << (openMaterialized - openBaseMaterialized) << " B, decoded +"
                          << (openEntryDecoded - openBaseEntryDecoded) << " B) — the verified "
                             "bytes are not the bytes the consumer reads"
                          << std::endl;
                exit(1);
            }
            // The gate itself must still run, over exactly the stored range —
            // skipping verification would satisfy the checks above trivially.
            if (Rowl::VFS::pkgEntryDigestBytes() - openBaseDigestBytes != compressed.size()) {
                std::cerr << "P2-8: openStream did not hash the stored range exactly once ("
                          << (Rowl::VFS::pkgEntryDigestBytes() - openBaseDigestBytes) << " of "
                          << compressed.size() << " stored bytes)" << std::endl;
                exit(1);
            }
            if (Rowl::VFS::zstdEntryStreamRewindCount() != openBaseRewinds + 1) {
                std::cerr << "P2-8: openStream must perform exactly one decoder setup" << std::endl;
                exit(1);
            }

            // And the bytes the consumer reads must be the verified bytes.
            std::vector<char> consumed(
                (std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            if (consumed.size() != streamingPayload.size() ||
                std::memcmp(consumed.data(), streamingPayload.data(), consumed.size()) != 0) {
                std::cerr << "P2-8: stream output differs from the verified payload" << std::endl;
                exit(1);
            }
        }
        TEST_PASS("P2-8 manifest-gated streams verify without a materializing decode pass");

        // A gated stream whose digest does not match must not open at all.
        {
            const std::string entryName = "audio/gated.ogg";
            const std::string manifestName = "rowl/manifest.json";
            std::string manifest = "{\"format\":1,\"files\":[{\"compressed_sha256\":\"" +
                                   std::string(64, '0') + "\",\"flags\":1,\"path\":\"" +
                                   entryName + "\"}]}";
            const uint64_t headerSize = sizeof(Rowl::VFS::RowlPkgHeader);
            const uint64_t entryOffset = headerSize;
            const uint64_t manifestOffset = entryOffset + compressed.size();
            const uint64_t indexOffset = manifestOffset + manifest.size();
            Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 2, indexOffset};
            std::string blob(reinterpret_cast<const char*>(&header), sizeof(header));
            blob.append(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            blob.append(manifest);
            Rowl::VFS::RowlPkgEntryRaw entryRecord{
                fnv1a64(entryName), static_cast<uint32_t>(entryName.size()), entryOffset,
                compressed.size(), streamingPayload.size(), 1};
            blob.append(reinterpret_cast<const char*>(&entryRecord), sizeof(entryRecord));
            blob.append(entryName);
            Rowl::VFS::RowlPkgEntryRaw manifestRecord{
                fnv1a64(manifestName), static_cast<uint32_t>(manifestName.size()),
                manifestOffset, manifest.size(), manifest.size(), 0};
            blob.append(reinterpret_cast<const char*>(&manifestRecord), sizeof(manifestRecord));
            blob.append(manifestName);
            const auto packagePath = testRoot / "p2_8_bad_digest_stream.rowlpkg";
            std::ofstream output(packagePath, std::ios::binary);
            output.write(blob.data(), static_cast<std::streamsize>(blob.size()));
            output.close();
            if (!output) {
                std::cerr << "P2-8 bad-digest fixture write failed" << std::endl;
                exit(1);
            }

            Rowl::VFS::RowlPkgDataSource badDigestSource(packagePath.string());
            if (!badDigestSource.isValid() || badDigestSource.openStream(entryName)) {
                std::cerr << "P2-8: a stream opened for an entry that fails manifest sha256 "
                             "verification"
                          << std::endl;
                exit(1);
            }
        }
        TEST_PASS("P2-8 manifest-gated streams fail closed on a digest mismatch");
    }
    const std::string graphVfsPath = "json/full_story_graph.json";
    const std::string graphJson = R"({"start_node_id":101,"nodes":[{"id":101,"speaker":"Packaged","dialogue":"VFS graph"}]})";
    const uint64_t graphIndexOffset = headerSize + graphJson.size();
    Rowl::VFS::RowlPkgHeader graphHeader{{'R', 'O', 'W', 'L'}, 1, 1, graphIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw graphEntry{fnv1a64(graphVfsPath), static_cast<uint32_t>(graphVfsPath.size()),
                                          headerSize, graphJson.size(), graphJson.size(), 0};
    const auto graphPackage = packagedProject / "Assets" / "packages" / "graph.rowlpkg";
    {
        std::ofstream output(graphPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&graphHeader), sizeof(graphHeader));
        output.write(graphJson.data(), static_cast<std::streamsize>(graphJson.size()));
        output.write(reinterpret_cast<const char*>(&graphEntry), sizeof(graphEntry));
        output.write(graphVfsPath.data(), static_cast<std::streamsize>(graphVfsPath.size()));
    }
    const std::string invalidGraphVfsPath = "json/invalid_story.json";
    const std::string invalidGraphJson = "{";
    const uint64_t invalidGraphIndexOffset = headerSize + invalidGraphJson.size();
    Rowl::VFS::RowlPkgHeader invalidGraphHeader{{'R', 'O', 'W', 'L'}, 1, 1, invalidGraphIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw invalidGraphEntry{
        fnv1a64(invalidGraphVfsPath), static_cast<uint32_t>(invalidGraphVfsPath.size()),
        headerSize, invalidGraphJson.size(), invalidGraphJson.size(), 0};
    const auto invalidGraphPackage = packagedProject / "Assets" / "packages" / "invalid_graph.rowlpkg";
    {
        std::ofstream output(invalidGraphPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&invalidGraphHeader), sizeof(invalidGraphHeader));
        output.write(invalidGraphJson.data(), static_cast<std::streamsize>(invalidGraphJson.size()));
        output.write(reinterpret_cast<const char*>(&invalidGraphEntry), sizeof(invalidGraphEntry));
        output.write(invalidGraphVfsPath.data(), static_cast<std::streamsize>(invalidGraphVfsPath.size()));
    }
    RowlEngineHandle packagedHandle = RowlEngine_Create();
    if (!packagedHandle || !RowlEngine_Init(packagedHandle, 320, 180, 0)) {
        std::cerr << "Could not initialize engine for packaged graph test" << std::endl;
        exit(1);
    }
    RowlEngine_SetProjectDirectory(packagedHandle, packagedProject.string().c_str());
    if (!RowlEngine_LoadStoryGraphFromVfs(packagedHandle, graphVfsPath.c_str()) ||
        RowlEngine_GetCurrentNodeId(packagedHandle) != 101 ||
        RowlEngine_LoadStoryGraphFromVfs(packagedHandle, "json/missing.json") ||
        std::string(RowlEngine_GetLastStoryGraphError(packagedHandle)).find("missing") == std::string::npos ||
        RowlEngine_LoadStoryGraphFromVfs(packagedHandle, invalidGraphVfsPath.c_str()) ||
        std::string(RowlEngine_GetLastStoryGraphError(packagedHandle)).find("rejected") == std::string::npos ||
        RowlEngine_GetCurrentNodeId(packagedHandle) != 101) {
        std::cerr << "C API did not load the graph from the packaged VFS correctly" << std::endl;
        RowlEngine_Destroy(packagedHandle);
        exit(1);
    }
    TEST_PASS("C API reports VFS graph load failures while preserving the active graph");
    // D3 (#133): bu handle'in lease'i bırakılmalı — süreç-ömürlü video
    // lease'i sonraki bölümlerin yabancı-thread init'lerini kilitler.
    RowlEngine_Shutdown(packagedHandle);
    RowlEngine_Destroy(packagedHandle);

    // Verify VFS-first resolution for story graphs and active story
    // T0b: fixed temp name replaced with the run-unique suffix so parallel
    // runs can never mount each other's project dir.
    const auto vfsProject = std::filesystem::temp_directory_path() /
        ("rowl_vfs_story_project_" + uniqueSuffix);
    std::filesystem::remove_all(vfsProject);
    std::filesystem::create_directories(vfsProject / "Assets" / "json");
    {
        std::ofstream graphOut(vfsProject / "Assets" / "json" / "full_story_graph.json");
        graphOut << R"({"start_node_id":505,"nodes":[{"id":505,"speaker":"VFS","dialogue":"VFS Native Story"}]})";
    }
    {
        std::ofstream activeOut(vfsProject / "Assets" / "json" / "active_story.json");
        activeOut << R"({"node_id":506,"speaker":"ActiveVFS","dialogue":"Active VFS Story"})";
    }
    RowlEngineHandle vfsStoryHandle = RowlEngine_Create();
    if (vfsStoryHandle && RowlEngine_Init(vfsStoryHandle, 320, 180, 0)) {
        RowlEngine_SetProjectDirectory(vfsStoryHandle, vfsProject.string().c_str());
        if (RowlEngine_GetCurrentNodeId(vfsStoryHandle) != 505) {
            std::cerr << "VFS-first story graph auto-load failed, expected node 505, got: "
                      << RowlEngine_GetCurrentNodeId(vfsStoryHandle) << std::endl;
            exit(1);
        }
        RowlEngine_Destroy(vfsStoryHandle);
    }
    std::filesystem::remove_all(vfsProject);
    TEST_PASS("VFS-first story graph auto-load upon project mount verified");

    // A2a-tur1 (boş-dosya bilerek-boz): a present-but-empty asset must stay
    // distinguishable from a miss — engaged-empty tryRead, silent readBytes,
    // and a VALID empty stream (old code returned nullptr for empty raws).
    {
        std::ofstream(mountRoot / "empty.txt", std::ios::binary).close();
        auto emptyProbe = source.tryRead("empty.txt");
        if (!emptyProbe || !emptyProbe->empty()) {
            std::cerr << "VFS tryRead did not report an engaged-empty asset" << std::endl;
            exit(1);
        }
        if (source.tryRead("definitely-missing.txt")) {
            std::cerr << "VFS tryRead reported a miss as present" << std::endl;
            exit(1);
        }
        Rowl::VFS::VFSManager emptyVfs;
        emptyVfs.mountDirectory("", mountRoot.string());
        if (!emptyVfs.exists("empty.txt") || !emptyVfs.readBytes("empty.txt").empty() ||
            emptyVfs.readString("empty.txt") != "" || emptyVfs.exists("definitely-missing.txt") ||
            !emptyVfs.readBytes("definitely-missing.txt").empty()) {
            std::cerr << "VFS confused a present-but-empty asset with a miss" << std::endl;
            exit(1);
        }
        auto emptyStream = emptyVfs.openReadStream("empty.txt");
        if (!emptyStream || emptyStream->peek() != std::char_traits<char>::eof() ||
            emptyVfs.openReadStream("definitely-missing.txt")) {
            std::cerr << "VFS empty-asset stream was not a valid empty stream" << std::endl;
            exit(1);
        }
        const std::string emptyEntryPath = "empty.txt";
        Rowl::VFS::RowlPkgHeader emptyHeader{{'R', 'O', 'W', 'L'}, 1, 1, headerSize};
        Rowl::VFS::RowlPkgEntryRaw emptyEntry{fnv1a64(emptyEntryPath),
                                              static_cast<uint32_t>(emptyEntryPath.size()),
                                              headerSize, 0, 0, 0};
        const auto emptyPackage = writePackage("empty_entry.rowlpkg", emptyHeader, emptyEntry,
                                               emptyEntryPath, "");
        Rowl::VFS::RowlPkgDataSource emptySource(emptyPackage.string());
        auto emptyEntryProbe = emptySource.tryRead(emptyEntryPath);
        if (!emptySource.isValid() || !emptyEntryProbe || !emptyEntryProbe->empty()) {
            std::cerr << "Package tryRead did not report an engaged-empty entry" << std::endl;
            exit(1);
        }
        auto emptyEntryStream = emptySource.tryOpenStream(emptyEntryPath);
        if (!emptyEntryStream || emptyEntryStream->peek() != std::char_traits<char>::eof() ||
            emptySource.tryRead("missing.txt") || emptySource.tryOpenStream("missing.txt")) {
            std::cerr << "Package empty entry did not yield a valid empty stream" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("A2a Present-but-empty assets stay distinguishable from misses (loose + package)");

    // A2a-tur1 (unicode-entry bilerek-boz): normalizePackagePath used
    // generic_string(), which throws ERROR_NO_UNICODE_TRANSLATION on Windows
    // for non-ASCII entry names — the packaged images/ışıklı_röle.jpg entry
    // killed every Windows mount (5 reds). A package carrying that exact
    // entry name must load and round-trip. (On Linux generic==UTF-8 bytes,
    // so this locks the contract locally and bites on Windows CI.)
    {
        const std::string unicodeEntryPath =
            "images/\xC4\xB1\xC5\x9F\xC4\xB1\x6B\x6C\xC4\xB1\x5F\x72\xC3\xB6\x6C\x65\x2E\x6A\x70\x67";
        const std::string unicodePayload = "unicode-pkg";
        const uint64_t unicodeIndexOffset = headerSize + unicodePayload.size();
        Rowl::VFS::RowlPkgHeader unicodeHeader{{'R', 'O', 'W', 'L'}, 1, 1, unicodeIndexOffset};
        Rowl::VFS::RowlPkgEntryRaw unicodeEntry{fnv1a64(unicodeEntryPath),
                                                static_cast<uint32_t>(unicodeEntryPath.size()),
                                                headerSize,
                                                static_cast<uint32_t>(unicodePayload.size()),
                                                static_cast<uint32_t>(unicodePayload.size()), 0};
        const auto unicodePackage = writePackage("unicode_entry.rowlpkg", unicodeHeader, unicodeEntry,
                                                 unicodeEntryPath, unicodePayload);
        Rowl::VFS::RowlPkgDataSource unicodeSource(unicodePackage.string());
        auto unicodeProbe = unicodeSource.tryRead(unicodeEntryPath);
        const std::vector<uint8_t> unicodeExpected(unicodePayload.begin(), unicodePayload.end());
        if (!unicodeSource.isValid() || !unicodeProbe || *unicodeProbe != unicodeExpected) {
            std::cerr << "Package did not round-trip a non-ASCII UTF-8 entry name" << std::endl;
            exit(1);
        }
        if (unicodeSource.tryRead("images/missing.jpg")) {
            std::cerr << "Package reported a miss as present next to a unicode entry" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("A2a Packages with non-ASCII UTF-8 entry names load and round-trip");

    // A2a-tur1 (ölü-mount bilerek-boz): a root that cannot even be
    // canonicalized must be refused at mount time — previously it mounted
    // "successfully" and every lookup silently missed. The healthy fixture
    // root must stay valid, so the gate cannot be a blanket refusal.
    {
        if (!source.isValid()) {
            std::cerr << "VFS mount gate rejected a healthy mount root" << std::endl;
            exit(1);
        }
        std::error_code linkEc;
        std::filesystem::create_symlink(testRoot / "loop-b", testRoot / "loop-a", linkEc);
        std::filesystem::create_symlink(testRoot / "loop-a", testRoot / "loop-b", linkEc);
        if (!linkEc) {
            Rowl::VFS::LooseDirectorySource loopSource((testRoot / "loop-a").string());
            Rowl::VFS::VFSManager loopVfs;
            loopVfs.mountDirectory("", (testRoot / "loop-a").string());
            if (loopSource.isValid() || !loopVfs.getMountPoints().empty()) {
                std::cerr << "VFS mounted a root it cannot canonicalize" << std::endl;
                exit(1);
            }
        }
    }
    TEST_PASS("A2a Uncanonicalizable mount roots are refused loudly at mount time");

    // A2a-tur1 (throwing-iterator bilerek-boz): an unreadable packages dir
    // must degrade to "no package mounts", never throw across remount (old
    // code used the throwing directory_iterator overload).
    {
        const auto noReadProject = testRoot / "noread_project";
        std::filesystem::create_directories(noReadProject / "Assets" / "packages");
        std::ofstream(noReadProject / "Assets" / "ok.txt") << "ok";
#ifndef _WIN32
        std::filesystem::permissions(noReadProject / "Assets" / "packages",
                                     std::filesystem::perms::none);
#endif
        Rowl::VFS::VFSManager noReadVfs;
        bool threw = false;
        try {
            noReadVfs.remountProject(noReadProject.string());
        } catch (...) {
            threw = true;
        }
#ifndef _WIN32
        std::filesystem::permissions(noReadProject / "Assets" / "packages",
                                     std::filesystem::perms::owner_all);
#endif
        if (threw || noReadVfs.readString("ok.txt") != "ok") {
            std::cerr << "VFS remount threw on (or lost assets to) an unreadable packages dir" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("A2a Unreadable package directories degrade to no-mounts without throwing");

    // #122 bilerek-boz: varolmayan proje kökü sessizce yutulmamalı. Remount
    // kök-nedeni story/context hata kanallarında kelimesi kelimesine görünür
    // olmalı (Init'in genel miss-notu DEĞİL — spesifik tanı her zaman yazar),
    // ve motor bare-init sözleşmesini korumalı (Init hâlâ true).
    // CWD-duyarlılığı: motorun fiziksel story yedeği process-CWD'yi yoklar;
    // süit CWD'si (repo kökü) gerçek bir grafik taşır ve miss'i maskeler.
    // Bu yüzden blok süresince CWD boş bir temp dizine sabitlenir (RAII).
    {
        struct ScopedCwd {
            std::filesystem::path saved;
            bool ok = false;
            explicit ScopedCwd(const std::filesystem::path& dir) {
                std::error_code ec;
                saved = std::filesystem::current_path(ec);
                if (ec) return;
                std::filesystem::create_directories(dir, ec);
                if (ec) return;
                std::filesystem::current_path(dir, ec);
                ok = !ec;
            }
            ~ScopedCwd() {
                if (ok) {
                    std::error_code ec;
                    std::filesystem::current_path(saved, ec);
                }
            }
        };
        const auto emptyCwd = std::filesystem::temp_directory_path() /
            ("rowl_122_empty_cwd_" + uniqueSuffix);
        ScopedCwd cwdPin(emptyCwd);
        if (!cwdPin.ok) {
            std::cerr << "#122 setup: could not pin CWD" << std::endl;
            exit(1);
        }
        RowlEngineHandle missHandle = RowlEngine_Create();
        if (!missHandle || !RowlEngine_Init(missHandle, 320, 180, 0)) {
            std::cerr << "#122 setup: bare init failed" << std::endl;
            exit(1);
        }
        const auto deadRoot = std::filesystem::temp_directory_path() /
            ("rowl_122_dead_root_" + uniqueSuffix);
        std::error_code deadEc;
        std::filesystem::remove_all(deadRoot, deadEc);
        RowlEngine_SetProjectDirectory(missHandle, deadRoot.string().c_str());
        const std::string missError =
            RowlEngine_GetLastStoryGraphError(missHandle);
        if (missError.find("missing or not a directory") == std::string::npos ||
            missError.find(deadRoot.string()) == std::string::npos) {
            std::cerr << "#122: dead project root left no root-cause diagnosis, got: '"
                      << missError << "'" << std::endl;
            RowlEngine_Destroy(missHandle);
            exit(1);
        }
        RowlEngine_Destroy(missHandle);
        std::error_code sweepEc;
        std::filesystem::remove_all(emptyCwd, sweepEc);
    }
    TEST_PASS("#122 Dead project roots fail loud with a root-cause diagnosis");

    // R1 (#14) bilerek-boz: initialize() CWD'ye göre mount yapmamalı (bare
    // init). Aynı binary farklı CWD'den farklı varlık çözmemeli; varlık
    // kökü yalnızca explicit remountProject ile gelir. Taze iki VFSManager
    // once-guard'ı deler, her biri kendi CWD'sini yansıtır.
    {
        struct ScopedCwd14 {
            std::filesystem::path saved;
            bool ok = false;
            explicit ScopedCwd14(const std::filesystem::path& dir) {
                std::error_code ec;
                saved = std::filesystem::current_path(ec);
                if (ec) return;
                std::filesystem::create_directories(dir, ec);
                if (ec) return;
                std::filesystem::current_path(dir, ec);
                ok = !ec;
            }
            ~ScopedCwd14() {
                if (ok) {
                    std::error_code ec;
                    std::filesystem::current_path(saved, ec);
                }
            }
        };
        const auto dirA = testRoot / "cwd_a";
        const auto dirB = testRoot / "cwd_b";
        std::error_code setupEc;
        std::filesystem::create_directories(dirA / "Assets", setupEc);
        std::filesystem::create_directories(dirB / "Assets", setupEc);
        if (setupEc) {
            std::cerr << "#14 setup: could not plant CWD fixtures" << std::endl;
            exit(1);
        }
        std::ofstream(dirA / "Assets" / "same.txt") << "A";
        std::ofstream(dirB / "Assets" / "same.txt") << "B";
        std::string readA = "<unread>", readB = "<unread>";
        {
            ScopedCwd14 cwdPin(dirA);
            if (!cwdPin.ok) {
                std::cerr << "#14 setup: could not pin CWD to dirA" << std::endl;
                exit(1);
            }
            Rowl::VFS::VFSManager vfsA;
            vfsA.initialize();
            readA = vfsA.readString("same.txt");
        }
        {
            ScopedCwd14 cwdPin(dirB);
            if (!cwdPin.ok) {
                std::cerr << "#14 setup: could not pin CWD to dirB" << std::endl;
                exit(1);
            }
            Rowl::VFS::VFSManager vfsB;
            vfsB.initialize();
            readB = vfsB.readString("same.txt");
        }
        if (readA != readB) {
            std::cerr << "#14: initialize() resolved CWD-relative assets ('" << readA
                      << "' vs '" << readB << "')" << std::endl;
            exit(1);
        }
        // Explicit kök hâlâ çalışmalı: bare init + remountProject(dirB).
        Rowl::VFS::VFSManager vfs;
        vfs.initialize();
        if (!vfs.remountProject(dirB.string())) {
            std::cerr << "#14: remountProject rejected a valid root" << std::endl;
            exit(1);
        }
        if (vfs.readString("same.txt") != "B") {
            std::cerr << "#14: explicit remount did not resolve dirB assets" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("R1 #14: initialize() mounts nothing CWD-relative; explicit remount works");

    // No global-restore remount: vfs is function-local.
    TEST_PASS("Project remount exposes Assets but not project-root files");

    // #142 bilerek-boz: bozuk .rowlpkg WARN-only yutulmamalı. Remount sayacı
    // atlar (skippedPackageCount==1 + ilk yol tutulur), sayaç proje-değişiminde
    // sıfırlanır (bayat sayım sızmaz), SetProjectDirectory ise story OK iken
    // ValidationError(6) + "skipped" mesajını context kanalına yazar. Story
    // zaten patlaksa story hatası önceliklidir (ezme yok — isOk gardı).
    {
        const auto proj142 = testRoot / "proj142";
        std::error_code ec142;
        std::filesystem::create_directories(proj142 / "Assets" / "packages", ec142);
        std::filesystem::create_directories(proj142 / "Assets" / "json", ec142);
        if (ec142) {
            std::cerr << "#142 setup: could not stage project dirs" << std::endl;
            exit(1);
        }
        {
            std::ofstream pkg(proj142 / "Assets" / "packages" / "corrupt.rowlpkg",
                              std::ios::binary);
            pkg << "NOT-A-PACKAGE";
        }
        {
            std::ofstream graph(proj142 / "Assets" / "json" / "full_story_graph.json");
            graph << "{\"format_version\":4,\"start_node_id\":707,\"nodes\":"
                     "[{\"id\":707,\"dialogue\":\"Hi\"}]}";
        }
        Rowl::VFS::VFSManager vfs142;
        vfs142.initialize();
        if (!vfs142.remountProject(proj142.string())) {
            std::cerr << "#142: remountProject rejected a live project root" << std::endl;
            exit(1);
        }
        if (vfs142.skippedPackageCount() != 1) {
            std::cerr << "#142: corrupt package left no skip count, got "
                      << vfs142.skippedPackageCount() << std::endl;
            exit(1);
        }
        if (vfs142.firstSkippedPackage().find("corrupt.rowlpkg") == std::string::npos) {
            std::cerr << "#142: first skipped package does not name the corrupt file, got: '"
                      << vfs142.firstSkippedPackage() << "'" << std::endl;
            exit(1);
        }
        // Sayaç proje-değişiminde sıfırlanmalı: temiz kök sıfır sayımla gelir.
        const auto empty142 = testRoot / "empty142";
        std::filesystem::create_directories(empty142, ec142);
        if (ec142 || !vfs142.remountProject(empty142.string()) ||
            vfs142.skippedPackageCount() != 0) {
            std::cerr << "#142: skip count leaked across project switches" << std::endl;
            exit(1);
        }
        // C API: story OK + 1 atlanan paket → ValidationError(6) + "skipped".
        struct ScopedCwd142 {
            std::filesystem::path saved;
            bool ok = false;
            explicit ScopedCwd142(const std::filesystem::path& dir) {
                std::error_code ec;
                saved = std::filesystem::current_path(ec);
                if (ec) return;
                std::filesystem::create_directories(dir, ec);
                if (ec) return;
                std::filesystem::current_path(dir, ec);
                ok = !ec;
            }
            ~ScopedCwd142() {
                if (ok) {
                    std::error_code ec;
                    std::filesystem::current_path(saved, ec);
                }
            }
        };
        const auto cwd142 = testRoot / "empty_cwd_142";
        ScopedCwd142 cwdPin(cwd142);
        if (!cwdPin.ok) {
            std::cerr << "#142 setup: could not pin CWD" << std::endl;
            exit(1);
        }
        RowlEngineHandle h142 = RowlEngine_Create();
        if (!h142 || !RowlEngine_Init(h142, 320, 180, 0)) {
            std::cerr << "#142 setup: bare init failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetProjectDirectory(h142, proj142.string().c_str());
        const int32_t code142 = RowlEngine_GetLastResultCode(h142);
        const char* rawMsg142 = RowlEngine_GetLastResultMessage(h142);
        const std::string msg142 = rawMsg142 ? rawMsg142 : "";
        if (code142 != 6) {
            std::cerr << "#142: skipped package left no ValidationError(6), got code "
                      << code142 << " msg: '" << msg142 << "'" << std::endl;
            RowlEngine_Destroy(h142);
            exit(1);
        }
        if (msg142.find("skipped") == std::string::npos ||
            msg142.find("corrupt.rowlpkg") == std::string::npos) {
            std::cerr << "#142: skip diagnosis names no package, got: '"
                      << msg142 << "'" << std::endl;
            RowlEngine_Destroy(h142);
            exit(1);
        }
        RowlEngine_Destroy(h142);
    }
    TEST_PASS("#142 Corrupt packages surface a skip count on the context channel");

    // #143: sticky failbit — a short read must not darken the whole package.
    // Two-entry archive on one shared stream: read A (good), truncate the
    // file so B short-reads (failbit+eof set on the shared stream), restore
    // the file, read B again. Pre-fix the second B read returns empty: the
    // stale failbit makes seekg a no-op and the good() gate fails. The fix
    // (clear() before seekg in readEntry) makes B readable again, and the
    // short-read entry itself still fails safely (contained, not masked).
    {
        const std::string stickyA = "sticky/a.txt";
        const std::string stickyB = "sticky/b.txt";
        const std::string payloadA(8192, 'A');
        const std::string payloadB(8192, 'B');
        const uint64_t stickyIndexOffset =
            headerSize + payloadA.size() + payloadB.size();
        Rowl::VFS::RowlPkgHeader stickyHeader{{'R', 'O', 'W', 'L'}, 1, 2, stickyIndexOffset};
        Rowl::VFS::RowlPkgEntryRaw stickyEntryA{
            fnv1a64(stickyA), static_cast<uint32_t>(stickyA.size()),
            headerSize, payloadA.size(), payloadA.size(), 0};
        Rowl::VFS::RowlPkgEntryRaw stickyEntryB{
            fnv1a64(stickyB), static_cast<uint32_t>(stickyB.size()),
            headerSize + payloadA.size(), payloadB.size(), payloadB.size(), 0};
        const auto stickyPackage = testRoot / "sticky_failbit.rowlpkg";
        const auto writeStickyFull = [&] {
            std::ofstream output(stickyPackage, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(&stickyHeader), sizeof(stickyHeader));
            output.write(payloadA.data(), static_cast<std::streamsize>(payloadA.size()));
            output.write(payloadB.data(), static_cast<std::streamsize>(payloadB.size()));
            output.write(reinterpret_cast<const char*>(&stickyEntryA), sizeof(stickyEntryA));
            output.write(stickyA.data(), static_cast<std::streamsize>(stickyA.size()));
            output.write(reinterpret_cast<const char*>(&stickyEntryB), sizeof(stickyEntryB));
            output.write(stickyB.data(), static_cast<std::streamsize>(stickyB.size()));
        };
        writeStickyFull();
        Rowl::VFS::RowlPkgDataSource stickySource(stickyPackage.string());
        if (!stickySource.isValid()) {
            std::cerr << "#143 setup: two-entry package rejected" << std::endl;
            exit(1);
        }
        const auto readA = [&](const std::string& path) {
            const auto bytes = stickySource.read(path);
            return std::string(bytes.begin(), bytes.end());
        };
        if (readA(stickyA) != payloadA || readA(stickyB) != payloadB) {
            std::cerr << "#143 setup: baseline reads failed" << std::endl;
            exit(1);
        }
        // Poison: cut the file inside B's payload. B short-reads and sets
        // failbit+eof on the shared stream.
        std::error_code truncateEc;
        std::filesystem::resize_file(stickyPackage, headerSize + payloadA.size(), truncateEc);
        if (truncateEc) {
            std::cerr << "#143 setup: resize_file failed: " << truncateEc.message() << std::endl;
            exit(1);
        }
        if (!stickySource.read(stickyB).empty()) {
            std::cerr << "#143: short read decoded instead of failing safely" << std::endl;
            exit(1);
        }
        // Restore the file: B must be readable again on the same open stream.
        writeStickyFull();
        if (readA(stickyB) != payloadB) {
            std::cerr << "#143: sticky failbit darkened the package after restore "
                         "(short read leaked past its entry)"
                      << std::endl;
            exit(1);
        }
        if (readA(stickyA) != payloadA) {
            std::cerr << "#143: neighboring entry unreadable after restore" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("#143 Short reads stay contained; shared stream recovers after restore");

    // Windows CI stalls for minutes deleting this tree (~140 small files, one
    // file symlink, one 128 MB fixture), hanging the suite with no output;
    // the same delete is instant elsewhere. Prime suspects, ranked: (1) AV /
    // Defender real-time scan of the 128 MB file on delete, (2) the 128 MB
    // resize_file landing non-sparse on NTFS, (3) symlink reparse-point
    // handling. Open handles are ruled out: they would fail fast, not hang.
    // This block deletes on a detached worker with a 60 s watchdog. On
    // timeout the suite FAILS (T0b: a stuck delete no longer passes green)
    // and the log names the tree contents, so the next Windows run pinpoints
    // the stalling entry instead of hanging silently.
#ifdef _WIN32
    {
        // T0b: the watchdog no longer masks a stuck delete — on timeout the
        // suite fails with the tree contents named, so the next Windows run
        // pinpoints the stalling entry instead of passing green silently.
        // Fast-path errors are checked too (the error_code was swallowed).
        std::error_code cleanupEc;
        std::packaged_task<void()> cleanup([root = testRoot, &cleanupEc] {
            std::filesystem::remove_all(root, cleanupEc);
        });
        std::future<void> finished = cleanup.get_future();
        std::thread(std::move(cleanup)).detach();
        if (finished.wait_for(std::chrono::seconds(60)) != std::future_status::ready) {
            std::cerr << "VFS test tree delete exceeded 60 s; failing, OS will "
                         "reclaim the temp dir. Top-level entries:";
            std::error_code listEc;
            for (const auto& entry :
                 std::filesystem::directory_iterator(testRoot, listEc)) {
                std::cerr << " [" << entry.path().filename().string() << "]";
            }
            std::cerr << std::endl;
            exit(1);
        }
        if (cleanupEc) {
            // Tur-15 (T0b-CI kanıtı): fast-path hatası her zaman ürün
            // sinyali DEĞİLDİR — Windows'ta AV/indexer 128 MB fixture'ı
            // kapatma anında yakalayıp ERROR_SHARING_VIOLATION ("being used
            // by another process") verir; bu geçici ÇEVRE kilididir, takılma
            // değil. Bu yüzden: önce kısaca retry, hâlâ kilitliyse YÜKSEK
            // SESLİ warn + devam (dizin run-unique, OS temizler — süit,
            // janitorial kilitte KIZARMAZ). Watchdog-timeout (gerçek takılma)
            // yukarıda hâlâ exit(1): anti-hang tripwire gevşetilmedi.
            // NOT: bu warn her koşuda tekrarlarsa retry uzatılmaz — bizim
            // sızan handle'ımız şüphesiyle leak-avı açılır.
            bool cleaned = false;
            for (int attempt = 0; attempt < 25 && !cleaned; ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                std::error_code existsEc;
                if (!std::filesystem::exists(testRoot, existsEc) && !existsEc) {
                    cleaned = true;
                    break;
                }
                std::error_code retryEc;
                std::filesystem::remove_all(testRoot, retryEc);
                if (!retryEc) {
                    std::error_code goneEc;
                    if (!std::filesystem::exists(testRoot, goneEc) && !goneEc)
                        cleaned = true;
                }
            }
            if (!cleaned) {
                std::cerr << "VFS test tree delete still locked after ~5 s of "
                             "retries (transient AV/indexer lock assumed); "
                             "continuing, OS will reclaim the unique temp dir: "
                          << testRoot.string() << std::endl;
            }
        }
    }
#else
    {
        std::error_code cleanupEc;
        std::filesystem::remove_all(testRoot, cleanupEc);
        if (cleanupEc) {
            std::cerr << "VFS test tree delete failed: " << cleanupEc.message()
                      << std::endl;
            exit(1);
        }
    }
#endif

}
