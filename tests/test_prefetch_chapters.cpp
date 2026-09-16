/**
 * test_prefetch_chapters.cpp — Faz 5 Dilim 4 native hat testleri.
 *
 * Kapsam: asset listesi toplama (4 slot + ses kanallari dahil), bayt-butce
 * kesmesi (butce-disi asset kuyrukta kalir), sure-butcesi erteleme, komsuluk
 * (aktif+-1 bellekte, +-2 unload), seffaf reload (unload->erisim->geri-yuklendi
 * + tani), legacy tek-dosya, C API vektorleri (65536, null-handle,
 * caller-buffer, clamp), 10.000-node'luk sentetik cok-chapter fixture'da
 * bellek-sinir probu (uzak chapter'lar unload iken sayim butcesi).
 */
#include "rowl_test_harness.hpp"
#include "rowl/core/chapter_loader.hpp"
#include "rowl/core/prefetch.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/vfs/vfs.hpp"

#include <nlohmann/json.hpp>
#include <unistd.h>
#include <unordered_map>

namespace {

void failPrefetch(const std::string& message) {
    std::cerr << "prefetch_chapters FAILED: " << message << std::endl;
    exit(1);
}

void checkPrefetch(bool condition, const std::string& message) {
    if (!condition) failPrefetch(message);
}

std::string makeTempDir(const std::string& name) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("rowl_prefetch_" + name + "_" + std::to_string(::getpid()));
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(base, error);
    if (error) failPrefetch("cannot create temp dir: " + error.message());
    return base.string();
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) failPrefetch("cannot write file: " + path);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void writeFileBytes(const std::string& path, std::size_t bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!file) failPrefetch("cannot write file: " + path);
    const std::string chunk(4096, 'A');
    std::size_t remaining = bytes;
    while (remaining > 0) {
        const std::size_t step = std::min<std::size_t>(remaining, chunk.size());
        file.write(chunk.data(), static_cast<std::streamsize>(step));
        remaining -= step;
    }
}

Rowl::Core::StoryNode makeNode(uint64_t id, const std::string& chapter) {
    Rowl::Core::StoryNode node;
    node.id = id;
    node.chapterId = chapter;
    node.background = "bg_" + chapter + ".png";
    node.character = "char.png";
    auto component = [&](const std::string& type, nlohmann::json data) {
        Rowl::Core::ComponentData componentData;
        componentData.type = type;
        componentData.id = type + "_" + std::to_string(id);
        componentData.enabled = true;
        componentData.data = std::move(data);
        node.components.push_back(std::move(componentData));
    };
    component("background", {{"texture", "tex_" + std::to_string(id) + ".png"}});
    component("character", {{"sprite", "sprite_" + std::to_string(id) + ".png"},
                            {"layers",
                             {{"body", "body_" + std::to_string(id) + ".png"},
                              {"face", {{"asset", "face_" + std::to_string(id) + ".png"}}},
                              {"outfit", "outfit_" + std::to_string(id) + ".png"},
                              {"accessory", "acc_" + std::to_string(id) + ".png"}}},
                            {"voice_blip_sound", "blip_" + std::to_string(id) + ".ogg"}});
    component("dialogue", {{"custom_box_texture", "box_" + std::to_string(id) + ".png"},
                           {"typewriter_sound", "voice_" + std::to_string(id) + ".ogg"}});
    component("audio", {{"bgm_track", "bgm_" + std::to_string(id) + ".ogg"},
                        {"sfx_track", "sfx_" + std::to_string(id) + ".ogg"},
                        {"voice_track", "vox_" + std::to_string(id) + ".ogg"},
                        {"ambience_track", "amb_" + std::to_string(id) + ".ogg"}});
    component("choice", {{"options",
                          {{{"option_id", "opt_a"},
                            {"normal_image", "btn_" + std::to_string(id) + ".png"}}}}});
    return node;
}

