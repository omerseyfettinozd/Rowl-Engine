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
#include "rowl/platform/user_data_directories.hpp"

#include <cerrno>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
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

// Var olamayacak bir pid üretir: POSIX'te kill(pid,0) ESRCH vermelidir;
// Windows'ta OpenProcess + ERROR_INVALID_PARAMETER (ölü sahip).
// Deterministik olması için yüksek adaylardan aşağı doğru ilk
// kanıtlanmış-ölü vereni seçer. Hiçbiri kanıtlanamazsa -1 döner;
// çağıran SKIP eder (kızartmaz) — 492 -1 guard'ı.
long findDeadPid() {
#ifndef _WIN32
    for (long candidate = 2147483647L; candidate > 2147483647L - 64; --candidate) {
        const pid_t pid = static_cast<pid_t>(candidate);
        if (::kill(pid, 0) != 0 && errno == ESRCH) return candidate;
    }
    return -1;
#else
    // ownerProcessAlive ile aynı sözleşme: ERROR_INVALID_PARAMETER ölü
    // demektir; diğer hatalar (ACCESS_DENIED dahil) muhafazakâr-canlıdır.
    for (long candidate = 4194300L; candidate > 4194300L - 256; --candidate) {
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    static_cast<DWORD>(candidate));
        if (handle != nullptr) {
            CloseHandle(handle);
            continue;
        }
        if (GetLastError() == ERROR_INVALID_PARAMETER) return candidate;
    }
    return -1;
#endif
}

long livePid() {
#ifndef _WIN32
    return static_cast<long>(::getpid());
#else
    return static_cast<long>(GetCurrentProcessId());
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
        // 492: yol kurulumu path operatörleriyle yapılır (dar .string()
        // birleştirme Windows'ta ASCII-dışı temp kökünde fırlatırdı).
        fs::path legacyStray = finalPath;
        legacyStray += ".tmp";
        writeFileBytes(legacyStray, "PARTIAL-LEGACY-GARBAGE");
        // 492 -1 guard: ölü pid kanıtlanamazsa bu dal SKIP edilir
        // (kızartılmaz); iyi-slot + legacy + canlı-yazar kontrolleri
        // taşınabilir oldukları için her platformda çalışır.
        const long deadPid = findDeadPid();
        const bool haveDeadPid = deadPid > 0;
        fs::path staleOwned;
        if (!haveDeadPid) {
            std::cout << "D09-PROBE SKIP: no provably-dead pid on this host; "
                         "stale-owned assertions skipped" << std::endl;
        } else {
            staleOwned = finalPath;
            staleOwned += ".tmp." + std::to_string(deadPid) + ".0.ABCDEF";
            writeFileBytes(staleOwned, "PARTIAL-OWNED-GARBAGE");
        }
        // Canlı yazar tmp'si: asla silinmemeli.
        const long selfPid = livePid();
        const bool haveLivePid = selfPid > 0;
        fs::path liveOwned;
        const std::string liveSentinel = "LIVE-WRITER-BYTES";
        if (!haveLivePid) {
            std::cout << "D09-PROBE SKIP: no live pid on this host; "
                         "live-writer assertions skipped" << std::endl;
        } else {
            liveOwned = finalPath;
            liveOwned += ".tmp." + std::to_string(selfPid) + ".0.LIVE";
            writeFileBytes(liveOwned, liveSentinel);
        }

        Rowl::State::cleanupStraySlotTemp(finalPath);

        std::error_code ec;
        checkProbe(!fs::exists(legacyStray, ec),
                   "legacy <slot>.json.tmp must be swept (control)");
        if (haveDeadPid) {
            if (fs::exists(staleOwned, ec)) {
                std::cerr << "D09-ORPHAN RED: stale owned tmp survived "
                             "cleanupStraySlotTemp: "
                          << Rowl::Platform::pathToUtf8(staleOwned) << std::endl;
                ++g_probeFailures;
            }
        }
        if (haveLivePid) {
            checkProbe(fs::exists(liveOwned, ec),
                       "live writer owned tmp must be preserved");
            if (fs::exists(liveOwned, ec)) {
                checkProbe(readFileBytes(liveOwned) == liveSentinel,
                           "live writer owned tmp must stay byte-identical");
            }
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
