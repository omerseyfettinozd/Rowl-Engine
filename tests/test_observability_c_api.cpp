/**
 * test_observability_c_api.cpp — RowlEye gözlem C API kilidi.
 *
 * Kapsam: engine/src/c_api_observability.cpp (7 sembol) +
 * Logger halka tamponu/callback'i. rowl_native_tests gövdesindedir ki
 * gece ASan/UBSan ve TSan tam-süit işleri bu yüzeyi de koşsun (ayrı
 * ikili sanitizer -R desenine girmezdi).
 *
 * Bacaklar:
 *   (A) ölü/bozuk/null handle: 0 döner, arabelleğe dokunulmaz.
 *   (B) yabancı thread: canlı-sahipli handle'da 0 + WRONG_THREAD damgası;
 *       InjectInput motoru İLERLETMEZ (copyEngineAnyThread çağrı hakkı vermez).
 *   (C) boyut protokolü: null→gerekli boyut; küçük→-gerekli + NUL; tam→uzunluk.
 *   (D) QueryWorld filtresi: bozuk JSON / bool-olmayan alan / seq_since
 *       reddedilir (tip hatası seq_since reddini yutmamalı).
 *   (E) InjectInput atomikliği: geçersiz olay içeren dizi HİÇBİR olayı
 *       uygulamaz; negatif adım reddedilir.
 *   (F) log: callback gerçek native logu alır, null'da susar; recent-logs
 *       katı JSON'dur (tırnak/kontrol/geçersiz UTF-8 dahil); eşzamanlı
 *       log + callback değişimi yarışsızdır (TSan).
 */
#include "rowl_test_harness.hpp"

#include <mutex>
#include <nlohmann/json.hpp>

