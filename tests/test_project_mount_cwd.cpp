/**
 * test_project_mount_cwd.cpp — oynatıcı cwd = proje kökü iken proje
 * VFS'e bağlanır.
 *
 * Hata (2026-10-22, "Son Fener" / second_signal): `cd <proje> &&
 * rowl_player --project <proje>` (paketli run_game.sh da cd yapar) VFS'i
 * hiç bağlamıyordu — "0 mount points" sonrası "Remounting" yok, tüm
 * görseller "VFS File not found", yazı tipi bitmap yedeğe düşüyordu.
 *
 * Kök neden: Init cwd'deki Assets/json/full_story_graph.json'u fiziksel
 * yedekle yükleyip başlangıç düğümünü sunar. Düğümün giriş etkileri
 * (variable bileşeni, script on_enter) stepId'yi 1'in üstüne çıkarır;
 * SetProjectDirectory'nin D2 (#113) "oynanmış oturum" kapısı `stepId > 1`
 * gördüğü için kimse oynamamışken bağlamayı StateError ile reddeder.
 * Oynatıcı sonucu okumaz → sessiz boş VFS.
 *
 * Kilit:
 *  1. cwd = proje kökü + giriş etkili başlangıç düğümü: Init sonrası
 *     SetProjectDirectory kabul edilir (StateError değil), VFS bağlanır
 *     (mountCount > 0, proje grafiği VFS'ten çözülür).
 *  2. D2 korunur: aynı projede gerçekten ilerlenmiş oturum (Advance) ve
 *     başlangıç düğümünde host değişken yazımı hâlâ reddedilir.
 *
 * Bilerek-boz: kapıdaki başlangıç-sunumu tabanı `> 1` sabitine geri
 * alınırsa test 1 kırmızıya döner.
 */
#include "rowl_test_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

namespace fs = std::filesystem;

constexpr const char* kSection = "project-mount-cwd";

void writeFile(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) rowlLockFail(kSection, "could not stage " + path.string());
    out << content;
    out.close();
    if (!out) rowlLockFail(kSection, "could not flush " + path.string());
}

// Süreci geçici proje köküne taşır, çıkışta CWD'yi geri yükler (RAII).
class ProjectCwd {
public:
    ProjectCwd() {
        saved_ = fs::current_path();
        const uint64_t stamp =
            static_cast<uint64_t>(std::chrono::steady_clock::now()
                                      .time_since_epoch().count());
        const uint64_t seq = s_counter.fetch_add(1, std::memory_order_relaxed);
        root_ = fs::temp_directory_path() /
                ("rowl_mountcwd_" + std::to_string(stamp) + "_" +
                 std::to_string(seq));
        std::error_code ec;
        fs::remove_all(root_, ec);
        fs::create_directories(root_, ec);
        if (ec) rowlLockFail(kSection, "could not stage project dir");
        fs::current_path(root_, ec);
        if (ec) rowlLockFail(kSection, "could not chdir to project dir");
    }
    ~ProjectCwd() {
        std::error_code ec;
        fs::current_path(saved_, ec);
        fs::remove_all(root_, ec);
    }
    ProjectCwd(const ProjectCwd&) = delete;
    ProjectCwd& operator=(const ProjectCwd&) = delete;
    const fs::path& root() const { return root_; }
private:
    fs::path saved_;
    fs::path root_;
    static std::atomic<uint64_t> s_counter;
};

inline std::atomic<uint64_t> ProjectCwd::s_counter{0};