std::string queryJson(RowlEngineHandle handle,
                      RowlEngine_ResultCode (*getter)(RowlEngineHandle, char*, uint32_t,
                                                     uint32_t*)) {
    uint32_t required = 0;
    if (getter(handle, nullptr, 0, &required) != ROWL_RESULT_OK || required < 1) {
        failPrefetch("caller-buffer size query failed");
    }
    if (required > 1) {
        std::vector<char> undersized(required - 1, 'x');
        uint32_t repeated = 0;
        if (getter(handle, undersized.data(), static_cast<uint32_t>(undersized.size()),
                   &repeated) != ROWL_RESULT_BUFFER_TOO_SMALL ||
            repeated != required || undersized.front() != '\0') {
            failPrefetch("caller-buffer undersized contract failed");
        }
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (getter(handle, buffer.data(), static_cast<uint32_t>(buffer.size()), &repeated) !=
            ROWL_RESULT_OK ||
        repeated != required || buffer.back() != '\0') {
        failPrefetch("caller-buffer exact-size copy failed");
    }
    return std::string(buffer.data());
}

void testAssetCollection() {
    TEST_SECTION("Prefetch asset collection (4 slots + audio channels)");
    Rowl::Core::StoryNode node = makeNode(7, "ch1");
    const auto assets = Rowl::Core::collectNodeAssets(node);
    std::unordered_map<std::string, int> kinds;
    for (const auto& asset : assets) {
        kinds[asset.kind]++;
        checkPrefetch(!asset.path.empty(), "asset path must not be empty");
        checkPrefetch(asset.nodeId == 7, "asset node id wrong");
        checkPrefetch(asset.chapterId == "ch1", "asset chapter id wrong");
    }
    checkPrefetch(kinds["image"] == 5, "expected 5 images (bg, char, tex, box, btn), got " +
                                           std::to_string(kinds["image"]));
    checkPrefetch(kinds["character-body"] == 2, "expected body x2 (legacy sprite + layer)");
    checkPrefetch(kinds["character-face"] == 1, "expected 1 face slot asset");
    checkPrefetch(kinds["character-outfit"] == 1, "expected 1 outfit slot asset");
    checkPrefetch(kinds["character-accessory"] == 1, "expected 1 accessory slot asset");
    checkPrefetch(kinds["audio-bgm"] == 1, "expected 1 bgm track");
    checkPrefetch(kinds["audio-voice"] == 3, "expected 3 voice assets (blip, typewriter, voice_track)");
    checkPrefetch(kinds["audio-sfx"] == 1, "expected 1 sfx track");
    checkPrefetch(kinds["audio-ambience"] == 1, "expected 1 ambience track");
    checkPrefetch(assets.size() == 16, "expected 16 unique assets, got " +
                                           std::to_string(assets.size()));

    // Ayni yol iki kez gecse tek kalem olur (dedup).
    Rowl::Core::StoryNode dup = makeNode(8, "ch1");
    dup.background = "bg_ch1.png"; // makeNode(7) ile ayni arka plan degil; esitle:
    dup.background = node.background;
    Rowl::Core::StoryGraphDocument document;
    document.nodes.emplace(7, node);
    document.nodes.emplace(8, dup);
    const auto merged =
        Rowl::Core::collectDocumentAssets(document, {7, 8, 999999});
    // makeNode(7) ve makeNode(8) bg_ch1.png + char.png'i paylasir: 16+16-2.
    checkPrefetch(merged.size() == 30, "dedup across nodes failed: " +
                                           std::to_string(merged.size()));
    TEST_PASS("prefetch asset collection");
}

void testByteBudgetCut() {
    TEST_SECTION("Prefetch byte-budget cut (over-budget asset stays queued)");
    const std::string dir = makeTempDir("bytebudget");
    writeFileBytes(dir + "/a.bin", 100);
    writeFileBytes(dir + "/b.bin", 100);
    writeFileBytes(dir + "/c.bin", 100);

    Rowl::VFS::VFSManager vfs;
    vfs.mountDirectory("", dir);

    std::vector<Rowl::Core::PrefetchAsset> assets = {
        {"a.bin", "image", 1, "ch"}, {"b.bin", "image", 1, "ch"}, {"c.bin", "image", 1, "ch"}};
    Rowl::Core::AssetPrefetch prefetch;
    prefetch.enqueue(assets, 250); // 2 x 100 sigar, 3. disarida kalir.
    checkPrefetch(prefetch.budgetBytes() == 250, "budget must be kept verbatim below max");
    const std::size_t pumped = prefetch.pump(&vfs, 5000.0);
    checkPrefetch(pumped == 2, "expected 2 ready, got " + std::to_string(pumped));
    checkPrefetch(prefetch.readyAssets() == 2, "ready counter wrong");
    checkPrefetch(prefetch.readyBytes() == 200, "ready bytes wrong");
    checkPrefetch(prefetch.queuedAssets() == 1, "over-budget asset must stay queued");
    checkPrefetch(!prefetch.complete(), "queue must not report complete");
    checkPrefetch(!prefetch.lastDiagnostic().empty(), "budget stop must produce a diagnostic");
    const auto progress = nlohmann::json::parse(prefetch.progressJson());
    checkPrefetch(progress["total_assets"] == 3, "progress total wrong");
    checkPrefetch(progress["ready_assets"] == 2, "progress ready wrong");
    checkPrefetch(progress["queued_assets"] == 1, "progress queued wrong");
    checkPrefetch(progress["complete"] == false, "progress must not be complete");

    // Eksik asset prefetch'i durdurmaz: ortaya eksik yol ekle, genis butceyle sur.
    std::vector<Rowl::Core::PrefetchAsset> mixed = {{"a.bin", "image", 1, "ch"},
                                                   {"nope_missing.bin", "image", 1, "ch"},
                                                   {"b.bin", "image", 1, "ch"}};
    prefetch.enqueue(mixed, 0); // 0 = varsayilan 32 MiB.
    checkPrefetch(prefetch.budgetBytes() == Rowl::Core::kPrefetchDefaultBudgetBytes,
                  "zero budget must select the default");
    const std::size_t pumpedMixed = prefetch.pump(&vfs, 5000.0);
    checkPrefetch(pumpedMixed == 2, "missing must not stop the queue");
    checkPrefetch(prefetch.missingAssets() == 1, "missing counter wrong");
    checkPrefetch(prefetch.complete(), "queue must report complete");
    checkPrefetch(!prefetch.lastDiagnostic().empty(), "missing asset must produce a diagnostic");
    const auto mixedProgress = nlohmann::json::parse(prefetch.progressJson());
    checkPrefetch(mixedProgress["missing_assets"] == 1, "progress missing wrong");
    checkPrefetch(mixedProgress["missing_paths"].size() == 1, "missing paths wrong");
    TEST_PASS("prefetch byte-budget cut + missing-continues");
}

void testTimeBudgetDeferral() {
    TEST_SECTION("Prefetch time-budget deferral");
    const std::string dir = makeTempDir("timebudget");
    for (int i = 0; i < 4; ++i) writeFileBytes(dir + "/t" + std::to_string(i) + ".bin", 10);

    Rowl::VFS::VFSManager vfs;
    vfs.mountDirectory("", dir);

    std::vector<Rowl::Core::PrefetchAsset> assets;
    for (int i = 0; i < 4; ++i) {
        assets.push_back({"t" + std::to_string(i) + ".bin", "image", 1, "ch"});
    }
    Rowl::Core::AssetPrefetch prefetch;
    prefetch.enqueue(assets, 0);

    // Sahte saat: her okumada +10 ms ilerler; butce 4 ms -> ilk asset hazir,
    // sonrasi ertelenir (uretimle ayni kod yolu, deterministik).
    auto fakeNow = std::chrono::steady_clock::now();
    int calls = 0;
    prefetch.setNowForTests([&]() mutable {
        auto current = fakeNow;
        if (++calls > 1) fakeNow += std::chrono::milliseconds(10);
        return current;
    });
    const std::size_t first = prefetch.pump(&vfs, 4.0);
    checkPrefetch(first == 1, "expected exactly 1 asset before deferral, got " +
                                  std::to_string(first));
    checkPrefetch(prefetch.queuedAssets() == 3, "deferred assets must stay queued");
    checkPrefetch(!prefetch.complete(), "deferred queue must not be complete");

    // Gercek saatle devam: kalan 3 asset hazir olur.
    prefetch.setNowForTests({});
    const std::size_t rest = prefetch.pump(&vfs, 5000.0);
    checkPrefetch(rest == 3, "resumed pump must finish the queue");
    checkPrefetch(prefetch.complete(), "queue must be complete after resume");
    checkPrefetch(prefetch.readyAssets() == 4, "ready counter wrong after resume");
    TEST_PASS("prefetch time-budget deferral");
}

Rowl::Core::ChapterLoader makeFiveChapterLoader() {
    nlohmann::json index;
    index["format_version"] = 5;
    index["start_node_id"] = 1;
    nlohmann::json chapters = nlohmann::json::array();
    for (int c = 1; c <= 5; ++c) {
        chapters.push_back({{"id", "ch" + std::to_string(c)},
                            {"title", "Chapter " + std::to_string(c)},
                            {"order", c},
                            {"start_node_id", (c - 1) * 10 + 1}});
    }
    index["chapters"] = chapters;

    Rowl::Core::ChapterLoader loader;
    std::string error;
    checkPrefetch(loader.loadIndexJson(index.dump(), error), "index load failed: " + error);
    for (int c = 1; c <= 5; ++c) {
        nlohmann::json file;
        file["format_version"] = 5;
        file["chapter_id"] = "ch" + std::to_string(c);
        nlohmann::json nodes = nlohmann::json::array();
        for (int n = 0; n < 10; ++n) {
            const int id = (c - 1) * 10 + 1 + n;
            nlohmann::json node;
            node["id"] = id;
            node["chapter_id"] = "ch" + std::to_string(c);
            node["speaker"] = "S";
            node["dialogue"] = "line " + std::to_string(id);
            if (n == 9 && c < 5) {
                node["next_nodes"] = {{{"id", id + 1}, {"label", "next"}}};
            }
            nodes.push_back(node);
        }
        file["nodes"] = nodes;
        checkPrefetch(loader.appendChapterFileJson(file.dump(), error),
                      "chapter file load failed: " + error);
    }
    return loader;
}

void testChapterWindow() {
    TEST_SECTION("Chapter window (active+-1 resident, +-2 unloaded)");
    Rowl::Core::ChapterLoader loader = makeFiveChapterLoader();
    std::string error;
    checkPrefetch(loader.setActiveChapter("ch3", error), "setActive failed: " + error);
    const auto loaded = loader.loadedChapters();
    checkPrefetch(loaded.size() == 3, "window must hold 3 chapters");
    checkPrefetch(loaded[0] == "ch2" && loaded[1] == "ch3" && loaded[2] == "ch4",
                  "window must be {ch2,ch3,ch4}");
    checkPrefetch(loader.residentNodeCount() == 30, "resident must be 30 nodes");
    checkPrefetch(loader.totalNodeCount() == 50, "total must be 50 nodes");
    // +-2 disi: ch1/ch5 resident degil.
    checkPrefetch(loader.chapterNodeIds("ch1").size() == 10, "bucket ch1 must keep 10 ids");
    const auto parsed = nlohmann::json::parse(loader.loadedChaptersJson());
    checkPrefetch(parsed["active"] == "ch3", "active chapter wrong in JSON");
    checkPrefetch(parsed["neighbors"].size() == 2, "neighbors wrong in JSON");
    checkPrefetch(parsed["resident_nodes"] == 30, "resident count wrong in JSON");
    checkPrefetch(parsed["total_nodes"] == 50, "total count wrong in JSON");
    TEST_PASS("chapter window");
}

void testTransparentReload() {
    TEST_SECTION("Chapter transparent reload (unload->access->restored + diagnostic)");
    Rowl::Core::ChapterLoader loader = makeFiveChapterLoader();
    std::string error;
    checkPrefetch(loader.setActiveChapter("ch3", error), "setActive failed: " + error);
    checkPrefetch(loader.residentNodeCount() == 30, "precondition: 30 resident");

    // ch1 unload (pencere disi zaten) + erisim -> seffaf reload.
    checkPrefetch(loader.unloadChapter("ch1", error), "unload ch1 failed: " + error);
    const Rowl::Core::StoryNode* node = loader.node(3);
    checkPrefetch(node != nullptr, "unloaded node must transparently reload");
    checkPrefetch(node->id == 3, "reloaded node id wrong");
    checkPrefetch(loader.residentNodeCount() == 40, "reload must bring the bucket back");
    checkPrefetch(loader.lastDiagnostic().find("transparent reload") != std::string::npos,
                  "reload must record a diagnostic, got: " + loader.lastDiagnostic());

    // Bilinmeyen node fail-closed: nullptr + tani, durum bozulmaz.
    const std::size_t before = loader.residentNodeCount();
    checkPrefetch(loader.node(999999) == nullptr, "unknown node must return null");
    checkPrefetch(loader.residentNodeCount() == before, "unknown access must not mutate");
    checkPrefetch(!loader.lastDiagnostic().empty(), "unknown access must diagnose");

    // Aktif chapter unload EDILEMEZ (fail-closed).
    checkPrefetch(!loader.unloadChapter("ch3", error), "active unload must be refused");
    checkPrefetch(!error.empty(), "active unload refusal needs an error");
    checkPrefetch(loader.activeChapterId() == "ch3", "active must be unchanged");

    // Sinir sorgusu: ch3'un ilk node'u chapter start; ch2->ch3 gecisi sinir.
    checkPrefetch(loader.isChapterBoundaryNode(21), "chapter start must be a boundary");
    checkPrefetch(loader.isChapterBoundaryNode(20), "cross-chapter edge must be a boundary");
    checkPrefetch(!loader.isChapterBoundaryNode(22), "inner node must not be a boundary");
    checkPrefetch(!loader.isChapterBoundaryNode(999999), "unknown node must not be a boundary");
    TEST_PASS("chapter transparent reload + boundaries");
}

void testLegacySingleFile() {
    TEST_SECTION("Chapter legacy single-file graph");
    nlohmann::json doc;
    doc["start_node_id"] = 1;
    doc["nodes"] = nlohmann::json::array();
    doc["nodes"].push_back({{"id", 1}, {"speaker", "S"}, {"dialogue", "hi"}, {"next_id", 2}});
    doc["nodes"].push_back({{"id", 2}, {"speaker", "S"}, {"dialogue", "yo"}});

    Rowl::Core::ChapterLoader loader;
    std::string error;
    checkPrefetch(loader.loadFullGraphJson(doc.dump(), error), "legacy load failed: " + error);
    checkPrefetch(loader.isLegacySingleGraph(), "legacy flag must be set");
    checkPrefetch(!loader.hasChapters(), "legacy graph must report no chapters");
    checkPrefetch(loader.residentNodeCount() == 2, "legacy nodes must all be resident");
    checkPrefetch(loader.node(2) != nullptr, "legacy node access failed");
    checkPrefetch(!loader.isChapterBoundaryNode(1), "legacy graph has no boundaries");
    checkPrefetch(!loader.setActiveChapter("nope", error), "legacy setActive must refuse");
    TEST_PASS("chapter legacy single-file");
}

void testLargeChapterMemoryBound() {
    TEST_SECTION("Chapter memory bound (10.000 nodes, windowed residency)");
    nlohmann::json index;
    index["format_version"] = 5;
    index["start_node_id"] = 1;
    nlohmann::json chapters = nlohmann::json::array();
    constexpr int kChapters = 100;
    constexpr int kPerChapter = 100;
    for (int c = 0; c < kChapters; ++c) {
        chapters.push_back({{"id", "k" + std::to_string(c)},
                            {"title", "K" + std::to_string(c)},
                            {"order", c},
                            {"start_node_id", c * kPerChapter + 1}});
    }
    index["chapters"] = chapters;
    Rowl::Core::ChapterLoader loader;
    std::string error;
    checkPrefetch(loader.loadIndexJson(index.dump(), error), "big index failed: " + error);
    for (int c = 0; c < kChapters; ++c) {
        nlohmann::json file;
        file["format_version"] = 5;
        file["chapter_id"] = "k" + std::to_string(c);
        nlohmann::json nodes = nlohmann::json::array();
        for (int n = 0; n < kPerChapter; ++n) {
            const int id = c * kPerChapter + 1 + n;
            nlohmann::json node;
            node["id"] = id;
            node["chapter_id"] = "k" + std::to_string(c);
            node["speaker"] = "S";
            node["dialogue"] = "line " + std::to_string(id);
            node["background"] = "bg.png";
            nodes.push_back(node);
        }
        file["nodes"] = nodes;
        checkPrefetch(loader.appendChapterFileJson(file.dump(), error),
                      "big chapter file failed: " + error);
    }
    checkPrefetch(loader.totalNodeCount() == 10000, "total must be 10000");
    checkPrefetch(loader.setActiveChapter("k50", error), "setActive k50 failed: " + error);
    // Pencere: k49+k50+k51 = 300 node; uzak 9.700 node unload.
    checkPrefetch(loader.residentNodeCount() == 300,
                  "resident must be 300, got " + std::to_string(loader.residentNodeCount()));
    const auto loaded = loader.loadedChapters();
    checkPrefetch(loaded.size() == 3 && loaded[1] == "k50", "window must center on k50");
    // Uzak erisim seffaf reload uretir ama pencere butcesi korunur mantigi:
    // reload sonrasi resident 400 olur, tani vardir.
    checkPrefetch(loader.node(1) != nullptr, "far node must reload");
    checkPrefetch(loader.residentNodeCount() == 400, "reload must add exactly one bucket");
    checkPrefetch(loader.lastDiagnostic().find("transparent reload") != std::string::npos,
                  "far reload must diagnose");
    TEST_PASS("chapter memory bound 10k");
}

void testCapis() {
    TEST_SECTION("Prefetch+chapters C API vectors");
    uint64_t caps = 0;
    checkPrefetch(RowlEngine_GetCapabilities(&caps) == ROWL_RESULT_OK,
                  "GetCapabilities failed");
    checkPrefetch((caps & UINT64_C(65536)) != 0, "capability 65536 must be set");
    checkPrefetch((caps & UINT64_C(32768)) != 0, "capability 32768 must stay set");
    checkPrefetch((caps & UINT64_C(16384)) != 0, "capability 16384 must stay set");
    checkPrefetch((caps & UINT64_C(8192)) != 0, "capability 8192 must stay set");
    checkPrefetch((caps & UINT64_C(1)) != 0, "capability 1 must stay set");

    // Null-handle vektorleri (hepsi fail-closed).
    checkPrefetch(RowlEngine_LoadChapterIndexJson(nullptr, "{}") == ROWL_RESULT_INVALID_HANDLE,
                  "index null-handle");
    checkPrefetch(RowlEngine_AppendChapterFileJson(nullptr, "{}") == ROWL_RESULT_INVALID_HANDLE,
                  "file null-handle");
    checkPrefetch(RowlEngine_LoadChapter(nullptr, "ch1") == ROWL_RESULT_INVALID_HANDLE,
                  "load null-handle");
    checkPrefetch(RowlEngine_UnloadChapter(nullptr, "ch1") == ROWL_RESULT_INVALID_HANDLE,
                  "unload null-handle");
    uint32_t required = 0;
    checkPrefetch(RowlEngine_GetLoadedChaptersJson(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "loaded-json null-handle");
    checkPrefetch(RowlEngine_IsChapterBoundaryNode(nullptr, 1) == 0, "boundary null-handle");
    checkPrefetch(RowlEngine_PrefetchChapterAssets(nullptr, "ch1", 0) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "prefetch null-handle");
    checkPrefetch(RowlEngine_PumpPrefetch(nullptr, 4.0f) == 0, "pump null-handle");
    checkPrefetch(RowlEngine_GetPrefetchProgressJson(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "progress null-handle");

    RowlEngineHandle handle = RowlEngine_Create();
    checkPrefetch(handle != nullptr, "Create failed");
    checkPrefetch(RowlEngine_Init(handle, 1920, 1080, 0) == 1, "Init failed");

    // Bilinmeyen chapter + bos index fail-closed.
    checkPrefetch(RowlEngine_LoadChapter(handle, "nope") == ROWL_RESULT_INVALID_ARGUMENT,
                  "unknown chapter must be INVALID_ARGUMENT");
    checkPrefetch(RowlEngine_UnloadChapter(handle, "nope") == ROWL_RESULT_INVALID_ARGUMENT,
                  "unknown unload must be INVALID_ARGUMENT");
    checkPrefetch(RowlEngine_LoadChapterIndexJson(handle, nullptr) ==
                      ROWL_RESULT_INVALID_ARGUMENT,
                  "null index JSON must be INVALID_ARGUMENT");
    checkPrefetch(RowlEngine_LoadChapterIndexJson(handle, "{oops") ==
                      ROWL_RESULT_PARSE_ERROR,
                  "malformed index must be PARSE_ERROR");
    checkPrefetch(RowlEngine_LoadChapterIndexJson(handle, "{}") ==
                      ROWL_RESULT_VALIDATION_ERROR,
                  "index without chapters must be VALIDATION_ERROR");

    // Oversized-input deseni (256 KiB + 1): chapterId ve index reddedilir.
    const std::string huge(262144 + 1, 'x');
    checkPrefetch(RowlEngine_LoadChapter(handle, huge.c_str()) == ROWL_RESULT_INVALID_ARGUMENT,
                  "oversized chapter id must be INVALID_ARGUMENT");
    checkPrefetch(RowlEngine_LoadChapterIndexJson(handle, huge.c_str()) ==
                      ROWL_RESULT_INVALID_ARGUMENT,
                  "oversized index must be INVALID_ARGUMENT");
    checkPrefetch(RowlEngine_PrefetchChapterAssets(handle, huge.c_str(), 0) ==
                      ROWL_RESULT_INVALID_ARGUMENT,
                  "oversized prefetch chapter must be INVALID_ARGUMENT");

    // Index + chapter dosyalariyla besle; caller-buffer sozlesmesi.
    nlohmann::json index;
    index["format_version"] = 5;
    index["start_node_id"] = 1;
    index["chapters"] = nlohmann::json::array();
    index["chapters"].push_back({{"id", "a"}, {"order", 0}, {"start_node_id", 1}});
    index["chapters"].push_back({{"id", "b"}, {"order", 1}, {"start_node_id", 3}});
    checkPrefetch(RowlEngine_LoadChapterIndexJson(handle, index.dump().c_str()) == ROWL_RESULT_OK,
                  "C API index load failed");
    nlohmann::json fileA;
    fileA["chapter_id"] = "a";
    fileA["nodes"] = nlohmann::json::array();
    fileA["nodes"].push_back({{"id", 1},
                              {"chapter_id", "a"},
                              {"speaker", "S"},
                              {"dialogue", "hi"},
                              {"next_nodes", {{{"id", 2}, {"label", "n"}}}}});
    fileA["nodes"].push_back(
        {{"id", 2}, {"chapter_id", "a"}, {"speaker", "S"}, {"dialogue", "mid"}});
    nlohmann::json fileB;
    fileB["chapter_id"] = "b";
    fileB["nodes"] = nlohmann::json::array();
    fileB["nodes"].push_back(
        {{"id", 3}, {"chapter_id", "b"}, {"speaker", "S"}, {"dialogue", "yo"}});
    checkPrefetch(RowlEngine_AppendChapterFileJson(handle, fileA.dump().c_str()) ==
                      ROWL_RESULT_OK,
                  "C API file A failed");
    checkPrefetch(RowlEngine_AppendChapterFileJson(handle, fileB.dump().c_str()) ==
                      ROWL_RESULT_OK,
                  "C API file B failed");

    const std::string loadedJson =
        queryJson(handle, RowlEngine_GetLoadedChaptersJson);
    const auto loaded = nlohmann::json::parse(loadedJson);
    checkPrefetch(loaded["active"] == "a", "C API active must default to first chapter");

    // Aktif unload red; sinir: node 2'nin successor'u (yok) degil, node 1 sinir
    // (successor ayni chapter) degil... node 1'in successor'u node 2 (a) -> sinir
    // degil; chapter start node 1 -> SINIR (start kurali).
    checkPrefetch(RowlEngine_UnloadChapter(handle, "a") == ROWL_RESULT_INVALID_ARGUMENT,
                  "C API active unload must be refused");
    checkPrefetch(RowlEngine_IsChapterBoundaryNode(handle, 1) == 1,
                  "chapter start must be a boundary via C API");
    checkPrefetch(RowlEngine_IsChapterBoundaryNode(handle, 2) == 0,
                  "inner node must not be a boundary via C API");
    checkPrefetch(RowlEngine_IsChapterBoundaryNode(handle, 424242) == 0,
                  "unknown node must not be a boundary via C API");

    // Butce clamp: 1 TiB istek 128 MiB'a iner, red degil.
    checkPrefetch(RowlEngine_PrefetchChapterAssets(handle, "a", UINT64_C(1099511627776)) ==
                      ROWL_RESULT_OK,
                  "clamped prefetch must stay OK");
    const std::string progressJson =
        queryJson(handle, RowlEngine_GetPrefetchProgressJson);
    const auto progress = nlohmann::json::parse(progressJson);
    checkPrefetch(progress["budget_bytes"] == Rowl::Core::kPrefetchMaxBudgetBytes,
                  "budget must clamp to 128 MiB");
    checkPrefetch(progress.contains("complete"), "progress must carry complete");

    RowlEngine_Destroy(handle);
    TEST_PASS("prefetch+chapters C API vectors");
}

void testEnginePrefetchEndToEnd() {
    TEST_SECTION("Prefetch end-to-end through engine + VFS");
    const std::string project = makeTempDir("e2e");
    std::filesystem::create_directories(project + "/Assets");
    writeFileBytes(project + "/Assets/bg.png", 512);
    writeFileBytes(project + "/Assets/hero.png", 256);
    writeFileBytes(project + "/Assets/bgm.ogg", 1024);
    // voice.ogg bilerek EKSIK: sayaca isler, prefetch durmaz.

    nlohmann::json graph;
    graph["start_node_id"] = 1;
    nlohmann::json nodes = nlohmann::json::array();
    nlohmann::json node1;
    node1["id"] = 1;
    node1["speaker"] = "S";
    node1["dialogue"] = "hi";
    node1["background"] = "bg.png";
    node1["components"] = nlohmann::json::array();
    node1["components"].push_back({{"type", "background"},
                                   {"data", {{"texture", "bg.png"}}},
                                   {"enabled", true}});
    node1["components"].push_back({{"type", "character"},
                                   {"data",
                                    {{"sprite", "hero.png"},
                                     {"layers", {{"face", "hero.png"}}}}},
                                   {"enabled", true}});
    node1["components"].push_back(
        {{"type", "audio"}, {"data", {{"bgm_track", "bgm.ogg"}, {"voice_track", "voice.ogg"}}}, {"enabled", true}});
    node1["next_nodes"] = {{{"id", 2}, {"label", "next"}}};
    nlohmann::json node2;
    node2["id"] = 2;
    node2["speaker"] = "S";
    node2["dialogue"] = "yo";
    node2["background"] = "bg.png";
    node2["components"] = nlohmann::json::array();
    nodes.push_back(node1);
    nodes.push_back(node2);
    graph["nodes"] = nodes;
    writeFile(project + "/graph.json", graph.dump());

    RowlEngineHandle handle = RowlEngine_Create();
    checkPrefetch(handle != nullptr, "e2e Create failed");
    checkPrefetch(RowlEngine_Init(handle, 1920, 1080, 0) == 1, "e2e Init failed");
    RowlEngine_SetProjectDirectory(handle, project.c_str());
    RowlEngine_LoadStoryGraph(handle, (project + "/graph.json").c_str());
    checkPrefetch(RowlEngine_GetCurrentNodeId(handle) == 1, "e2e graph did not load");

    // Bos chapterId = aktif (legacy: aktif node + successor).
    checkPrefetch(RowlEngine_PrefetchChapterAssets(handle, nullptr, 0) == ROWL_RESULT_OK,
                  "e2e prefetch trigger failed");
    RowlEngine_PumpPrefetch(handle, 5000.0f);
    const std::string progressJson =
        queryJson(handle, RowlEngine_GetPrefetchProgressJson);
    const auto progress = nlohmann::json::parse(progressJson);
    checkPrefetch(progress["ready_assets"] == 3,
                  "e2e ready must be 3 (bg, hero, bgm), got " +
                      progress["ready_assets"].dump());
    checkPrefetch(progress["missing_assets"] == 1, "e2e missing must be 1 (voice.ogg)");
    checkPrefetch(progress["complete"] == true, "e2e queue must be complete");
    checkPrefetch(progress["ready_bytes"] == 512 + 256 + 1024, "e2e ready bytes wrong");
    checkPrefetch(progress["missing_paths"].size() == 1, "e2e missing paths wrong");

    RowlEngine_Destroy(handle);
    TEST_PASS("prefetch end-to-end through engine + VFS");
}

} // namespace

void test_prefetch_chapters() {
    testAssetCollection();
    testByteBudgetCut();
    testTimeBudgetDeferral();
    testChapterWindow();
    testTransparentReload();
    testLegacySingleFile();
    testLargeChapterMemoryBound();
    testCapis();
    testEnginePrefetchEndToEnd();
}
