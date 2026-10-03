/**
 * test_rc_soak_and_data_safety.cpp — RC soak and data-safety regression.
 *
 * Job 24: long-run frame/transition soak with RSS stability, stressed
 * save/load/rewind integrity, repeated lifecycle + audio-focus
 * interruption cycles, and write-failure safety (read-only save dir keeps
 * the previous valid slot byte-identical via .tmp atomic isolation).
 */
#include "rowl_test_harness.hpp"

#include "rowl/state/session_persistence.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

// RSS olcum platform bagimliliklari. psapi baglantisi tests/CMakeLists.txt'te
// WIN32 icin zaten var (GetProcessMemoryInfo icin).
#if defined(_WIN32)
// windows.h min/max'i KORUMASIZ makro olarak tanimlar ve MSVC'nin STL'i
// bunlari geri almaz; asagidaki std::min/std::max kullanimlari (RSS olcum
// dongusu) "illegal token on right side of ::" ile patlar. Once tanimla.
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Sanitizer algilamasi — DOSYA BASINDA TEK KOSUL, TEK YERDE.
//
// DENETIM (2026-10-04). Onceki yazimda bu soru dosyanin UC ayri yerinde,
// UC farkli bicimde soruluyordu:
//   * bolum 1  kTolerance        -> ASan/TSan (UBSan YOK)
//   * bolum 5  kStressBudgetSecs -> ASan/UBSan/TSan veya _WIN32
//   * bolum 6  kGrowthTimeScale  -> ASan/UBSan/TSan
// ve 63d767b bolum 6'yi `#if defined(NDEBUG)` anahtarina baglayarak kirdi.
// Sonuc, bu commit ile duzeltilen tutarsizlik:
//
//   Debug, sanitizer yok : ESKI 150/300 ms  ->  YENI 300/600 ms  (2x GEVSEMIS)
//
// CI'da butce kapisi calisan isler TAMAMEN Debug (ci.yml 68/208/413/465/537),
// yani EN SIK calisan tip 2 kat gevsetilmis oldu. Oysa 63d767b commit'i
// "DEGER DEGISTIRILMEDI... bir sayi uretilmez" diyordu; bu ifade YANLISTI.
//
// Buradaki iki makro, eski (main = 5c07162) satirlarin ETKIN degerlerini
// birebir korur — hicbir esik gevsemez:
//   ROWL_SANITIZER_BUILD        ASan | UBSan | TSan   (zaman olcekleri)
//   ROWL_RSS_INSTRUMENTED_BUILD ASan | TSan           (yalniz RSS'i oynatanlar)
//
// Ikisi ayri kaldi cunku sorulari farklidir ve eski kodda da farkliydi:
// UBSan zaman olcegini ~2x yavaslatir ama resident set size'i kendi basina
// oynatmaz; ASan/TSan (arena/quarantine, shadow memory) RSS'i onlarca MB
// oynatir. Tek makroya indirmek UBSan-only derlemelerde kTolerance'i
// 8 MB -> 64 MB yapardi, yani yine bir esik gevsemesi olurdu.
//
// __has_feature yalnizca Clang'da tanimlidir; GCC'de dogrudan #if icinde
// sorgulanirsa "missing binary operator" hatasi verir. Bu yuzden once
// defined() ile varligi ayiklanir, icteki #if yalnizca tanimliyken
// degerlendirilir. GCC'nin __SANITIZE_* makrolari GCC sanitizer isini,
// __has_feature dali Clang sanitizer derlemelerini kapsar.
// ---------------------------------------------------------------------------
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__) || defined(__SANITIZE_THREAD__)
#define ROWL_SANITIZER_BUILD 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer) || __has_feature(thread_sanitizer)
#define ROWL_SANITIZER_BUILD 1
#endif
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ROWL_RSS_INSTRUMENTED_BUILD 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ROWL_RSS_INSTRUMENTED_BUILD 1
#endif
#endif

namespace {

// ---------------------------------------------------------------------------
// P2-17 (2026-10-03): RSS kapisi sessiz gecmeyi birakmaz.
//
// Onceki yazim: `uint64_t currentRssBytes() { ... }` idi ve Windows/macOS
// kollarinda DUZ `return 0` idi. Butce govdesi `if (firstRss > 0)` icinde
// oldugu icin bu platformlarda hicbir olcum yapilmadan dogrudan
// "TEST_PASS(3000-frame soak keeps RSS stable)" yaziliyordu: kapi hicbir seyi
// olcmeden yesil donuyordu. Kanit: currentRssBytes 0 donerken soak dongusune
// ~200 MiB sizdirildi, test yine "ALL TESTS PASSED" + EXIT=0 verdi; ayni
// ikili /proc/self/statm okurken ayni sizdirma ile EXIT=1 verdi.
//
// Duzeltme iki parca:
//   1. `readRssBytes(uint64_t&)` her desteklenen platformda GERCEKTEN olcer ve
//      "olcemeyi basaramadim" bilgisini `false` donusuyle disari verir. 0
//      artuk gecerli bir RSS degeri DEGILDIR, hata sinyalidir.
//   2. Kapi karari saf `evaluateRss(...)` fonksiyonundan gecer; bu fonksiyon
//      olcum yoksa ASLA kararli (yesil) sonuc veremez.
// ---------------------------------------------------------------------------

// Desteklenen platformun adı (rapor/hata mesajlari icin).
const char* rssPlatformName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unsupported";
#endif
}

// Kullanici sayisina anlamsiz gelen bir "resident" degeri olcumun calismadiginin
// isaretidir (dosya acildi ama bos/bozuk okundu, sayfa boyutu 0, vb.).
constexpr uint64_t kMinPlausibleRss = 1ULL << 20; // 1 MiB

// Gercek resident set size okur. Basariliysa true + `out` doldurulur.
bool readRssBytes(uint64_t& out) {
#if defined(_WIN32)
    // GetProcessWorkingSetSize kota LIMITI raporlar, kullanimi degil, bu yuzden
    // hicbir buyumeyi goremez; WorkingSetSize gercek resident degerdir.
    // (psapi baglantisi tests/CMakeLists.txt, yalniz WIN32.)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                             sizeof(counters)) == 0) {
        return false;
    }
    out = static_cast<uint64_t>(counters.WorkingSetSize);
    return true;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return false;
    }
    out = static_cast<uint64_t>(info.resident_size);
    return true;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0, resident = 0;
    if (!(statm >> size >> resident)) return false;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) return false;
    out = resident * static_cast<uint64_t>(pageSize);
    return true;
#else
    // Desteklenmeyen platform: kapinin kirmiziya dusmesi icin false donulur.
    // (Eskiden burada 0 donulup kapi sessizce yesil geciyordu.)
    (void)out;
    return false;
#endif
}

// RSS kapisinin karar tipleri. kUnmeasured YESIL DEGILDIR: kapi bu durumda
// kirmiziya dusmelidir (bkz. bolum 1'deki kullanim).
enum class RssVerdict { kStable, kDrift, kUnmeasured };

