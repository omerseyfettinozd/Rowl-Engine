/**
 * test_boot_story_diagnostics.cpp — #118/#119/#120 kilidi: grafsız veya
 * bozuk-grafik boot ayırt edilebilir kalır.
 *
 * Kilitlenen davranışlar (engine.cpp initialize + loadStoryGraphFile):
 *  1. #118/#119: hikayesiz boot'ta Init yine 1 döner (bare-init
 *     sözleşmesi) ama birincil kanal OK'a dönmez — boot hükmü
 *     init-imzasıyla korunur (FileNotFound) ve story-error kanalı dolu
 *     kalır. Eski kod blanket setSuccess("init") ile teşhisi silip
 *     Init=1 + OK döndürüyordu (storyless boot sağlıklı sanılıyordu).
 *  2. #120: bozuk ilk fiziksel aday (exists-geçen ama parse-hatalı)
 *     zinciri kesmez — ikinci aday denenir. Bozuk birinci + geçerli
 *     ikinci grafikte ikinci grafın start node'u yüklenir.
 *  3. #120: fiziksel adayların tamamı ıskalarsa active-story overlay
 *     denenir — bozuk grafik + geçerli overlay'de overlay sahnesi
 *     uygulanır (boş sahne değil).
 *
 * Bilerek-boz: initialize'daki storyLoaded ayrımı stash'lenirse (eski
 * blanket setSuccess) test 1; fiziksel daldaki erken return geri
 * gelirse test 2/3 kırmızıya döner.
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

constexpr const char* kSection = "boot-story-diagnostics";

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) rowlLockFail(kSection, "could not stage " + path.string());
    out << content;
    out.close();
    if (!out) rowlLockFail(kSection, "could not flush " + path.string());
}

// Süreci geçici dizine taşır, çıkışta CWD'yi geri yükler (RAII).
class TempCwd {
public:
    TempCwd() {
        saved_ = fs::current_path();
        root_ = fs::temp_directory_path() /
                ("rowl_bootdiag_" + std::to_string(
                    static_cast<unsigned long long>(::getpid())));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        if (ec) rowlLockFail(kSection, "could not stage temp dir");
        fs::current_path(root_, ec);
        if (ec) rowlLockFail(kSection, "could not chdir to temp dir");
    }
    ~TempCwd() {
        std::error_code ec;
        fs::current_path(saved_, ec);
        fs::remove_all(root_, ec);
    }
    TempCwd(const TempCwd&) = delete;
    TempCwd& operator=(const TempCwd&) = delete;
private:
    fs::path saved_;
    fs::path root_;
};

std::string graphJson(uint64_t startId) {
    return std::string("{\"format_version\":4,\"start_node_id\":") +
           std::to_string(startId) + ",\"nodes\":[{\"id\":" +
           std::to_string(startId) + ",\"dialogue\":\"Hi\"}]}";
}

void checkResultCode(const char* name, RowlEngineHandle h, int32_t want) {
    const int32_t got = RowlEngine_GetLastResultCode(h);
    if (got != want) {
        rowlLockFail(kSection, std::string(name) + ": expected last-result " +
                     std::to_string(want) + ", got " + std::to_string(got));
    }
}

} // namespace

void test_boot_story_diagnostics() {
    TEST_SECTION("Boot Story Diagnostics (#118/#119/#120)");

    // 1. Hikayesiz boot: Init=1 (sözleşme) + FileNotFound(3) + dolu
    // story-error kanalı. Eski kod burada 0 (OK) döndürüyordu.
    {
        TempCwd cwd;
        RowlEngineHandle h = RowlEngine_Create();
        if (h == nullptr) rowlLockFail(kSection, "Create returned null");
        if (RowlEngine_Init(h, 320, 180, 0) != 1)
            rowlLockFail(kSection, "storyless Init must still return 1 (bare-init contract)");
        checkResultCode("storyless-boot", h, 3 /* FileNotFound */);
        const char* graphError = RowlEngine_GetLastStoryGraphError(h);
        if (graphError == nullptr || std::string(graphError).empty())
            rowlLockFail(kSection, "storyless boot left the story-error channel empty");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Storyless boot stays distinguishable (Init=1, FileNotFound, graph error set)");

    // 2. Bozuk birinci + geçerli ikinci fiziksel aday: ikinci yüklenir
    // (start 707). Eski erken-return burada storyless bırakıyordu.
    {
        TempCwd cwd;
        writeFile("Assets/json/full_story_graph.json", "{ not json at all");
        writeFile("Assets/full_story_graph.json", graphJson(707));
        RowlEngineHandle h = RowlEngine_Create();
        if (h == nullptr) rowlLockFail(kSection, "Create returned null");
        if (RowlEngine_Init(h, 320, 180, 0) != 1)
            rowlLockFail(kSection, "fallback Init must return 1");
        checkResultCode("fallback-boot", h, 0 /* OK */);
        if (RowlEngine_GetCurrentNodeId(h) != 707)
            rowlLockFail(kSection, "second physical candidate was not tried after corrupt first");
        const char* graphError = RowlEngine_GetLastStoryGraphError(h);
        if (graphError != nullptr && std::string(graphError).length() > 0)
            rowlLockFail(kSection, "successful fallback left a stale story error");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Corrupt first candidate falls through to the second");

    // 3. Bozuk grafik + geçerli overlay: overlay sahnesi uygulanır.
    // Eski erken-return loadActiveStoryFile'a hiç düşmüyordu.
    {
        TempCwd cwd;
        writeFile("Assets/json/full_story_graph.json", "{ not json at all");
        writeFile("Assets/json/active_story.json",
                  "{\"node_id\":0,\"speaker\":\"OverlayGhost\",\"dialogue\":\"hi\"}");
        RowlEngineHandle h = RowlEngine_Create();
        if (h == nullptr) rowlLockFail(kSection, "Create returned null");
        if (RowlEngine_Init(h, 320, 180, 0) != 1)
            rowlLockFail(kSection, "overlay Init must return 1");
        char speaker[256] = {};
        uint32_t needed = 0;
        if (RowlEngine_GetSpeakerUtf8(h, speaker, sizeof(speaker), &needed) != ROWL_RESULT_OK)
            rowlLockFail(kSection, "overlay speaker unreadable");
        if (std::string(speaker) != "OverlayGhost")
            rowlLockFail(kSection, "active-story overlay was skipped after corrupt graph (got '" +
                         std::string(speaker) + "')");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Corrupt graph falls through to the active-story overlay");
}
