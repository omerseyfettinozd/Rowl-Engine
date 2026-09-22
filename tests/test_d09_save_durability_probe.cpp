/**
 * test_d09_save_durability_probe.cpp — D09 save-dayanıklılık RED/YEŞİL probu.
 *
 * Onarım yönü (save dayanıklılığı):
 *  (a) tmp-yetimi: crash/güç-kesintisi benzetimi. Kesintiye uğramış yazımdan
 *      kalan SAHİPLİ-benzersiz tmp'ler (<slot>.json.tmp.<pid>.<ctr>.<rand>)
 *      sahibi ölmüşse disk tozu olarak birikir; her crash bir tmp sızdırır,
 *      dolan disk bir sonraki save'i ENOSPC ile fail-closed'a düşürür.
 *      Kusur satırı: engine/src/state/save_durability.cpp:261
 *      (cleanupStraySlotTemp yalnızca legacy "<slot>.json.tmp" süpürür,
 *      sahibi-ölü benzersiz tmp'lere dokunmaz).
 *  (b) createNextState graf-kimliğini düşürür: adım geçişinde graphIdentity
 *      taşınmaz; Engine-dışı saveSlot yolu (public API) kimliksiz slot yazar,
 *      kayıt legacy-warn yoluna düşer (#70 provenance garantisi tek
 *      çağrıcıya bağımlı kalır).
 *
 * RED (mevcut kod) — öngörülen gözlem + exit:
 *   "D09-ORPHAN RED: stale owned tmp survived cleanupStraySlotTemp" + exit 1
 *   "D09-IDENTITY RED: createNextState dropped graphIdentity" + exit 1
 * YEŞİL (onarım sonrası): iki bölüm de sessiz geçer, exit 0.
 *
 * Koruma kolları (RED koşusunda da YEŞİL kalır — aşırı-silme/slot-bozulma
 * regresyonuna karşı):
 *   - canlı yazar tmp'si (<pid>=getpid()) aynen korunur, bayt-birebir;
 *   - iyi slot dosyası süpürme boyunca bayt-birebir aynı kalır;
 *   - legacy "<slot>.json.tmp" süpürülür (mevcut davranış kontrolü).
 *
 * rowl_tests gövdesine gömülmez; rowl_engine_objects'a bağlanır (D08 emsali).
 */
#include "rowl_test_harness.hpp"

#include "rowl/state/game_state.hpp"
#include "rowl/state/save_durability.hpp"

#include <cerrno>
#include <chrono>
#include <iostream>

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

int g_probeFailures = 0;

void checkProbe(bool cond, const char* what) {
    if (!cond) {
        std::cerr << "D09-PROBE FAIL: " << what << std::endl;
        ++g_probeFailures;
    }
}

std::string readFileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void writeFileBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// Var olamayacak bir pid üretir: kill(pid,0) ESRCH vermelidir (ölü sahip).
// Deterministik olması için INT_MAX'tan aşağı doğru ilk ESRCH vereni seçer.
long findDeadPid() {
#ifndef _WIN32
    for (long candidate = 2147483647L; candidate > 2147483647L - 64; --candidate) {
        const pid_t pid = static_cast<pid_t>(candidate);
        if (::kill(pid, 0) != 0 && errno == ESRCH) return candidate;
    }
    return -1;
#else
    return -1;
#endif
}

long livePid() {
#ifndef _WIN32
    return static_cast<long>(::getpid());
#else
    return -1;
#endif
}

}  // namespace

int main() {
    using Rowl::State::GameState;

    TEST_SECTION("D09 probe (a): tmp-yetimi / guc-kesinti benzetimi");
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() /
            ("rowl_d09_probe_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        {
            std::error_code ec;
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
        }
        const fs::path finalPath = root / "save_slot_3.json";
        const std::string goodPayload = R"({"version":4,"step_id":9,"note":"good"})";

        std::string writeError;
        checkProbe(Rowl::State::writeSlotFileAtomically(finalPath, goodPayload, &writeError),
                   "probe setup: baseline atomic write failed");

        // Kesinti artıkları: legacy stray + sahibi-ölü benzersiz tmp.
        const fs::path legacyStray = finalPath.string() + ".tmp";
        writeFileBytes(legacyStray, "PARTIAL-LEGACY-GARBAGE");
        const long deadPid = findDeadPid();
        checkProbe(deadPid > 0, "probe setup: no provably-dead pid available");
        const fs::path staleOwned =
            fs::path(finalPath.string() + ".tmp." + std::to_string(deadPid) + ".0.ABCDEF");
        writeFileBytes(staleOwned, "PARTIAL-OWNED-GARBAGE");
        // Canlı yazar tmp'si: asla silinmemeli.
        const long selfPid = livePid();
        checkProbe(selfPid > 0, "probe setup: live pid unavailable");
        const fs::path liveOwned =
            fs::path(finalPath.string() + ".tmp." + std::to_string(selfPid) + ".0.LIVE");
        const std::string liveSentinel = "LIVE-WRITER-BYTES";
        writeFileBytes(liveOwned, liveSentinel);

        Rowl::State::cleanupStraySlotTemp(finalPath);

        std::error_code ec;
        checkProbe(!fs::exists(legacyStray, ec),
                   "legacy <slot>.json.tmp must be swept (control)");
        if (fs::exists(staleOwned, ec)) {
            std::cerr << "D09-ORPHAN RED: stale owned tmp survived "
                         "cleanupStraySlotTemp: "
                      << staleOwned.string() << std::endl;
            ++g_probeFailures;
        }
        checkProbe(fs::exists(liveOwned, ec),
                   "live writer owned tmp must be preserved");
        if (fs::exists(liveOwned, ec)) {
            checkProbe(readFileBytes(liveOwned) == liveSentinel,
                       "live writer owned tmp must stay byte-identical");
        }
        checkProbe(readFileBytes(finalPath) == goodPayload,
                   "good slot file must stay byte-identical across sweep");

        fs::remove_all(root, ec);
    }

    TEST_SECTION("D09 probe (b): createNextState graphIdentity tasiyici");
    {
        auto base = GameState::createInitialState(101);
        auto stamped = GameState::withGraphIdentity(base, "d09-probe-graph");
        checkProbe(stamped && stamped->graphIdentity == "d09-probe-graph",
                   "probe setup: withGraphIdentity did not stamp");
        auto next = GameState::createNextState(stamped, 102, "k", "v");
        checkProbe(next != nullptr, "probe setup: createNextState returned null");
        if (next) {
            checkProbe(next->stepId == stamped->stepId + 1,
                       "createNextState must advance stepId (control)");
            checkProbe(next->getVariable("k") == "v",
                       "createNextState must carry the variable write (control)");
            if (next->graphIdentity != "d09-probe-graph") {
                std::cerr << "D09-IDENTITY RED: createNextState dropped graphIdentity "
                             "(got \""
                          << next->graphIdentity << "\")" << std::endl;
                ++g_probeFailures;
            }
            checkProbe(next->serializeJson().find("graph_id") != std::string::npos,
                       "staged save must carry graph_id on the wire");
        }
    }

    if (g_probeFailures != 0) {
        std::cerr << "D09-PROBE: " << g_probeFailures << " failure(s)" << std::endl;
        return 1;
    }
    TEST_PASS("D09 save durability: stale-tmp sweep + identity carriage");
    return 0;
}