// Saf karar fonksiyonu (yan etkisiz; bolum 10'daki meta kilit dogrudan
// dogrular). Kapsanan "sessiz gecme" yollari:
//   * samples == 0                     -> hic olcum alinmadi
//   * samples * 2 < expectedSamples    -> orneklerin yarisi okunamadi
//   * minRss < kMinPlausibleRss       -> sayisal ama anlamsiz "resident"
//   * maxRss < minRss                  -> tutarsiz istatistik
RssVerdict evaluateRss(uint64_t minRss, uint64_t maxRss, uint64_t tolerance,
                       size_t samples, size_t expectedSamples) {
    if (samples == 0) return RssVerdict::kUnmeasured;
    if (samples * 2 < expectedSamples) return RssVerdict::kUnmeasured;
    if (minRss < kMinPlausibleRss) return RssVerdict::kUnmeasured;
    if (maxRss < minRss) return RssVerdict::kDrift;
    if (maxRss - minRss > tolerance) return RssVerdict::kDrift;
    return RssVerdict::kStable;
}

// ---------------------------------------------------------------------------
// P1-11 (2026-10-03): sure kapisi yesil donerken "within budget" demeyecek.
//
// Onceki yazimda butce asiminda `std::cout` ile uyari yazilip, TEST_PASS ise
// KOSULSUZ "within budget" metniyle cikardi. ROWL_PERF_FLOOR=report iken butce
// 333 katina ciksa da yesil veriyordu. Simdi karar `evaluateBudget(...)` ile
// veriliyor ve PASS metni ancak butce GERCEKTEN icinde oldugunda "within
// budget" iceriyor.
// ---------------------------------------------------------------------------
enum class BudgetVerdict { kWithin, kExceededEnforced, kExceededReported };

bool perfFloorEnforced() {
    return environmentValue("ROWL_PERF_FLOOR", "enforced") == "enforced";
}

BudgetVerdict evaluateBudget(double measuredSecs, double budgetSecs, bool enforced) {
    if (!(measuredSecs >= budgetSecs)) return BudgetVerdict::kWithin;
    return enforced ? BudgetVerdict::kExceededEnforced : BudgetVerdict::kExceededReported;
}

// Olcumu GERCEK birimiyle bicimlendirir. "ms" icin 2 ondalik — bolum 6'nin
// kendi [history-growth] olcum satiri da setprecision(2) kullandigi icin PASS
// notu ile olcum satiri ayni sayiyi gosterir. "s" icin tam sayi (saniye
// butcelerinde yaslama anlamsiz).
std::string formatBudgetValue(double value, const char* unit) {
    std::ostringstream os;
    const bool isSeconds = (unit[0] == 's' && unit[1] == '\0');
    os << std::fixed << std::setprecision(isSeconds ? 0 : 2);
    os << value;
    return os.str();
}

// PASS satirina eklenecek zamanlama notu. kWithin disinda HICBIR dalinda
// "within budget" kelimesi uretilmez; boylece yesil bir log, butce asilmis
// oldugunu saklayamaz.
//
// DENETIM (2026-10-04): `unit` parametresi eklendi. Onceki yazim her olcumu
// saniye varsayip "s" soneki yaziyordu, ama bu fonksiyon IKI farkli birimle
// cagrilir:
//   * bolum 5 (N-stres): saniye  — stressSecs / kStressBudgetSecs
//   * bolum 6 (buyume):  MILLISANIYE — saveMs / loadMs, 150.0/300.0 * scale
// Bolum 6'da sonuc yesil logda "within budget (273s <= 600s)" oluyordu,
// gercek degerler ise 272.98 ms <= 600 ms idi. Bu bir BIRIM etiketi yalaniydi
// ve dogrudan P1-11'in "yesil log butceye dair yalan soylemesin" amaciyla
// celisiyordu: metin dogru birimi degil, yanlis birimi ilan ediyordu.
// Artik cagri yeri gercek birimi acikca belirtir; BUTCE DEGERLERINE (150/300,
// kStressBudgetSecs, kGrowthTimeScale) dokunulmamistir — bu yalnizca etikettir.
std::string budgetPassNote(BudgetVerdict verdict, double measured, double bound,
                           const char* unit) {
    switch (verdict) {
        case BudgetVerdict::kWithin:
            return " within budget (" + formatBudgetValue(measured, unit) + unit +
                   " <= " + formatBudgetValue(bound, unit) + unit + ")";
        case BudgetVerdict::kExceededEnforced:
            return " OVER BUDGET (enforced)";
        case BudgetVerdict::kExceededReported:
            return " OVER BUDGET, NOT ENFORCED — this pass does NOT verify timing";
    }
    return " UNKNOWN VERDICT";
}

class SoakHost final : public Rowl::Platform::PlatformHost {
public:
    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        if (path != assetPath) return nullptr;
        return std::make_unique<std::istringstream>(assetJson);
    }

    std::filesystem::path writableSavePath() const override { return savePath; }
    Rowl::Platform::LifecycleState lifecycleState() const override { return lifecycle; }

    std::vector<Rowl::Platform::RuntimeInputEvent> takeInputEvents() override {
        std::vector<Rowl::Platform::RuntimeInputEvent> result;
        while (!input.empty()) {
            result.push_back(input.front());
            input.pop_front();
        }
        return result;
    }

    Rowl::Platform::RenderSurface renderSurface() const override { return surface; }
    Rowl::Platform::AudioFocus audioFocus() const override { return focus; }

    // Eight-node ring: every node advances to the next, last wraps to first.
    std::string assetPath = "json/full_story_graph.json";
    std::string assetJson = R"({
        "format_version": 4,
        "start_node_id": 1,
        "nodes": [
            {"id": 1, "speaker": "S", "dialogue": "N1", "next_nodes": [{"id": 2}]},
            {"id": 2, "speaker": "S", "dialogue": "N2", "next_nodes": [{"id": 3}]},
            {"id": 3, "speaker": "S", "dialogue": "N3", "next_nodes": [{"id": 4}]},
            {"id": 4, "speaker": "S", "dialogue": "N4", "next_nodes": [{"id": 5}]},
            {"id": 5, "speaker": "S", "dialogue": "N5", "next_nodes": [{"id": 6}]},
            {"id": 6, "speaker": "S", "dialogue": "N6", "next_nodes": [{"id": 7}]},
            {"id": 7, "speaker": "S", "dialogue": "N7", "next_nodes": [{"id": 8}]},
            {"id": 8, "speaker": "S", "dialogue": "N8", "next_nodes": [{"id": 1}]}
        ]
    })";
    std::filesystem::path savePath;
    Rowl::Platform::LifecycleState lifecycle = Rowl::Platform::LifecycleState::Active;
    Rowl::Platform::RenderSurface surface{
        Rowl::Platform::RenderSurfaceKind::Offscreen, nullptr, 320, 180};
    Rowl::Platform::AudioFocus focus = Rowl::Platform::AudioFocus::Granted;
    std::deque<Rowl::Platform::RuntimeInputEvent> input;
};

bool ringContains(uint64_t id) { return id >= 1 && id <= 8; }

std::string uniqueTempDir(const std::string& prefix) {
    return (std::filesystem::temp_directory_path() /
            (prefix + std::to_string(
                           std::chrono::steady_clock::now().time_since_epoch().count())))
        .string();
}