namespace {

void obsFail(const std::string& message) {
    rowlLockFail("observability-c-api", message);
}

void check(bool condition, const std::string& what) {
    if (!condition) obsFail(what);
}

// Koşuya özgü dizin: sabit /tmp yolu paralel koşuları birbirine bağlar.
std::string makeRunRoot() {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_observability_"
         << std::chrono::steady_clock::now().time_since_epoch().count() << "_"
         << (++counter);
    auto dir = std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

RowlEngineHandle makeTwoNodeEngine(const std::string& root) {
    RowlEngineHandle h = RowlEngine_Create();
    check(h != nullptr, "setup: RowlEngine_Create returned null");
    RowlEngine_SetProjectDirectory(h, root.c_str());
    check(RowlEngine_Init(h, 320, 180, 0) == 1, "setup: Init(320x180) failed");
    const std::string graphPath = root + "/two_node.json";
    {
        std::ofstream graph(graphPath);
        graph << R"({"format_version":4,"start_node_id":101,"nodes":[)"
                 R"({"id":101,"speaker":"Guide","dialogue":"First.","next_nodes":[{"id":102}]},)"
                 R"({"id":102,"speaker":"Guide","dialogue":"Second."}]})";
    }
    RowlEngine_LoadStoryGraph(h, graphPath.c_str());
    RowlEngine_Step(h, 0.0f);
    check(RowlEngine_GetCurrentNodeId(h) == 101, "setup: start node != 101");
    return h;
}

std::string queryWorld(RowlEngineHandle h, const char* filter = nullptr) {
    const int required = RowlEngine_QueryWorldJson(h, filter, nullptr, 0);
    check(required > 1, "QueryWorld size query failed");
    std::vector<char> buffer(static_cast<size_t>(required), 'x');
    const int written = RowlEngine_QueryWorldJson(h, filter, buffer.data(), required);
    check(written == required - 1, "QueryWorld exact-size write length mismatch");
    check(buffer.back() == '\0', "QueryWorld exact-size not NUL-terminated");
    return std::string(buffer.data());
}

// ── (F) log callback yakalayıcısı ───────────────────────────────────────
std::mutex g_captureMutex;
std::vector<std::string> g_captured;
std::atomic<int> g_callbackHits{0};

void captureLog(int /*level*/, const char* /*levelStr*/, const char* message,
                const char* /*timestamp*/) {
    g_callbackHits.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_captureMutex);
    g_captured.emplace_back(message ? message : "");
}

bool capturedContains(const std::string& needle) {
    std::lock_guard<std::mutex> lock(g_captureMutex);
    for (const auto& line : g_captured) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

nlohmann::json recentLogs(int minLevel, int maxCount) {
    const int required = RowlEngine_GetRecentLogsJson(nullptr, 0, minLevel, maxCount);
    check(required > 1, "recent-logs size query failed");
    std::vector<char> buffer(static_cast<size_t>(required) + 4096, '\0');
    const int written = RowlEngine_GetRecentLogsJson(
        buffer.data(), static_cast<int>(buffer.size()), minLevel, maxCount);
    check(written > 0, "recent-logs write failed");
    auto parsed = nlohmann::json::parse(buffer.data(), nullptr, false);
    check(!parsed.is_discarded(), "recent-logs is not strict JSON");
    check(parsed.is_array(), "recent-logs is not an array");
    return parsed;
}

void legDeadHandles() {
    auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
    RowlEngineHandle dead = RowlEngine_Create();
    check(dead != nullptr, "A: Create returned null");
    RowlEngine_Destroy(dead);

    for (RowlEngineHandle h : {static_cast<RowlEngineHandle>(nullptr), bogus, dead}) {
        char buffer[16];
        std::memset(buffer, 'x', sizeof(buffer));
        check(RowlEngine_QueryWorldJson(h, nullptr, buffer, sizeof(buffer)) == 0,
              "A: dead QueryWorld != 0");
        check(RowlEngine_GetAudioStatsJson(h, buffer, sizeof(buffer)) == 0,
              "A: dead AudioStats != 0");
        check(RowlEngine_QueryVfsStatusJson(h, "x.json", buffer, sizeof(buffer)) == 0,
              "A: dead VfsStatus != 0");
        check(RowlEngine_InjectInputJson(h, R"({"t":"advance"})") == 0,
              "A: dead InjectInput != 0");
        for (char c : buffer) check(c == 'x', "A: dead handle touched the buffer");
    }
    RowlEngine_FreeObservabilityString(nullptr);  // no-op sözleşmesi
    TEST_PASS("observability: dead/bogus/null handle fail-closed, buffer untouched");
}

void legForeignThread(RowlEngineHandle h) {
    char buffer[64];
    std::memset(buffer, 'x', sizeof(buffer));
    int world = -1, audio = -1, vfs = -1, inject = -1;
    std::thread foreign([&] {
        world = RowlEngine_QueryWorldJson(h, nullptr, buffer, sizeof(buffer));
        audio = RowlEngine_GetAudioStatsJson(h, buffer, sizeof(buffer));
        vfs = RowlEngine_QueryVfsStatusJson(h, nullptr, buffer, sizeof(buffer));
        inject = RowlEngine_InjectInputJson(h, R"({"t":"advance"})");
    });
    foreign.join();

    check(world == 0, "B: foreign QueryWorld not rejected");
    check(audio == 0, "B: foreign AudioStats not rejected");
    check(vfs == 0, "B: foreign VfsStatus not rejected");
    check(inject == 0, "B: foreign InjectInput not rejected");
    for (char c : buffer) check(c == 'x', "B: foreign call touched the buffer");
    check(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_WRONG_THREAD,
          "B: foreign call left no WRONG_THREAD stamp");
    RowlEngine_Step(h, 0.0f);
    check(RowlEngine_GetCurrentNodeId(h) == 101, "B: foreign InjectInput advanced the story");
    TEST_PASS("observability: foreign thread rejected with WRONG_THREAD, no mutation");
}

void legSizeProtocol(RowlEngineHandle h) {
    const int required = RowlEngine_QueryWorldJson(h, nullptr, nullptr, 0);
    check(required > 2, "C: size query too small");
    std::vector<char> small(static_cast<size_t>(required - 1), 'x');
    const int rc = RowlEngine_QueryWorldJson(h, nullptr, small.data(), required - 1);
    check(rc == -required, "C: undersized buffer must return -required");
    check(small.back() == '\0', "C: undersized buffer not NUL-terminated");

    const auto world = nlohmann::json::parse(queryWorld(h), nullptr, false);
    check(!world.is_discarded() && world.is_object(), "C: world JSON invalid");
    check(world.value("nodeId", 0ull) == 101, "C: world nodeId != 101");

    for (int (*fn)(RowlEngineHandle, char*, int) : {&RowlEngine_GetAudioStatsJson}) {
        const int need = fn(h, nullptr, 0);
        check(need > 1, "C: audio size query failed");
        std::vector<char> out(static_cast<size_t>(need), 'x');
        check(fn(h, out.data(), need) == need - 1, "C: audio exact write mismatch");
        check(!nlohmann::json::parse(out.data(), nullptr, false).is_discarded(),
              "C: audio JSON invalid");
    }
    const int vfsNeed = RowlEngine_QueryVfsStatusJson(h, "missing.json", nullptr, 0);
    check(vfsNeed > 1, "C: vfs size query failed");
    std::vector<char> vfsOut(static_cast<size_t>(vfsNeed), 'x');
    check(RowlEngine_QueryVfsStatusJson(h, "missing.json", vfsOut.data(), vfsNeed) == vfsNeed - 1,
          "C: vfs exact write mismatch");
    const auto vfs = nlohmann::json::parse(vfsOut.data(), nullptr, false);
    check(!vfs.is_discarded(), "C: vfs JSON invalid");
    check(vfs["query"].value("exists", true) == false, "C: missing probe reported as existing");
    TEST_PASS("observability: size protocol (null/undersized/exact) for world/audio/vfs");
}

void legWorldFilter(RowlEngineHandle h) {
    char buffer[256];
    // Tip hatası (compact:1) seq_since reddini YUTMAMALI.
    check(RowlEngine_QueryWorldJson(h, R"({"compact":1,"seq_since":5})", buffer,
                                    sizeof(buffer)) == 0,
          "D: non-bool compact swallowed the seq_since rejection");
    check(RowlEngine_QueryWorldJson(h, R"({"seq_since":5})", buffer, sizeof(buffer)) == 0,
          "D: seq_since accepted");
    check(RowlEngine_QueryWorldJson(h, R"({"dirty_only":true})", buffer, sizeof(buffer)) == 0,
          "D: dirty_only accepted");
    check(RowlEngine_QueryWorldJson(h, R"({"anomalies":"yes"})", buffer, sizeof(buffer)) == 0,
          "D: non-bool anomalies accepted");
    check(RowlEngine_QueryWorldJson(h, "{ not json", buffer, sizeof(buffer)) == 0,
          "D: malformed filter accepted as default snapshot");
    check(RowlEngine_QueryWorldJson(h, "[1]", buffer, sizeof(buffer)) == 0,
          "D: non-object filter accepted");
    const auto pretty = nlohmann::json::parse(
        queryWorld(h, R"({"compact":false,"anomalies":false})"), nullptr, false);
    check(!pretty.is_discarded() && !pretty.contains("anom"),
          "D: anomalies:false still emitted anom");
    TEST_PASS("observability: world filter rejects malformed/unsupported/mistyped fields");
}

void legInjectAtomic(RowlEngineHandle h) {
    check(RowlEngine_InjectInputJson(h, R"([{"t":"advance"},{"t":"tap","x":"bad"}])") == 0,
          "E: batch with mistyped event accepted");
    check(RowlEngine_InjectInputJson(h, R"([{"t":"advance"},{"t":"bogus"}])") == 0,
          "E: batch with unknown event accepted");
    check(RowlEngine_InjectInputJson(h, R"({"t":"rewind","steps":-1})") == 0,
          "E: negative rewind accepted");
    check(RowlEngine_InjectInputJson(h, R"({"t":"choice","index":-1})") == 0,
          "E: negative choice index accepted");
    check(RowlEngine_InjectInputJson(h, "[]") == 0, "E: empty batch accepted");
    RowlEngine_Step(h, 0.0f);
    check(RowlEngine_GetCurrentNodeId(h) == 101, "E: rejected batch partially applied");

    check(RowlEngine_InjectInputJson(h, R"([{"t":"advance"}])") == 1, "E: valid advance rejected");
    RowlEngine_Step(h, 0.0f);
    check(RowlEngine_GetCurrentNodeId(h) == 102, "E: valid advance did not move to 102");
    TEST_PASS("observability: InjectInput is all-or-nothing, rejects negative counts");
}

void legLogs(RowlEngineHandle h) {
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_captured.clear();
    }
    RowlEngine_SetLogCallback(&captureLog);
    // Gerçek native log yolu: reddedilen filtre bir WARN üretir.
    char buffer[64];
    RowlEngine_QueryWorldJson(h, R"({"seq_since":1})", buffer, sizeof(buffer));
    check(capturedContains("seq_since/dirty_only filter rejected"),
          "F: native WARN did not reach the callback");

    RowlEngine_SetLogCallback(nullptr);
    const int before = g_callbackHits.load();
    Rowl::Core::Logger::warn("observability: after-unhook marker");
    check(g_callbackHits.load() == before, "F: callback fired after SetLogCallback(nullptr)");

    // Katı JSON: tırnak, ters bölü, satır sonu, kontrol baytı, geçersiz UTF-8.
    Rowl::Core::Logger::warn("obs-escape \"q\" \\ \n\t\x01 \xFF\xFE end");
    const auto logs = recentLogs(0, 256);
    bool found = false;
    for (const auto& entry : logs) {
        const std::string msg = entry.value("message", "");
        if (msg.rfind("obs-escape", 0) == 0) {
            found = true;
            check(msg.find("\"q\" \\") != std::string::npos, "F: quote/backslash lost");
            check(msg.find(" end") != std::string::npos, "F: message truncated");
        }
    }
    check(found, "F: escaped message missing from recent logs");
    const auto warnOnly = recentLogs(3, 2);
    check(warnOnly.size() <= 2, "F: maxCount not honored");
    for (const auto& entry : warnOnly) {
        check(entry.value("level", -1) >= 3, "F: minLevel not honored");
    }

    // Eşzamanlı log + callback değişimi + okuma (TSan bacağı).
    std::atomic<bool> stop{false};
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([t, &stop] {
            int i = 0;
            while (!stop.load(std::memory_order_relaxed) && i < 200) {
                Rowl::Core::Logger::info("obs-race writer " + std::to_string(t) + " #" +
                                         std::to_string(i++));
            }
        });
    }
    for (int i = 0; i < 50; ++i) {
        RowlEngine_SetLogCallback((i % 2) ? &captureLog : nullptr);
        (void)recentLogs(0, 64);
    }
    stop = true;
    for (auto& w : writers) w.join();
    RowlEngine_SetLogCallback(nullptr);
    TEST_PASS("observability: log callback/unhook, strict recent-logs JSON, concurrent logging");
}

}  // namespace

void test_observability_c_api() {
    TEST_SECTION("RowlEye Observability C API (owner-thread, size protocol, logs)");

    legDeadHandles();

    const std::string root = makeRunRoot();
    RowlEngineHandle h = makeTwoNodeEngine(root);
    legForeignThread(h);
    legSizeProtocol(h);
    legWorldFilter(h);
    legInjectAtomic(h);
    legLogs(h);
    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}