// second_signal başlangıç düğümünün özü: variable + script on_enter giriş
// etkileri, ardından ilerlenebilir ikinci düğüm.
constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "title": "Static",
     "objects": [{"id": "dlg1", "name": "Dialogue", "is_active": true,
       "components": [
         {"type": "variable", "id": "var1", "enabled": true,
          "data": {"key": "station_awake", "value": "yes", "operation": "set"}},
         {"type": "script", "id": "script1", "enabled": true,
          "data": {"code": "function on_enter() rowl.var_set('entered', 'yes') end"}},
         {"type": "dialogue", "id": "dlg1c", "enabled": true,
          "data": {"speaker": "Margot", "dialogue": "Röle uyanıyor.",
                   "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 2}]},
    {"id": 2, "dialogue": "İkinci"}
  ]
})json";

void stageProject(const fs::path& root) {
    writeFile(root / "Assets" / "json" / "full_story_graph.json", kGraph);
    writeFile(root / "project.rowlproj",
              R"json({"name": "MountCwd", "version": "1.0.0"})json");
}

nlohmann::json vfsStatus(RowlEngineHandle h, const char* probe) {
    std::vector<char> buffer(64 * 1024);
    const int written = RowlEngine_QueryVfsStatusJson(
        h, probe, buffer.data(), static_cast<int>(buffer.size()));
    if (written <= 0) rowlLockFail(kSection, "QueryVfsStatusJson failed");
    auto status = nlohmann::json::parse(std::string(buffer.data()), nullptr, false);
    if (!status.is_object()) rowlLockFail(kSection, "QueryVfsStatusJson returned no JSON object");
    return status;
}

void initAtProjectRoot(RowlEngineHandle h, const char* what) {
    if (h == nullptr) rowlLockFail(kSection, std::string(what) + ": Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail(kSection, std::string(what) + ": Init must return 1");
    // Ön koşul: Init cwd'deki grafiği yükleyip başlangıçta durur.
    if (RowlEngine_GetCurrentNodeId(h) != 1)
        rowlLockFail(kSection, std::string(what) +
                     ": Init should boot the cwd graph at start node 1");
}

} // namespace

void test_project_mount_cwd() {
    TEST_SECTION("Project Mount With CWD = Project Root");

    // 1. Oynatıcı akışı: cwd = proje, Init, SetProjectDirectory(proje).
    {
        ProjectCwd cwd;
        stageProject(cwd.root());
        RowlEngineHandle h = RowlEngine_Create();
        initAtProjectRoot(h, "player-boot");
        const auto before = vfsStatus(h, nullptr);
        if (before.value("mountCount", 0) != 0)
            rowlLockFail(kSection, "precondition: no project mount before SetProjectDirectory");

        const std::string rootUtf8 = cwd.root().string();
        RowlEngine_SetProjectDirectory(h, rootUtf8.c_str());
        const int32_t code = RowlEngine_GetLastResultCode(h);
        if (code == 11 /* StateError */) {
            rowlLockFail(kSection,
                         "unplayed boot at project root rejected as mid-session (stepId=" +
                         std::to_string(RowlEngine_GetCurrentStepId(h)) +
                         "); start-node entry effects are not player progress");
        }
        const auto after = vfsStatus(h, "json/full_story_graph.json");
        if (after.value("mountCount", 0) == 0)
            rowlLockFail(kSection, "project root was not mounted into the VFS");
        const auto query = after.value("query", nlohmann::json::object());
        if (query.value("diagnosis", std::string()) != "OK")
            rowlLockFail(kSection, "project graph does not resolve through the VFS: " +
                         query.value("diagnosis", std::string("?")));
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Player boot with cwd = project root mounts the project VFS");

    // 2. D2 (#113) korunur: gerçek ilerleme hâlâ reddedilir.
    {
        ProjectCwd cwd;
        stageProject(cwd.root());
        const std::string rootUtf8 = cwd.root().string();

        RowlEngineHandle advanced = RowlEngine_Create();
        initAtProjectRoot(advanced, "advanced");
        // İlk çağrı yazılıyorsa satırı tamamlar (MS-6), sonraki ilerler.
        RowlEngine_AdvanceNode(advanced, 0);
        if (RowlEngine_GetCurrentNodeId(advanced) == 1) RowlEngine_AdvanceNode(advanced, 0);
        if (RowlEngine_GetCurrentNodeId(advanced) != 2)
            rowlLockFail(kSection, "AdvanceNode must move to node 2");
        RowlEngine_SetProjectDirectory(advanced, rootUtf8.c_str());
        if (RowlEngine_GetLastResultCode(advanced) != 11)
            rowlLockFail(kSection, "advanced session must still reject SetProjectDirectory");
        RowlEngine_Shutdown(advanced);
        RowlEngine_Destroy(advanced);

        RowlEngineHandle hostVar = RowlEngine_Create();
        initAtProjectRoot(hostVar, "host-variable");
        RowlEngine_SetVariable(hostVar, "met_operator", "yes");
        RowlEngine_SetProjectDirectory(hostVar, rootUtf8.c_str());
        if (RowlEngine_GetLastResultCode(hostVar) != 11)
            rowlLockFail(kSection,
                         "variable progress at the start node must still reject SetProjectDirectory");
        RowlEngine_Shutdown(hostVar);
        RowlEngine_Destroy(hostVar);
    }
    TEST_PASS("Real progress (advance / host variable) still rejects the mount");
}
