/**
 * test_vfs_security.cpp — VFS mounts, package security, traversal defense.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

#include <future>

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
    TEST_PASS("Loose-directory mounts reject parent traversal and symlink escapes");

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

    // Verify VFS-first resolution for story graphs and active story
    const auto vfsProject = std::filesystem::temp_directory_path() / "rowl_vfs_story_project";
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

    // No global-restore remount: vfs is function-local.
    TEST_PASS("Project remount exposes Assets but not project-root files");

    // Windows CI stalls for minutes deleting this tree (~140 small files, one
    // file symlink, one 128 MB fixture), hanging the suite with no output;
    // the same delete is instant elsewhere. Prime suspects, ranked: (1) AV /
    // Defender real-time scan of the 128 MB file on delete, (2) the 128 MB
    // resize_file landing non-sparse on NTFS, (3) symlink reparse-point
    // handling. Open handles are ruled out: they would fail fast, not hang.
    // This block deletes on a detached worker with a 60 s watchdog. On
    // timeout the suite proceeds (temp dirs are unique per run and OS-cleaned)
    // and the log names the tree contents, so the next Windows run pinpoints
    // the stalling entry instead of hanging silently.
#ifdef _WIN32
    {
        std::packaged_task<void()> cleanup([root = testRoot] {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        });
        std::future<void> finished = cleanup.get_future();
        std::thread(std::move(cleanup)).detach();
        if (finished.wait_for(std::chrono::seconds(60)) != std::future_status::ready) {
            std::cerr << "VFS test tree delete exceeded 60 s; continuing, OS will "
                         "reclaim the temp dir. Top-level entries:";
            std::error_code listEc;
            for (const auto& entry :
                 std::filesystem::directory_iterator(testRoot, listEc)) {
                std::cerr << " [" << entry.path().filename().string() << "]";
            }
            std::cerr << std::endl;
        }
    }
#else
    std::filesystem::remove_all(testRoot);
#endif
}