bool probeWriteBlocked(const std::filesystem::path& dir) {
    const auto probe = dir / ".rowl_write_probe";
    std::ofstream out(probe, std::ios::out | std::ios::trunc);
    const bool blocked = !out.is_open();
    if (out.is_open()) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(probe, ec);
    }
    return blocked;
}

} // namespace

void test_rc_soak_and_data_safety() {
    TEST_SECTION("RC Soak & Data Safety");

    // -----------------------------------------------------------------------
    // 0. KAPI HIJYENI KILIDI (P2-17 + P1-11, 2026-10-03)
    //
    // Bu blok, asagidaki iki OLÇME kredisinin karar fonksiyonlarini dogrudan
    // dogrular. Amaci: bu dosyanin ileride tekrar "sessizlestirilmesini"
    // engellemek. Bugun P2-17 ve P1-11 bulgularinin IKISI de ayni tek seyden
    // dogdu: karar, olcumden bagimsiz olarak yesil uretebiliyordu. Asagidaki
    // tablo, o eski kirli yesil yollarin her birinin ARTIK kirmiziya dustugunu
    // kanitlar. Saf fonksiyonlari dogruladigi icin makine bagimliligi yoktur ve
    // Linux'ta Windows/macOS dalinin da kirli olmadigini kanitlar.
    //
    // ROWL_SKIP_LONG_TESTS=1 ile de calisir: bu kilit uzun degildir ve
    // atlanan bir soak'in arkasinda hicbir sey olcmeden gecmesine izin
    // vermemelidir.
    // -----------------------------------------------------------------------
    {
        const size_t kExpected = 10;
        struct RssCase {
            const char* label;
            uint64_t minRss;
            uint64_t maxRss;
            uint64_t tolerance;
            size_t samples;
            RssVerdict expected;
        };
        const RssCase rssCases[] = {
            {"stable 8MB drift under 8MB tolerance", 100ULL << 20, 104ULL << 20,
             8ULL << 20, kExpected, RssVerdict::kStable},
            {"leak: 200MiB drift", 128696320ULL, 333512704ULL,
             8ULL << 20, kExpected, RssVerdict::kDrift},
            // ESKI KIRLI YESIL YOL #1: hic ornek alinmadi (Windows/macOS'ta
            // currentRssBytes() 0 donerdi -> 10 ornekten 0'u okunurdu).
            {"zero samples (old Windows/macOS stub)", 0, 0, 8ULL << 20, 0,
             RssVerdict::kUnmeasured},
            // ESKI KIRLI YESIL YOL #2: orneklerin yarisi okunamadi.
            {"4/10 samples readable", 100ULL << 20, 101ULL << 20, 8ULL << 20, 4,
             RssVerdict::kUnmeasured},
            {"5/10 samples readable (boundary ok)", 100ULL << 20, 101ULL << 20,
             8ULL << 20, 5, RssVerdict::kStable},
            {"implausible reading (<1MiB resident)", 4096, 4096, 8ULL << 20,
             kExpected, RssVerdict::kUnmeasured},
            {"inverted min/max", 200ULL << 20, 100ULL << 20, 8ULL << 20,
             kExpected, RssVerdict::kDrift},
        };
        for (const RssCase& c : rssCases) {
            const RssVerdict got = evaluateRss(c.minRss, c.maxRss, c.tolerance,
                                               c.samples, kExpected);
            if (got != c.expected) {
                std::cerr << "RSS gate hygiene lock failed: " << c.label
                          << " -> verdict " << static_cast<int>(got)
                          << ", expected " << static_cast<int>(c.expected) << std::endl;
                exit(1);
            }
        }

        struct BudgetCase {
            const char* label;
            double measured;
            double budget;
            bool enforced;
            BudgetVerdict expected;
            bool passNoteMayClaimWithinBudget;
        };
        const BudgetCase budgetCases[] = {
            {"inside budget, enforced", 100.0, 300.0, true,
             BudgetVerdict::kWithin, true},
            {"inside budget, report", 100.0, 300.0, false,
             BudgetVerdict::kWithin, true},
            // KANIT-2'deki mutasyonun KARSILIGI: butce 99999 sn'e cekildi.
            {"budget blown 333x, enforced -> RED", 99999.0, 300.0, true,
             BudgetVerdict::kExceededEnforced, false},
            {"budget blown 333x, report", 99999.0, 300.0, false,
             BudgetVerdict::kExceededReported, false},
            {"exactly at budget is an exceed", 300.0, 300.0, true,
             BudgetVerdict::kExceededEnforced, false},
        };
        for (const BudgetCase& c : budgetCases) {
            const BudgetVerdict got =
                evaluateBudget(c.measured, c.budget, c.enforced);
            const std::string note = budgetPassNote(got, c.measured, c.budget, "s");
            const bool claimsWithin = note.find("within budget") != std::string::npos;
            if (got != c.expected || claimsWithin != c.passNoteMayClaimWithinBudget) {
                std::cerr << "Budget gate hygiene lock failed: " << c.label
                          << " -> verdict " << static_cast<int>(got)
                          << " (expected " << static_cast<int>(c.expected)
                          << "), pass-note claims 'within budget'=" << claimsWithin
                          << " (expected " << c.passNoteMayClaimWithinBudget
                          << "): \"" << note << "\"" << std::endl;
                exit(1);
            }
        }

        // BIRIM ETIKETI KILIDI (denetim 2026-10-04). budgetPassNote iki
        // birimle cagrilir: bolum 5 saniye, bolum 6 milisaniye. Onceki
        // yazim her ikisine de "s" yaziyordu; bolum 6'nin gercek degeri
        // loadMs=272.98 iken log "within budget (273s <= 600s)" diyordu.
        // Yanlis birim, P1-11'in yasakladigi turden bir yesil-log yalanidir
        // ("dogru sayi, yanlis birim"), bu yuzden asagidaki iki kontrol
        // regresyona donusmesini engeller:
        //   * "ms" cagrisi "s" uretmemeli,
        //   * "s" cagrisi "ms" uretmemeli,
        //   * ms degerleri tam sayiya YUVARLANMAMALI (272.98 -> "273" olmaz).
        {
            const std::string secsNote =
                budgetPassNote(BudgetVerdict::kWithin, 25.71, 300.0, "s");
            const std::string msNote =
                budgetPassNote(BudgetVerdict::kWithin, 272.98, 600.0, "ms");
            if (secsNote.find("ms") != std::string::npos ||
                msNote.find("272.98ms") == std::string::npos ||
                msNote.find("600.00ms") == std::string::npos) {
                std::cerr << "Budget gate hygiene lock failed: unit label does not "
                             "match the measurement. seconds note=\""
                          << secsNote << "\" ms note=\"" << msNote << "\""
                          << std::endl;
                exit(1);
            }
        }

        // Oznemli: kirmiziya dusmemis her kosulda notun metni gercekten
        // kirli-yesil iddiasini tasimamalidir (defensive, asagidaki iki
        // kontrol regresyona donusmemis olsun diye).
        if (budgetPassNote(BudgetVerdict::kExceededReported, 99999.0, 300.0, "s")
                .find("within budget") != std::string::npos ||
            budgetPassNote(BudgetVerdict::kExceededEnforced, 99999.0, 300.0, "s")
                .find("within budget") != std::string::npos) {
            std::cerr << "Budget gate hygiene lock failed: an exceeded budget still "
                         "labels the pass line as 'within budget'" << std::endl;
            exit(1);
        }
        TEST_PASS("gate hygiene: RSS never passes unmeasured; time budget never "
                  "self-labels 'within budget' when exceeded");
    }

    // T0 katmanlama: PR sanitizer işi hızlı alt-kümeyi koşar; soak/stres
    // (3000-frame soak, 200/1000-iterasyon stresleri, 1200-adım büyüme)
    // nightly'daki tam süite aittir. ROWL_SKIP_LONG_TESTS=1 iken bu bölüm
    // raporlanıp geçilir — atlanmaz, kayda girer.
    if (environmentValue("ROWL_SKIP_LONG_TESTS", "") == "1") {
        std::cout << "  [soak] (ROWL_SKIP_LONG_TESTS=1: soak/stres bölümü "
                     "nightly süite bırakıldı, raporlanıp geçildi)" << std::endl;
        TEST_PASS("RC soak & stress deferred to nightly suite");
        return;
    }

    auto vfs = std::make_shared<Rowl::VFS::VFSManager>();
    auto host = std::make_shared<SoakHost>();
    const std::string saveRoot = uniqueTempDir("rowl_rc_soak_");
    host->savePath = saveRoot;
    auto context = std::make_shared<Rowl::Core::RuntimeContext>(vfs, host);
    Rowl::Core::Engine engine(context);

    Rowl::Core::EngineConfig config;
    config.virtualWidth = 320;
    config.virtualHeight = 180;
    if (!engine.initialize(config)) {
        std::cerr << "Soak engine could not initialize offscreen" << std::endl;
        exit(1);
    }
    engine.setPlayState(true);
    engine.resetToStartNode();

    // 1. Frame + transition soak with RSS stability.
    {
        constexpr int kWarmupSteps = 300;
        constexpr int kSoakSteps = 3000;
        constexpr int kAdvanceEvery = 10; // 300 node transitions total
        for (int i = 0; i < kWarmupSteps; ++i) engine.step(1.0f / 60.0f);

        // P2-17: olcum basarisiz olursa hicbir dongu kirilmaz; kapi en basta
        // kirmiziya dusmelidir (eski yazimda `firstRss > 0` sarti butceyi
        // butun denetimiyle birlikte atliyordu).
        uint64_t firstRss = 0;
        if (!readRssBytes(firstRss)) {
            std::cerr << "Soak RSS gauge could not read resident set size on platform '"
                      << rssPlatformName()
                      << "' — the RSS stability gate did NOT run and will NOT "
                         "report a pass" << std::endl;
            exit(1);
        }

        constexpr int kRssSampleEvery = 300;
        const size_t expectedRssSamples =
            static_cast<size_t>(kSoakSteps / kRssSampleEvery);
        uint64_t minRss = firstRss, maxRss = firstRss;
        size_t rssSamples = 0;
        size_t rssReadFailures = 0;
        uint64_t transitions = 0;
        for (int i = 1; i <= kSoakSteps; ++i) {
            engine.step(1.0f / 60.0f);
            if (i % kAdvanceEvery == 0) {
                engine.advanceToNextNode();
                ++transitions;
            }
            if (i % kRssSampleEvery == 0) {
                uint64_t rss = 0;
                // Eskiden `if (rss > 0)` ile sessizce atiliyordu; artik sayilir.
                if (readRssBytes(rss)) {
                    ++rssSamples;
                    minRss = std::min(minRss, rss);
                    maxRss = std::max(maxRss, rss);
                } else {
                    ++rssReadFailures;
                }
            }
            if (!ringContains(engine.getCurrentNodeId()) || !engine.isRunning()) {
                std::cerr << "Soak diverged at step " << i << std::endl;
                exit(1);
            }
        }
        if (transitions != 300) {
            std::cerr << "Soak did not perform the expected transitions" << std::endl;
            exit(1);
        }
        // Under ASan/TSan the runtime (ASan quarantine and arenas, TSan
        // shadow memory and sync metadata) moves RSS by tens of MB on
        // its own, so the tight 8MB production tolerance
        // is meaningless there; leak detection under sanitizers is
        // LSan's job (currently out of scope), not this gauge's.
        // Anahtar dosya basinda TEK KOSUL olarak hesaplanir
        // (ROWL_RSS_INSTRUMENTED_BUILD); UBSan burada bilerek disarida,
        // cunku resident set size'i kendi basina oynatmaz.
#ifdef ROWL_RSS_INSTRUMENTED_BUILD
        constexpr uint64_t kTolerance = 64ULL * 1024ULL * 1024ULL;
#else
        constexpr uint64_t kTolerance = 8ULL * 1024ULL * 1024ULL;
#endif

        const RssVerdict rssVerdict = evaluateRss(
            minRss, maxRss, kTolerance, rssSamples, expectedRssSamples);
        std::cout << "  [soak-rss] platform=" << rssPlatformName()
                  << " first=" << firstRss << "B min=" << minRss << "B max=" << maxRss
                  << "B drift=" << (maxRss >= minRss ? maxRss - minRss : 0)
                  << "B tolerance=" << kTolerance << "B samples=" << rssSamples
                  << "/" << expectedRssSamples << " readFailures=" << rssReadFailures
                  << std::endl;
        if (rssVerdict == RssVerdict::kUnmeasured) {
            std::cerr << "Soak RSS gate FAILED: measurement is not trustworthy "
                         "(samples="
                      << rssSamples << "/" << expectedRssSamples
                      << ", readFailures=" << rssReadFailures
                      << ", min=" << minRss << "B). Refusing to report a pass "
                         "for a gate that did not measure anything." << std::endl;
            exit(1);
        }
        if (rssVerdict == RssVerdict::kDrift) {
            std::cerr << "Soak RSS drifted: min=" << minRss << " max=" << maxRss
                      << " tolerance=" << kTolerance << std::endl;
            exit(1);
        }
        TEST_PASS("3000-frame / 300-transition soak keeps RSS stable (measured on " +
                  std::string(rssPlatformName()) + ")");
    }

    // 2. Stressed save / load / rewind integrity.
    {
        for (int i = 0; i < 200; ++i) {
            engine.advanceToNextNode();
            const int32_t slot = static_cast<int32_t>(i % 4);
            if (!engine.saveGameSlot(slot)) {
                std::cerr << "Stressed save failed at iteration " << i << std::endl;
                exit(1);
            }
            if (i % 5 == 0) {
                if (!engine.loadGameSlot(slot) || !ringContains(engine.getCurrentNodeId())) {
                    std::cerr << "Stressed load lost integrity at iteration " << i << std::endl;
                    exit(1);
                }
            }
            if (i % 7 == 0) {
                engine.rewind(1);
                if (!ringContains(engine.getCurrentNodeId())) {
                    std::cerr << "Stressed rewind left the ring at iteration " << i << std::endl;
                    exit(1);
                }
            }
        }
        for (int32_t slot = 0; slot < 4; ++slot) {
            if (!engine.loadGameSlot(slot) || !ringContains(engine.getCurrentNodeId())) {
                std::cerr << "Post-stress slot reload failed for slot " << slot << std::endl;
                exit(1);
            }
        }
        TEST_PASS("200-iteration save/load/rewind stress preserves state");
    }

    // 3. Repeated lifecycle + audio-focus interruption cycles.
    {
        engine.resetToStartNode();
        uint64_t completedCycles = 0;
        for (int cycle = 0; cycle < 20; ++cycle) {
            const uint64_t frozen = engine.getCurrentNodeId();
            host->lifecycle = Rowl::Platform::LifecycleState::Suspended;
            host->focus = Rowl::Platform::AudioFocus::Lost;
            for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
            if (engine.getCurrentNodeId() != frozen || !engine.getAudio()->isOutputSuspended()) {
                std::cerr << "Interruption cycle " << cycle << " did not freeze work" << std::endl;
                exit(1);
            }
            host->lifecycle = Rowl::Platform::LifecycleState::Active;
            host->focus = Rowl::Platform::AudioFocus::Granted;
            host->input.push_back({Rowl::Platform::RuntimeInputEvent::Type::Advance});
            for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
            if (!ringContains(engine.getCurrentNodeId()) ||
                engine.getAudio()->isOutputSuspended()) {
                std::cerr << "Interruption cycle " << cycle << " did not resume" << std::endl;
                exit(1);
            }
            ++completedCycles;
        }
        if (completedCycles != 20) {
            std::cerr << "Interruption soak did not complete its cycles" << std::endl;
            exit(1);
        }
        TEST_PASS("20 suspend/lost-focus cycles freeze and resume without hanging");
    }

    // 5. N-ardisik save/load stres: 1000 iterasyon save->load->karsilastir
    // (4 slot round-robin). Her load'da node-id esitligi + slot JSON boyutu
    // orneklenir; her 20 iterasyonda stepId + dialogue_history boyutu da
    // dogrulanir. Toplam sure olculup gozlem olarak raporlanir.
    {
        constexpr int kStressIters = 1000;
        constexpr uintmax_t kMaxSaveBytes = 4ULL * 1024ULL * 1024ULL;
        constexpr int kDeepProbeEvery = 20; // her iterasyonda tam parse
                                            // CI butcesini sisirirdi
        Rowl::State::SessionPersistence probe(saveRoot); // salt-okur sondaj
        const auto stressStart = std::chrono::steady_clock::now();
        uintmax_t firstBytes = 0, lastBytes = 0, maxBytes = 0;
        int detailedProbes = 0;
        for (int i = 0; i < kStressIters; ++i) {
            engine.advanceToNextNode();
            const int32_t slot = static_cast<int32_t>(i % 4);
            if (!engine.saveGameSlot(slot)) {
                std::cerr << "N-stress save failed at iteration " << i << std::endl;
                exit(1);
            }
            const uint64_t expected = engine.getCurrentNodeId();
            // Orneklemli derin karsilastirma: dosyadaki stepId +
            // dialogue_history boyutu save aninda kaydedilir, load sonrasi
            // ayni dosyadan tekrar okunup karsilastirilir.
            const bool deepCheck = (i % kDeepProbeEvery == 0);
            uint64_t expectedStep = 0;
            size_t expectedHist = 0;
            if (deepCheck) {
                const auto savedProbe = probe.loadSlotDetailed(slot);
                if (!savedProbe.succeeded() || !savedProbe.state) {
                    std::cerr << "N-stress detailed probe failed at iteration " << i
                              << std::endl;
                    exit(1);
                }
                expectedStep = savedProbe.state->stepId;
                expectedHist = savedProbe.state->dialogueHistory
                                   ? savedProbe.state->dialogueHistory->size()
                                   : 0;
                if (savedProbe.state->activeNodeId != expected) {
                    std::cerr << "N-stress slot node disagrees at iteration " << i
                              << std::endl;
                    exit(1);
                }
                ++detailedProbes;
            }
            std::error_code sizeError;
            const uintmax_t bytes = std::filesystem::file_size(
                std::filesystem::path(saveRoot) /
                    ("save_slot_" + std::to_string(slot) + ".json"),
                sizeError);
            if (sizeError) {
                std::cerr << "N-stress slot file missing at iteration " << i << std::endl;
                exit(1);
            }
            if (i == 0) firstBytes = bytes;
            lastBytes = bytes;
            maxBytes = std::max(maxBytes, bytes);
            if (bytes >= kMaxSaveBytes) {
                std::cerr << "N-stress slot hit the 4MB read limit at iteration " << i
                          << " (" << bytes << " bytes)" << std::endl;
                exit(1);
            }
            if (!engine.loadGameSlot(slot)) {
                std::cerr << "N-stress load failed at iteration " << i << std::endl;
                exit(1);
            }
            if (engine.getCurrentNodeId() != expected ||
                !ringContains(engine.getCurrentNodeId())) {
                std::cerr << "N-stress state mismatch at iteration " << i << std::endl;
                exit(1);
            }
            if (deepCheck) {
                const auto loadedProbe = probe.loadSlotDetailed(slot);
                const size_t loadedHist = (loadedProbe.succeeded() && loadedProbe.state &&
                                           loadedProbe.state->dialogueHistory)
                                              ? loadedProbe.state->dialogueHistory->size()
                                              : static_cast<size_t>(-1);
                if (!loadedProbe.succeeded() || !loadedProbe.state ||
                    loadedProbe.state->stepId != expectedStep ||
                    loadedHist != expectedHist) {
                    std::cerr << "N-stress step/history mismatch at iteration " << i
                              << std::endl;
                    exit(1);
                }
            }
        }
        const auto stressSecs = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - stressStart).count();
        std::cout << "  [soak-stress] iters=" << kStressIters
                  << " first=" << firstBytes << "B last=" << lastBytes
                  << "B max=" << maxBytes << "B deepProbes=" << detailedProbes
                  << " secs=" << std::fixed
                  << std::setprecision(2) << stressSecs << std::endl;
        // Gozlem kilidi (2026-09-16: ~156 sn): butce 60 sn'in UZERINDE cikti.
        // Uretim koduna dokunulmadigindan bulgu olarak kilitlenir: sure
        // CPU-bagimli, her saveGameSlot 320x180 thumbnail'i PNG-encode+base64
        // yapar (~310KB dosya), her loadGameSlot tam JSON parse yapar
        // (engine.cpp:2031-2100 duz sirali yol) — iterasyon basi ~150ms
        // buradan gelir. KNOWN_ISSUES adayi: save thumbnail'ini
        // atlama/azaltma secenegi. Kilit: <300 sn (gozlemin ~1.9x'i).
        // Sanitizer derlemelerinde ayni is ~2.1x surer (CI gozlemi: 330.8 sn
        // ASan+UBSan altinda); enstrumantasyon yavaslamasini gercek
        // regresyondan ayirmak icin kilit sanitizer altinda 2 katina cikar.
        // Anahtar dosya basinda TEK KOSUL olarak hesaplanir
        // (ROWL_SANITIZER_BUILD) — bu blok kendi #if zincirini tasimaz,
        // boylece bolum 6 ile senkron kalamama yapisi kapatilir.
        //
        // BUTCE DEGERLERINE DOKUNULMADI (2026-10-04). Yukaridaki blok
        // 37a2449 ile BIT BIT aynidir; aradaki iki deneme geri alindi:
        //   * 975ac46: 300 -> 150 sn. Dayanagi RelWithDebInfo olcumuydu ve
        //     hicbir sekilde push edilmemisti; duzelttigi bir hata
        //     yasanmadi.
        //   * 31a9139: 300 -> 600 sn (NDEBUG tanimsiz kol). Dayanagi bu
        //     makinede alinmis 313.96 / 351.30 sn'lik iki Debug olcumuydu.
        //     Denetim bunlari CJRUTTU: (a) olcumler yuk bozukken alinmis
        //     (load avg 14-20 / 16 cekirdek; ayni is RelWithDebInfo'da
        //     25.4 sn olculmustu), (b) gercek CI'de tum ikili 203-261 sn'de
        //     YESIL bitiyor, yani 300 sn asilmiyor, (c) capraz alinan
        //     Windows 434 ms sayisi bolum 6'nin (history-growth) LOAD
        //     suresi ve bu butcenin gerekcesi DEGIL.
        // Yani "su an kac saniye suruyor" turunden bir DEGER kalibrasyonu
        // bu makinede uretilemez. Butce POLITIKASI (hangi runner, kac pay)
        // ayri bir is kalemidir ve temiz bir runner gerektirir; burada
        // butceye karar VERILMEZ, var olan deger korunur.
        //
        // KORUNAN (P1-11): report modu bir kacis yolu DEGIL. evaluateBudget()
        // kararini verir; TEST_PASS metni butceye dair iddiayi yalnizca
        // gercekten icindeysek tasir. Asilmis butce "OVER BUDGET, NOT
        // ENFORCED" der; "within budget" yazmaz.
#if defined(ROWL_SANITIZER_BUILD) || defined(_WIN32)
        constexpr double kStressBudgetSecs = 600.0;
#else
        constexpr double kStressBudgetSecs = 300.0;
#endif
        const BudgetVerdict stressVerdict =
            evaluateBudget(stressSecs, kStressBudgetSecs, perfFloorEnforced());
        if (stressVerdict == BudgetVerdict::kExceededEnforced) {
            std::cerr << "N-stress exceeded the locked observation budget ("
                      << kStressBudgetSecs << "s): " << stressSecs << "s" << std::endl;
            exit(1);
        }
        if (stressVerdict == BudgetVerdict::kExceededReported) {
            std::cerr << "  [soak-stress] BUDGET EXCEEDED AND NOT ENFORCED: " << stressSecs
                      << "s >= " << kStressBudgetSecs
                      << "s (ROWL_PERF_FLOOR=report). The pass line below does NOT "
                         "verify timing; treat this as an open perf budget debt."
                      << std::endl;
        }
        TEST_PASS("1000-iteration save/load round-robin preserves node-id under 4MB" +
                  budgetPassNote(stressVerdict, stressSecs, kStressBudgetSecs, "s"));
    }

    // 6. Tarihce-buyume olcumu: budama OLMADIGI icin (previousState sinirsiz
    // bagli liste, game_state.cpp) buyume bekleniyor; save JSON zinciri
    // serilestirmez (serializeJson yalnizca aktif state + dialogueHistory),
    // o yuzden dosya boyutu yalnizca dialogue_history kadar buyur (gorulen:
    // ~60B/adim), zincirin tamami RAM'de buyur. Patolojik esik: 4MB'a dayanma / saniyelik
    // save suresi (o durumda esigi genisletme, bulguyu raporla).
    {
        engine.resetToStartNode();
        if (!engine.saveGameSlot(0)) {
            std::cerr << "Growth baseline save failed" << std::endl;
            exit(1);
        }
        std::error_code baseError;
        const uintmax_t baseBytes = std::filesystem::file_size(
            std::filesystem::path(saveRoot) / "save_slot_0.json", baseError);
        if (baseError) {
            std::cerr << "Growth baseline slot file missing" << std::endl;
            exit(1);
        }
        constexpr int kGrowthAdvances = 1200;
        for (int i = 0; i < kGrowthAdvances; ++i) engine.advanceToNextNode();
        const auto saveStart = std::chrono::steady_clock::now();
        if (!engine.saveGameSlot(1)) {
            std::cerr << "Growth post-advance save failed" << std::endl;
            exit(1);
        }
        const double saveMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - saveStart).count();
        const uint64_t expectNode = engine.getCurrentNodeId();
        const auto loadStart = std::chrono::steady_clock::now();
        if (!engine.loadGameSlot(1) || engine.getCurrentNodeId() != expectNode) {
            std::cerr << "Growth post-advance load failed" << std::endl;
            exit(1);
        }
        const double loadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - loadStart).count();
        std::error_code grownError;
        const uintmax_t grownBytes = std::filesystem::file_size(
            std::filesystem::path(saveRoot) / "save_slot_1.json", grownError);
        if (grownError) {
            std::cerr << "Growth post-advance slot file missing" << std::endl;
            exit(1);
        }
        std::cout << "  [history-growth] advances=" << kGrowthAdvances
                  << " base=" << baseBytes << "B grown=" << grownBytes
                  << "B saveMs=" << std::fixed << std::setprecision(2) << saveMs
                  << " loadMs=" << std::fixed << std::setprecision(2) << loadMs
                  << std::endl;
        // Normal/patolojik karari: 4MB okuma sinirina dayanim veya saniyelik
        // save suresi patolojiktir (kirmizi birakma, raporla + esigi genis tut).
        if (grownBytes >= 4ULL * 1024ULL * 1024ULL || saveMs >= 1000.0) {
            std::cout << "  [history-growth] PATHOLOGICAL: slot near 4MB or "
                         "save took seconds — report, do not tighten" << std::endl;
        }
        // Kilit (2026-09-16 gozlemi: taban ~310KB, buyumus ~370KB — buyume
        // dialogue_history'nin her advance'te bir kayit uzamasindan gelir,
        // ~60B/adim; tabanin tamami 320x180 thumbnail base64'tir).
        // Ust-sinir ~2x payla 768KB, sure ~2x payla save <= 150ms, load <= 300ms.
        // Sanitizer derlemelerinde enstrumantasyon vergisi ~2.1x (CI gozlemi:
        // ASan+UBSan altinda load 316ms); N-stres kilidindeki ayni olcek
        // buruda da gecerli, yoksa kilit gercek regresyonla yavaslamayi
        // ayirt edemez.
        //
        // DENETIM (2026-10-04) — 63d767b'in YANLIS YORUMU VE ETKI.
        // 63d767b bu blogu `#if defined(NDEBUG)` anahtarina baglamisti ve
        // "DEGER DEGISTIRILMEDI... bir sayi uretilmez" demisti. IKISI DE
        // YANLISTI. Preprocessor ile olculdu (5 yapilandirma, eski = main
        // 5c07162'ye karsilik):
        //
        //   Debug, sanitizer yok : ESKI 150/300  ->  63d767b 300/600
        //   Debug + ASan/UBSan   : ESKI 300/600  ->  63d767b 300/600
        //   Debug + TSan         : ESKI 300/600  ->  63d767b 300/600
        //   Release (NDEBUG)     : ESKI 150/300  ->  63d767b 150/300
        //   RelWithDebInfo       : ESKI 150/300  ->  63d767b 150/300
        //
        // Yani CI'da butce kapisi calisan isler TAMAMEN Debug
        // (ci.yml 68/208/413/465/537), yani EN SIK CALISAN tip 2 kat
        // gevsetilmis oldu. "Degerler aynen korundu" degil; gercek olan:
        // "etkin degerler yapilandirmaya gore degisiyor ama HER
        // yapilandirmada eskisiyle ayni".
        //
        // Neden "yapilandirmaya gore degisiyor" dogru bir sey: taban
        // esikler 150/300 ms, olcek 2.0 yalnizca enstrumantasyonun zamani
        // yaklaşık ikiye katladigi kolu tanir. Bu bir politika, bir olcum
        // kalibrasyonu degil.
        //
        // DUZELTME: anahtar `#if defined(NDEBUG)` DEGIL, dosya basinda TEK
        // KOSUL olarak hesaplanan ROWL_SANITIZER_BUILD geri alindi. Boylece
        // (a) etkin degerler eski kodla BIT BIT ayni — hicbir esik gevsemez,
        // (b) bolum 1 / 5 / 6 artik ayni makroyu okudugu icin senkron
        // kalma hatasi YAPISAL olarak kapanir; uc ayri #if zinciri bir
        // tanesine indirgendi. Bu commit icin HICBIR sure olcumu
        // yapilmadi; kanit tamamen preprocessor okumasidir.
        //
        // BOYUT kilidi (768KB) DEGISTIRILMEDI ve her zaman enforced.
#ifdef ROWL_SANITIZER_BUILD
        constexpr double kGrowthTimeScale = 2.0;
#else
        constexpr double kGrowthTimeScale = 1.0;
#endif
        if (grownBytes > 768ULL * 1024ULL) {
            std::cerr << "History growth exceeded the locked upper bound: "
                      << grownBytes << "B" << std::endl;
            exit(1);
        }
        // Asil asim kosulu: save 150ms * scale, load 300ms * scale. Iki esik
        // farkli oldugu icin tek bir max()/budget ciftiyle temsil EDILEMEZ
        // (saveMs=200ms, scale=1 iken asim vardir ama max()=200 < 300 derdi);
        // bu yuzden asim hangi esigi astiysa O cift evaluateBudget'a girer.
        const bool saveOverBound = saveMs > 150.0 * kGrowthTimeScale;
        const bool loadOverBound = loadMs > 300.0 * kGrowthTimeScale;
        std::string growthPassNote;
        if (saveOverBound || loadOverBound) {
            // Tur-14: Faz 4.5 D1 karari — sure kilidi de fail-gate Linux-only.
            // CI-13'te Windows hosted runner'da load 434ms > 300ms: yavas
            // disk/CPU, urun regresyonu degil (boyut kilidi 372KB ile saglam).
            // P1-11 (2026-10-03): bu dal bir uyari basmamalidir. Onceki yazim
            // burada uyari yazip TEST_PASS'i kosulsuz "stays within the locked
            // size/time bound" metniyle cikardi; yani sure kilidi asildiginda
            // log "within ... bound" diyordu. Artik evaluateBudget karari
            // PASS metnini belirliyor.
            const double measured = saveOverBound ? saveMs : loadMs;
            const double bound = saveOverBound ? 150.0 * kGrowthTimeScale
                                               : 300.0 * kGrowthTimeScale;
            const BudgetVerdict growthVerdict =
                evaluateBudget(measured, bound, perfFloorEnforced());
            if (growthVerdict == BudgetVerdict::kExceededEnforced) {
                std::cerr << "Post-growth save/load exceeded the locked time bound (save<="
                          << 150.0 * kGrowthTimeScale << "ms load<=" << 300.0 * kGrowthTimeScale
                          << "ms): save=" << saveMs << "ms load=" << loadMs << "ms" << std::endl;
                exit(1);
            }
            if (growthVerdict == BudgetVerdict::kExceededReported) {
                std::cerr << "  [history-growth] TIME BOUND EXCEEDED AND NOT ENFORCED: save="
                          << saveMs << "ms load=" << loadMs << "ms (bound save<="
                          << 150.0 * kGrowthTimeScale << "ms load<=" << 300.0 * kGrowthTimeScale
                          << "ms). The size bound IS enforced; the time bound is not."
                          << std::endl;
            }
            growthPassNote =
                "1200-advance history growth stays within the locked SIZE bound" +
                budgetPassNote(growthVerdict, measured, bound, "ms") + " [time]";
        } else {
            // Ikisi de icinde: notun HER IKISINI de gercek birimiyle (ms)
            // bildirmesi daha durust — yalniz loadMs yazmak saveMs'i
            // belirsiz birakirdi. Degerler aynen 150.0/300.0 * kGrowthTimeScale.
            growthPassNote =
                "1200-advance history growth stays within the locked size/time bound"
                " [save " + formatBudgetValue(saveMs, "ms") + "ms <= " +
                formatBudgetValue(150.0 * kGrowthTimeScale, "ms") + "ms, load " +
                formatBudgetValue(loadMs, "ms") + "ms <= " +
                formatBudgetValue(300.0 * kGrowthTimeScale, "ms") + "ms]";
        }
        TEST_PASS(growthPassNote);
    }

    // 7. Derin-rewind clamp: 1200+ derinlikte rewind(2000) kok dugume
    // sessizce clamp'lenir; kokte rewind(1) -> false, state degismez.
    // Bos-tarihce (tek dugum, reset sonrasi) rewind -> false.
    // (Kendi zincirini kurar: bolum 6 chain-less load ile bittigi icin.)
    {
        engine.resetToStartNode();
        constexpr int kDeepAdvances = 1200;
        for (int i = 0; i < kDeepAdvances; ++i) engine.advanceToNextNode();
        // 1200 advance: 8'li ring'de kok (node 1) + 1200 % 8 == 0 adim.
        if (!engine.rewind(2000)) {
            std::cerr << "Deep rewind reported no movement on a 1200-deep chain"
                      << std::endl;
            exit(1);
        }
        if (engine.getCurrentNodeId() != 1 || !ringContains(engine.getCurrentNodeId())) {
            std::cerr << "Deep rewind did not clamp to the root node" << std::endl;
            exit(1);
        }
        const uint64_t rootNode = engine.getCurrentNodeId();
        if (engine.rewind(1)) {
            std::cerr << "Rewind at the root reported movement" << std::endl;
            exit(1);
        }
        if (engine.getCurrentNodeId() != rootNode) {
            std::cerr << "Root rewind mutated state" << std::endl;
            exit(1);
        }
        engine.resetToStartNode();
        const uint64_t freshNode = engine.getCurrentNodeId();
        if (engine.rewind(1)) {
            std::cerr << "Rewind on a single-node history reported movement" << std::endl;
            exit(1);
        }
        if (engine.getCurrentNodeId() != freshNode) {
            std::cerr << "Single-node rewind mutated state" << std::endl;
            exit(1);
        }
        TEST_PASS("deep rewind clamps to root; root/empty-history rewind is a silent no-op");
    }

    // 8. Ayni-kare yarisi: save->load->rewind->load sirali cagrilar (tek
    // thread; save/load/rewind yolunda kilit yok, sirali guvenlik). D08 (a):
    // load serilestirilmis sinirli gecmisten ("history") zincir kurar, o
    // yuzden ortadaki rewind(1) TRUE doner ve bir onceki halkaya iner
    // (stepId tam 1 azalir); son load tutarli olmalidir.
    {
        engine.resetToStartNode();
        engine.advanceToNextNode();
        if (!engine.saveGameSlot(2)) {
            std::cerr << "Same-frame save failed" << std::endl;
            exit(1);
        }
        if (!engine.loadGameSlot(2) || !ringContains(engine.getCurrentNodeId())) {
            std::cerr << "Same-frame first load failed" << std::endl;
            exit(1);
        }
        const uint64_t loadedNode = engine.getCurrentNodeId();
        const uint64_t loadedStep = engine.getCurrentStepId();
        // 8'li halkada oncul: ((loaded-1+7)%8)+1.
        const uint64_t wantNode = ((loadedNode - 1 + 7) % 8) + 1;
        if (!engine.rewind(1)) {
            std::cerr << "Same-frame rewind failed on a history-chained loaded state"
                      << std::endl;
            exit(1);
        }
        if (engine.getCurrentNodeId() != wantNode) {
            std::cerr << "Same-frame rewind did not reach the predecessor ring" << std::endl;
            exit(1);
        }
        if (engine.getCurrentStepId() + 1 != loadedStep) {
            std::cerr << "Same-frame rewind did not step back exactly once" << std::endl;
            exit(1);
        }
        if (!engine.loadGameSlot(2) || engine.getCurrentNodeId() != loadedNode) {
            std::cerr << "Same-frame final load diverged" << std::endl;
            exit(1);
        }
        TEST_PASS("same-frame save/load/rewind/load sequence is status-clean and consistent");
    }

    // 9. Kesinti+save birlesik (bolum 3 deseni): suspend/freeze ortasinda
    // save -> gozlem kilidi BASARI (saveGameSlot yolunda lifecycle kapisi
    // yok, engine.cpp:2031-2100 duz sirali). Davranis degistirme, gozlem+belge.
    {
        engine.resetToStartNode();
        engine.advanceToNextNode();
        const uint64_t frozen = engine.getCurrentNodeId();
        host->lifecycle = Rowl::Platform::LifecycleState::Suspended;
        host->focus = Rowl::Platform::AudioFocus::Lost;
        for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
        if (engine.getCurrentNodeId() != frozen) {
            std::cerr << "Suspend-freeze did not hold before mid-freeze save" << std::endl;
            exit(1);
        }
        if (!engine.saveGameSlot(3)) {
            std::cerr << "Mid-freeze save was rejected (locked expectation: success)"
                      << std::endl;
            exit(1);
        }
        host->lifecycle = Rowl::Platform::LifecycleState::Active;
        host->focus = Rowl::Platform::AudioFocus::Granted;
        host->input.push_back({Rowl::Platform::RuntimeInputEvent::Type::Advance});
        for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
        if (!engine.loadGameSlot(3) || !ringContains(engine.getCurrentNodeId())) {
            std::cerr << "Post-resume load of the mid-freeze save failed" << std::endl;
            exit(1);
        }
        if (engine.getCurrentNodeId() != frozen) {
            std::cerr << "Mid-freeze save did not capture the frozen node" << std::endl;
            exit(1);
        }
        TEST_PASS("mid-freeze save succeeds and reloads the frozen node after resume");
    }

    engine.shutdown();

    // 4. Write-failure safety: read-only save dir, old slot preserved.
    {
        const std::string dir = uniqueTempDir("rowl_rc_readonly_");
        Rowl::State::SessionPersistence store(dir);
        auto initial = Rowl::State::GameState::createInitialState(4);
        if (!store.saveSlot(initial, 3)) {
            std::cerr << "Could not seed the write-failure fixture" << std::endl;
            exit(1);
        }
        const auto slotPath = std::filesystem::path(dir) / "save_slot_3.json";
        std::ifstream seeded(slotPath, std::ios::binary);
        const std::string before((std::istreambuf_iterator<char>(seeded)),
                                 std::istreambuf_iterator<char>());
        if (before.empty()) {
            std::cerr << "Seeded save slot is empty" << std::endl;
            exit(1);
        }

        std::error_code permError;
        std::filesystem::permissions(dir, std::filesystem::perms::owner_read |
                                              std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::replace, permError);
        if (!permError && probeWriteBlocked(dir)) {
            auto newer = Rowl::State::GameState::createNextState(initial, 5);
            if (store.saveSlot(newer, 3) || store.saveSlot(newer, 4)) {
                std::cerr << "Save into a read-only directory reported success" << std::endl;
                exit(1);
            }
            std::ifstream after(slotPath, std::ios::binary);
            const std::string preserved((std::istreambuf_iterator<char>(after)),
                                        std::istreambuf_iterator<char>());
            if (preserved != before) {
                std::cerr << "Failed save corrupted the previous valid slot" << std::endl;
                exit(1);
            }
            if (std::filesystem::exists(std::filesystem::path(dir) / "save_slot_3.json.tmp") ||
                std::filesystem::exists(std::filesystem::path(dir) / "save_slot_4.json") ||
                std::filesystem::exists(std::filesystem::path(dir) / "save_slot_4.json.tmp")) {
                std::cerr << "Failed save left stray slot or temp files" << std::endl;
                exit(1);
            }
            auto reloaded = store.loadSlotDetailed(3);
            if (!reloaded.succeeded() || reloaded.state->activeNodeId != 4) {
                std::cerr << "Preserved slot did not reload after write failure" << std::endl;
                exit(1);
            }
            TEST_PASS("read-only save dir fails safe and preserves the valid slot");
        } else {
            std::cout << "  (privileged filesystem: read-only gate skipped, "
                         "crash-safety probed via blocked path instead)" << std::endl;
            // A regular file where the directory must be: create_directories
            // throws inside saveSlot and is contained as `false`, everywhere.
            const std::string blocker = uniqueTempDir("rowl_rc_blocker_") + ".file";
            {
                std::ofstream touch(blocker);
                touch << "block";
            }
            Rowl::State::SessionPersistence blocked(blocker);
            if (blocked.saveSlot(initial, 0)) {
                std::cerr << "Save through a file-blocked directory reported success" << std::endl;
                exit(1);
            }
            std::error_code ec;
            std::filesystem::remove(blocker, ec);
            TEST_PASS("blocked save path fails safe without crashing");
        }

        std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, permError);
        std::error_code cleanupError;
        std::filesystem::remove_all(dir, cleanupError);
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(saveRoot, cleanupError);
}
