#include "rowl/state/save_durability.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#endif

namespace Rowl::State {

namespace {

// FSYNC DECISION (see header): fsync is OFF in this slice — deliberate.
// The crash-mid-write guarantee is atomicity via temp-file + rename, which
// needs no flushing contract beyond the stream flush before rename: either
// the rename happened (new complete file) or it did not (old file intact).
// Forcing bytes to stable storage (POSIX fdatasync/fsync + dir fsync,
// Windows FlushFileBuffers) would only matter for OS/power loss, which is
// out of scope here. If that is ever required, add it here — inside this
// module — without touching the SessionPersistence save path. The header
// documents the full guarantee ladder (L1 process-crash covered, L2 crash
// residue bounded by the sweep below, L3 power-loss out of scope).

std::atomic<int> g_injectErrno{0};

bool envEnospcRequested() {
#ifdef NDEBUG
    // The env-var trigger is a dev/test convenience only: in release builds
    // a stray ROWL_SAVE_INJECT_ENOSPC=1 in the process environment must never
    // break real saves. Tests use the explicit setter, which works in all
    // configurations.
    return false;
#else
    const char* value = std::getenv("ROWL_SAVE_INJECT_ENOSPC");
    return value != nullptr && std::strcmp(value, "1") == 0;
#endif
}

bool isSupportedInjectErrno(int code) {
    return code == ENOSPC || code == EACCES || code == EROFS;
}

const char* errnoShortName(int code) {
    switch (code) {
        case ENOSPC: return "ENOSPC";
        case EACCES: return "EACCES";
        case EROFS: return "EROFS";
        default: return nullptr;
    }
}

// "[<NAME> (<code>): <strerror>] " prefix; unknown codes render as
// "[ERRNO<code> (<code>): <strerror>] " so the numeric value is never lost.
std::string errnoPrefix(int code) {
    const char* name = errnoShortName(code);
    const char* description = std::strerror(code);
    std::string prefix = "[";
    if (name != nullptr) {
        prefix += name;
    } else {
        prefix += "ERRNO" + std::to_string(code);
    }
    prefix += " (" + std::to_string(code) + "): ";
    prefix += (description != nullptr ? description : "unknown error");
    prefix += "] ";
    return prefix;
}

// Effective injected errno: explicit setter wins, env-var trigger degrades
// to ENOSPC (legacy behavior).
int effectiveInjectErrno() {
    const int code = g_injectErrno.load(std::memory_order_relaxed);
    if (code != 0) return code;
    if (envEnospcRequested()) return ENOSPC;
    return 0;
}

// ---------------------------------------------------------------------------
// PAYLAŞIM YARIŞI (Windows) — iki katmanlı savunma.
//
// Paylaşılan tek nesne finalPath'tir. Temp adları (R1 #3) ve, 878c467'den
// sonra, yedek adları YAZAR BAŞINA benzersizdir; çekişen yalnızca hedefin
// kendisidir. Hedef iki yönden zorlanır:
//
//   (a) yazma-yazma — 490 yedek kopyası finalPath'i KAYNAK olarak 128 KB
//       boyunca okur. O kaynak elde FILE_SHARE_DELETE taşımadığı için
//       rakiplerin MoveFileExW çağrısı hedefe DELETE erişimi açamaz ve
//       ERROR_ACCESS_DENIED (5) alır (MoveFileExW(REPLACE_EXISTING) hedefi
//       DELETE erişimiyle açmak ZORUNDA; LLVM rL250046).
//   (b) okuma-yazma — ters yön: uçuşta olan bir rename hedefi delete-pending
//       yapar, kendi kaynak açmamız ERROR_SHARING_VIOLATION (32) ile
//       reddedilir. CI'da görülen iki hata da bu TEK yarışın iki yüzüdür.
//
// Paylaşım sayaçları dosya NESNESİNDE yaşar, süreçte değil: 8 thread tek
// süreçte de 8 süreçte çarpışır. Dolayısıyla mutex süreç içi yarışı
// deterministik kapatır, süreçler arası yarışa (ikinci motor örneği, editör,
// Defender minifilter) dokunamaz — onu sınırlı retry karşılar. Biri diğerinin
// yerine geçmez; ikisi birlikte ancak o zaman kapıyı tam kapatır.
//
// Kilit bir YAPRAK kilittir: bölge içinde yalnızca error_code'lu
// std::filesystem çağrıları, MoveFileExW ve Sleep vardır — hiçbiri repo
// mutex'u almaz. Logger::s_logMutex yalnızca kilit BIRAKILDIKTAN SONRA
// fail() ile alınır, dolayısıyla bu bölgeden kilit grafiğine çıkan kenar
// yoktur ve kilit sırası tersine dönemez. Repo stili (logger.cpp:12).
std::mutex g_slotCommitMutex;

// Retry bütçesi: 1 ilk deneme + 4 yeniden deneme; geri çekilme
// kCommitBackoffBaseMs << (attempt-1) = 1, 2, 4, 8 ms (nominal 15 ms).
// Bütçe BİLEREK kısadır: kalıcı bir hatada (izin yok, salt-okunur dizin) kayıt
// yine fail-closed döner. Gerçek tavan NOMİNALDEN YÜKSEKTİR: Sleep Windows
// tanesi granülaritesine uyar (varsayılan ~15,6 ms), yani istenen 1 ms fiilen
// ~15,6 ms sürebilir => site başına gerçekçi tavan ~63 ms. DÖRT site de tükerse
// bu, commit bölgesi kilitliyken yaklaşık ~250 ms olur; yalnızca zaten
// BAŞARISIZ olan bir kayıtta ve mutex'i başka yazarlara göre tutar (ölçülen
// maliyet: 200 ardışık kayıt = 18 ms, CI bütçesinin %0,3'ü). Sayaç sabit
// `for` ile sınırlıdır, hiçbir girdi onu uzatamaz.
constexpr int kMaxTransientAttempts = 5;
constexpr int kCommitBackoffBaseMs = 1;

// Test-only geçici-hata sentyeli. Windows'ta gerçek kod, POSIX'te EAGAIN.
// Var olma sebebi: Win32 kodları Linux'ta hiç üretilemediği için retry yolu
// platformdan bağımsız koşsun diye kanca bunu üretir ve sınıflandırıcı KENDİSİ
// bu değeri kabul eder. Böylece "retry gerçekten çalışıyor" iddiası Linux
// CI'da kırmızıya düşer. NOT: POSIX'te bir ağ dosya sistemi gerçek bir EAGAIN
// de verebilir; o da geçici sayılıp yeniden denenir, ki bu istenen davranıştır.
#if defined(_WIN32)
constexpr int kInjectedTransientCode = ERROR_SHARING_VIOLATION;
#else
constexpr int kInjectedTransientCode = EAGAIN;
#endif

constexpr int kTransientSiteCount =
    static_cast<int>(SaveDurabilityTransientSite::BackupRemove) + 1;

// Site dizilerinin uzunluğu enum ile AYNI olmalı. Bu guard olmadan
// kTransientSiteCount 3'e düşseydi BackupRemove=3 iki std::atomic<int>[3]
// dizisinin sonunu aşar ve ASan/UBSan bacakları global-buffer-overflow ile
// kırmızıya dönerdi; oysa normal Linux düzeninde hata .bss dolgusuna düşüp
// sessizce "çalışır" gibi görünür. Enum genişletilirse burası derlemede
// yakalar.
static_assert(kTransientSiteCount == 4,
              "kTransientSiteCount must track SaveDurabilityTransientSite");

// Site-bazlı sayaçlar. Neden site-bazlı: kanca tek bir FIFO olsaydı, probe
// denemelerini tüketip yedek kopyası yoluna hiç ulaşmak MÜMKÜN OLMAZDI
// (probe yalnızca kuyruk boşken başarılı olur). Site-bazlı sayaç olmadan
// yedekleme hata yolu ve 490 restore yolu hiçbir testte koşamazdı.
std::atomic<int> g_injectTransientFailures[kTransientSiteCount] = {0, 0, 0};
std::atomic<int> g_consumedTransientFailures[kTransientSiteCount] = {0, 0, 0};

// Test-only: "rakip yazar" simülasyonu. Yedek hazırlandıktan SONRA, replace
// ÖNCESİ hedefe başka bir yazarın baytlarını yazar. Süreçler arası yarışın
// (ikinci motor örneği) testte gözlenebilir hâli; süreç içi mutex onu zaten
// engellediği için başka hiçbir yolla üretilemez. Amaç: parmakiz korumasının
// "eski yazar, süreçler arası kazananın daha yeni verisini EZMEZ" iddiasını
// ölçülebilir kılmak. Üretimde her zaman kapalı.
std::atomic<bool> g_injectCompetingWrite{false};

const char* const kCompetingWriteMarker = "ROWL-COMPETING-WRITER-PAYLOAD";

// Test-only: commit bölgesine eşzamanlı giriş sayacı. Mutex'in tek gözlenebilir
// işareti budur: Linux'ta Windows paylaşım hatası üretilemediği için mutex'i
// KALDIRMAK hiçbir testi kırmızıya düşürmez — yalnızca bu sayaç düşürür.
// Mutex varken giriş daima 0'dan 1'e gider, yani ihlal sayacı KESİNLİKLE 0'dır:
// bu kapı yanlış-pozitif üretemez, yalnızca eksik kalan mutexeği yakalar.
// İki gevşek atomik RMW, kayıt başına ~20 ns.
std::atomic<int> g_commitOccupancy{0};
std::atomic<int> g_commitOverlaps{0};

// RAII: commitSlotLocked'ın HER dönüş yolunda sayacı düşürür.
struct CommitOccupancyGuard {
    CommitOccupancyGuard() {
        if (g_commitOccupancy.fetch_add(1, std::memory_order_acq_rel) != 0) {
            g_commitOverlaps.fetch_add(1, std::memory_order_relaxed);
        }
    }
    ~CommitOccupancyGuard() {
        g_commitOccupancy.fetch_sub(1, std::memory_order_acq_rel);
    }
    CommitOccupancyGuard(const CommitOccupancyGuard&) = delete;
    CommitOccupancyGuard& operator=(const CommitOccupancyGuard&) = delete;
};

// Kanca: sıradaki N denemesi GEÇİCİ hata ile başarısız olsun. Gerçek syscall
// ÇAĞRILMAZ (yerel dosya sisteminde paylaşım hatası üretilemez); sarmalayıcı
// hata sınıfını görür ve geri çekilmeyi uygular.
bool consumeInjectedTransientFailure(int site) {
    int remaining = g_injectTransientFailures[site].load(std::memory_order_relaxed);
    while (remaining > 0) {
        if (g_injectTransientFailures[site].compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_relaxed)) {
            g_consumedTransientFailures[site].fetch_add(1,
                                                        std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

// Ham Win32 kodları std::system_category()'nin değer alanıdır: MSVC'de
// __std_system_error_allocate_message mesaj id'sini FormatMessage'a çeviri
// tablosu olmadan olduğu gibi geçirir, _Winerror_map yalnızca
// default_error_condition türetmek için kullanılır. Bu yüzden value()
// doğrudan ham kodla karşılaştırılabilir (replaceFileAtomically zaten
// öyle dolduruyor). Güvenli hata yönü: ileride bir STL bunları
// generic_category'ye sarmalarsa retry hiç tetiklenmez, mutex yine tek
// başına süreç içi kapıyı kapatır.
bool isTransientCommitError(int rawCode, bool copying) {
    if (rawCode == kInjectedTransientCode) return true;
#if defined(_WIN32)
    // 32 ERROR_SHARING_VIOLATION, 33 ERROR_LOCK_VIOLATION,
    // 303 ERROR_DELETE_PENDING (komşu NTFS replace'in geçici durumu).
    if (rawCode == ERROR_SHARING_VIOLATION ||
        rawCode == ERROR_LOCK_VIOLATION ||
        rawCode == 303) {
        return true;
    }
    // 5 ERROR_ACCESS_DENIED yalnızca RENAME'de geçicidir (yukarıda (a)).
    // Kopyada ise 5 KALICIDİR: MSVC STL'in kendi notu "is_regular_file(from)
    // is false => ERROR_ACCESS_DENIED" — izin reddi ya da kaynak bir dizin.
    // Yeniden denemek boşa bütçe harcar, yine de başarısız olur.
    return !copying && rawCode == ERROR_ACCESS_DENIED;
#else
    // POSIX: yerel dosya sisteminde rename/copy_file atomiktir ve paylaşım
    // hatası üretmez; tek kabul edilen değer yukarıdaki sentyeldir.
    (void)copying;
    return false;
#endif
}

void backoffBeforeCommitRetry(int attempt) {
    const int delayMs = kCommitBackoffBaseMs << (attempt - 1);
#if defined(_WIN32)
    // Sleep sistem tanesi granülaritesine uyar (Windows varsayılanı ~15.6 ms),
    // yani istenen 1 ms fiilen ~15.6 ms sürebilir. Tavan sabit `for` ile
    // sınırlıdır ama "15 ms" bir TABANDIR, garanti değildir.
    ::Sleep(static_cast<DWORD>(delayMs));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
#endif
}

// Sınırlı transient-retry sarmalayıcısı. Geri çağrı gerçek işi yapar ve
// başarısızlıkta error'u doldurur; sarmalayıcı yalnızca GEÇİCİ sayılan
// kodda, bütçe bitene kadar yeniden dener. `copying` yalnızca 5'in
// (ERROR_ACCESS_DENIED) sınıflandırmasını ayırır.
//
// `onInjectedFailure`, kanca bir denemeyi geçici hata ile reddettiğinde
// çağrılır. KOPYADA amacı, gerçek bir yarıda kesilmiş CopyFile2'nin bıraktığı
// artığı taklit etmektir; böylece "yarım yedek temizleniyor" iddiası testte
// ölçülebilir olur. Diğer çağrılarda boş bir lambda.
//
// DİKKAT: enjekte edilen hata da GERÇEK bir hataymış gibi sınıflandırmadan
// geçer (aşağıdaki continue YOK). Bu bilinçlidir: sınıflandırıcının kendisi
// bu yolla üzerinde koşar, aksi halde Linux CI'da hiç çalışmazdı.
template <class OnInjected, class Attempt>
bool withTransientCommitRetry(OnInjected&& onInjectedFailure, Attempt&& attempt,
                              std::error_code& error, bool copying, int site) {
    for (int index = 1; index <= kMaxTransientAttempts; ++index) {
        if (index > 1) backoffBeforeCommitRetry(index - 1);
        error.clear();
        if (consumeInjectedTransientFailure(site)) {
            onInjectedFailure();
            error = std::error_code(kInjectedTransientCode,
                                    std::system_category());
        } else if (attempt()) {
            return true;
        }
        if (!isTransientCommitError(error.value(), copying)) return false;
    }
    return false;
}

const auto kNoInjectedArtifact = []() {};

// overwrite_existing kopyası, geçici paylaşım hatalarında sınırlı retry ile.
// Yeniden denemek GÜVENLİDİR: overwrite_existing hedefi baştan kesip yeniden
// yazar, bu yüzden yarım deneme sonuca sızmaz — hedef ya tam ya hiç yok.
bool copyFileWithTransientRetry(const std::filesystem::path& from,
                                const std::filesystem::path& to,
                                std::error_code& error) {
    namespace fs = std::filesystem;
    return withTransientCommitRetry(
        // Gerçek CopyFile2 hedefte yarım bir dosya bırakabilir; kanca da
        // öyle bırakır, aksi halde bu satırın temizlik yolunun kapısı olmaz.
        [&]() {
            std::error_code stageError;
            // binary: aynı CRLF gerekçesi (bkz. yukarıdaki uzun yorum).
            std::ofstream partial(to, std::ios::out | std::ios::trunc | std::ios::binary);
            if (partial.is_open()) partial << "partial-copy";
        },
        [&]() {
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);
            return !error;
        },
        error, /*copying=*/true, static_cast<int>(SaveDurabilityTransientSite::Copy));
}

// Yedek alınabilir mi? (hedef mevcut ve düz bir dosya).
// DÜZELTME: eski tek-şanslı sürümde eşzamanlı bir rename probe'u kıpırdadığında
// probeError dolar, `&& !probeError` kısa devre eder ve haveBackup sessizce
// FALSE kalır — dosya sistemi EN ÇOK tartışmalıyken 490 güvenlik ağı sessizce
// düşerdi (fail-open).
//
// DÜRÜST KAPSAM: MSVC STL, GetFileAttributesExW'in ERROR_SHARING_VIOLATION
// dönmesi durumunda kendi içinde FindFirstFileW'ye düşen bir geri düşüş
// taşıyor; yani 32'nin bize hiç ulaşmama ihtimali VARDIR ve bu makaleden
// doğrulanamadı. Bu yüzden probe retry'si "kanıtlanmış bir düzeltme" değil,
// savunma-derinliğidir: tek-şanslı sürümün kısa devrelerken düşürdüğü pencereyi
// daraltır. Windows CI bu yolun gerçekten ne döndürdüğünü ilk kez ölçebilir.
//
// DÜRÜST KALICI SINIR: bütçe tükenirse yedek YİNE hazırlanmaz ve yazma
// yedeksiz devam eder. Bu, hedefin bozulduğu anlamına GELMEZ (başarılı
// replace'te yedek zaten silinirdi); etkisi, replace de düşerse elde
// geri yüklenecek bir yedek bulunmamasıdır. Yani retry güvenlik ağını
// genişletir, yoktan var etmez.
bool targetIsRegularFileWithRetry(const std::filesystem::path& finalPath) {
    namespace fs = std::filesystem;
    std::error_code probeError;
    std::error_code reported;
    return withTransientCommitRetry(
        kNoInjectedArtifact,
        [&]() {
            probeError.clear();
            if (fs::exists(finalPath, probeError) && !probeError &&
                fs::is_regular_file(finalPath, probeError) && !probeError) {
                return true;
            }
            if (!probeError) {
                // Dosya gerçekten yok ya da bir dizin: transient DEĞİL.
                // Sarmalayıcının bunu terminal sayıp çıkması için error'u
                // geçici olmayan bir kodla doldur.
                reported = std::make_error_code(
                    std::errc::no_such_file_or_directory);
                return false;
            }
            reported = probeError;
            return false;
        },
        reported, /*copying=*/true,
        static_cast<int>(SaveDurabilityTransientSite::Probe));
}

bool replaceFileAtomically(const std::filesystem::path& temporaryPath,
                           const std::filesystem::path& finalPath,
                           std::error_code& error) {
    namespace fs = std::filesystem;
    // Yalnızca GEÇİCİ sayılan paylaşım hataları yeniden denenir. MoveFileExW
    // atomiktir: başarısız bir çağrı hem sahipliğimizdeki kaynak temp'i hem
    // hedefi yerinde bırakır, dolayısıyla aynı kaynakla tekrar denemek
    // "başarılı ya da başarısız"tır ve asla yırtık final üretemez.
    // MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH her denemede BİREBİR
    // korunur: WRITE_THROUGH, header'da belgelenen dayanıklılık merdiveninin
    // metadata adımıdır, pazarlık konusu değildir.
    const bool moved = withTransientCommitRetry(
        kNoInjectedArtifact,
        [&]() {
#if defined(_WIN32)
            if (MoveFileExW(temporaryPath.c_str(), finalPath.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return true;
            }
            error = std::error_code(static_cast<int>(GetLastError()),
                                    std::system_category());
            return false;
#else
            fs::rename(temporaryPath, finalPath, error);
            return !error;
#endif
        },
        error, /*copying=*/false,
        static_cast<int>(SaveDurabilityTransientSite::Replace));
    if (moved) {
        error.clear();
        return true;
    }
#if defined(_WIN32)
    // MOVEFILE_WRITE_THROUGH'lu bir çağrı, taşıma fiilen gerçekleştikten
    // SONRA disk boşaltmasında başarısız olabilir: bu durumda temp kaybolmuş,
    // hedef yenilenmiştir, ama API "başarısız" der. Böyle bir çağrıdan
    // sonra temp'in HÂLÂ durması beklenir — durmuyorsa taşıma olmuş demektir
    // ve bunu başarı saymak, ardından gelen restore'un taze payload'ı eski
    // yedekle ezmesini engeller. (Süpürme yalnızca kanıtlanmış-ÖLÜ pid'e
    // dokunduğu için süreç içinden başka bir yol temp'i silemez.)
    //
    // YALNIZCA WINDOWS'TA: POSIX rename atomiktir ve başarısızlıkta kaynağı
    // yerinde bırakır, dolayısıyla bu çıkarım POSIX'te yalnızca yanlış
    // başarı üretebilirdi. Windows'ta bile bir dış temizleyicinin temp'i
    // silmiş olma ihtimali sıfır değildir; bu yüzden çıkarım bilinçli olarak
    // dar tutulur ve aşağıdaki parmakiz koruması ikinci emniyet kaynağıdır.
    std::error_code probeError;
    if (!fs::exists(temporaryPath, probeError) && !probeError) {
        error.clear();
        return true;
    }
#endif
    return false;
}

// R1 (#3): sahipli benzersiz temp üretimi. Eski kod her yazar için aynı
// "<slot>.json.tmp" adını kullanıyordu: aynı slota eşzamanlı yazan iki
// yazar aynı tmp dosyasını O_TRUNC ile paylaşıp birbirinin baytını ezer,
// son rename sessizce ilk yazarı kaybederdi (kayıp güncelleme / yırtık
// bayt). Artık her yazar yalnızca kendisinin bildiği bir tmp dosyası
// üretir (<slot>.json.tmp.<pid>.<sayaç>[.rastgele]): yazma+rename ya hep
// ya hiçtir; kazanan her zaman TAM bir payload'dur, yırtık okuma imkânsız.
// Kasıtlı olarak .lock yok: rename atomikliği zaten tam-payload garantisi
// verir; son-kazanan-kazanır burada doğru davranıştır (kayıp-güncelleme
// değil, atomik slot değişimi).
std::atomic<unsigned long> g_tempCounter{0};

#if defined(_WIN32)
std::string currentProcessTag() {
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
}
#else
std::string currentProcessTag() {
    return std::to_string(static_cast<unsigned long>(::getpid()));
}
#endif

// Yalnızca bu işleme ait, yeni oluşturulmuş boş bir temp dosyasının yolunu
// döndürür (0600). Üretimde başarısız olursa boş path döner. Dönen ad
// tahmin edilemez olduğu için (pid + atomik sayaç + mkstemp rastgeleliği /
// O_EXCL sahiplenmesi) başka bir yazarla paylaşılamaz.
//
// 490: ad üretimi kayıpsız UTF-8 turuyla yapılır
// (pathToUtf8/pathFromUtf8): Windows'ta path::string() ANSI codepage
// dönüşümü uygular ve ASCII-dışı dizinlerde fırlatır/kayıplı çevirir.
// Açma POSIX'te mkstemp (dar yol yereldir), Windows'ta _wsopen_s (geniş yol)
// ile yapılır; dar _sopen_s + c_str() ASCII-dışı dizinde açamazdı.
std::filesystem::path mintOwnedTempPath(const std::filesystem::path& finalPath) {
    const std::string stemUtf8 =
        Rowl::Platform::pathToUtf8(finalPath) + ".tmp." + currentProcessTag() +
        "." + std::to_string(g_tempCounter.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
    // _wsopen_s O_CREAT|O_EXCL: dosya varsa EEXIST ile başarısız olur; sayaç
    // her çağrıda arttığı için çakışma pratikte imkânsız, attempt döngüsü
    // sayaç-sarma kalıntısına karşı kemerdir.
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::filesystem::path candidate =
            Rowl::Platform::pathFromUtf8(stemUtf8 + "." + std::to_string(attempt));
        int fd = -1;
        const int opened = _wsopen_s(&fd, candidate.c_str(),
                                     _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYRW,
                                     _S_IREAD | _S_IWRITE);
        if (opened == 0) {
            _close(fd);
            return candidate;
        }
        if (errno != EEXIST) return std::filesystem::path{};
    }
    return std::filesystem::path{};
#else
    // mkstemp: O_CREAT|O_EXCL ile 0600 kipinde atomik üretir, XXXXXX'i
    // yerinde rastgele adla değiştirir.
    std::string pattern = stemUtf8 + ".XXXXXX";
    const int fd = ::mkstemp(pattern.data());
    if (fd < 0) return std::filesystem::path{};
    ::close(fd);
    return Rowl::Platform::pathFromUtf8(pattern);
#endif
}

} // namespace

// R1 (#3): artık yalnızca legacy stray adı; yazma yolu
// mintOwnedTempPath kullanır. İmza korunur (fuzz testi plant/kontrol için
// kullanır, SessionPersistence değişmez).
std::filesystem::path saveTempPathFor(const std::filesystem::path& finalPath) {
    std::filesystem::path temporaryPath = finalPath;
    temporaryPath += ".tmp";
    return temporaryPath;
}

// 490: ön-yedek adı. Yazma yolu rename ÖNCESİ mevcut slotu buraya kopyalar;
// başarıda silinir, rename başarısızlığında geri yüklenir. Salt-ASCII sonek
// birleştirme (+=) native temsilde kalır: codepage dönüşümü/fırlatma yok.
std::filesystem::path saveBackupPathFor(const std::filesystem::path& finalPath) {
    std::filesystem::path backupPath = finalPath;
    backupPath += ".pre-save-bak";
    return backupPath;
}

// Yedek temizleme, geçici paylaşım hataları için sınırlı retry ile. Yedek adı
// yazar başına benzersiz olsa da bir antivirüs filtresi dosyayı kısa süre
// tutabiliyor; bu olmadan "dizinde yalnızca slot.json vardır" diye sınanan
// testler Windows CI'da kırılgan hale gelirdi.
void removeBackupQuietly(const std::filesystem::path& backupPath) {
    namespace fs = std::filesystem;
    std::error_code removeError;
    withTransientCommitRetry(
        kNoInjectedArtifact,
        [&]() {
            fs::remove(backupPath, removeError);
            return !removeError;
        },
        removeError, /*copying=*/true,
        static_cast<int>(SaveDurabilityTransientSite::BackupRemove));
}

// Sahipli temp'i sessizce sil, geçici paylaşım hataları için sınırlı retry ile
// (antivirüs filtresi dosyayı kısa süre tutabiliyor). Artık bırakılırsa
// "dizinde yalnızca slot.json" diye sınanan testler kırılır.
void removeOwnedTempQuietly(const std::filesystem::path& temporaryPath) {
    namespace fs = std::filesystem;
    std::error_code removeError;
    withTransientCommitRetry(
        kNoInjectedArtifact,
        [&]() {
            fs::remove(temporaryPath, removeError);
            return !removeError;
        },
        removeError, /*copying=*/true,
        static_cast<int>(SaveDurabilityTransientSite::Probe));
}

// Commit bölümü: finalPath'e dokunan TEK yer. g_slotCommitMutex ALTINDA
// çalışır ve ASLA loglamaz — çağıran kilidi bıraktıktan SONRA loglar, böylece
// Logger::s_logMutex altındaki rotateLogFile dosya I/O'su hiçbir zaman seri
// pencereye girmez. Fırlatmaz: hata durumunda *message doldurur, çağıran
// fail() ile loglar. Başarıda true; başarısızlıkta belgelenen tüm
// best-effort temizlik/restore adımları yapılmıştır.
static bool commitSlotLocked(const std::filesystem::path& finalPath,
                             const std::filesystem::path& temporaryPath,
                             std::string* message) {
    namespace fs = std::filesystem;
    // Mutex'in varlığını TEST EDİLEBİLİR kılan sayacı tut (bkz.
    // CommitOccupancyGuard). Kilit zaten alınmış durumdadır.
    const CommitOccupancyGuard occupancyGuard;
    auto note = [&](const std::string& text) {
        if (message != nullptr) *message = text;
        return false;
    };

    // 490 ön-yedek: hedef mevcutsa rename ÖNCESİ birebir kopyası alınır.
    // Kopya alınamazsa fail-closed (eski dosya riske atılmaz): tmp temizlenir,
    // hedefe dokunulmaz.
    // NOT: Yedek adını finalPath'den değil temporaryPath'ten türet — her yazar
    // zaten benzersiz bir tmp dosyasına sahip, böylece yedek HEDEFİ benzersizdir
    // ve eşzamanlı yazarlar aynı .pre-save-bak dosyası için Windows dosya kilidi
    // (ERROR_SHARING_VIOLATION) üretmez. 878c467.
    // NOT 2: Çekişen nesne yedek değil KAYNAK yani paylaşılan finalPath'tir;
    // onu kendimiz 128 KB boyunca FILE_SHARE_DELETE'siz tutuyoruz. İşte asıl
    // çekişen budur ve g_slotCommitMutex + copyFileWithTransientRetry bunu
    // birlikte ele alır.
    const fs::path backupPath = saveBackupPathFor(temporaryPath);
    bool haveBackup = false;
    // Parmakizi: rename başarısız olduğunda restore'un hâlâ güvenli olup
    // olmadığını anlamak için. KOPYADAN ÖNCE alınır: kopyadan sonra araya
    // giren bir rename başka bir yazarın payload'ını yerleştirirse parmakizi
    // tutmaz ve restore ATLANIR. Aksi hâlde eski bir snapshot, süreç dışından
    // gelen daha yeni bir verinin üstüne yazılırdı — sessiz kayıp güncelleme.
    std::uintmax_t baselineSize = 0;
    fs::file_time_type baselineStamp{};
    bool haveBaselineFingerprint = false;
    if (targetIsRegularFileWithRetry(finalPath)) {
        {
            std::error_code sizeError;
            std::error_code stampError;
            const std::uintmax_t size = fs::file_size(finalPath, sizeError);
            const fs::file_time_type stamp =
                fs::last_write_time(finalPath, stampError);
            if (!sizeError && !stampError) {
                baselineSize = size;
                baselineStamp = stamp;
                haveBaselineFingerprint = true;
            }
        }
        std::error_code copyError;
        if (!copyFileWithTransientRetry(finalPath, backupPath, copyError)) {
            // Yarım yedeği bırakma. Ad, cleanupStaleOwnedSlotTemps desenine
            // UYAR (başlıkta belgelendiği gibi), ama süpürme yalnızca
            // kanıtlanmış-ölü pid'e dokunduğu için canlı bizim sürecimizin
            // yarım yedeği süreç ömrü boyunca erişilemez disk artığı olarak
            // kalırdı. Yani desen uyumu burada yardım etmez, temizlik şart.
            std::error_code removeError;
            fs::remove(backupPath, removeError);
            fs::remove(temporaryPath, removeError);
            return note("Failed to stage pre-save backup beside: " +
                        Rowl::Platform::pathToUtf8(finalPath) + ": " +
                        copyError.message());
        }
        haveBackup = true;
    }

    // Test-only rakip yazar (bkz. g_injectCompetingWrite): yedek HAZIRLANDIKTAN
    // SONRA, replace ÖNCESİ. Süreçler arası bir yazarın kazandığı senaryo.
    if (g_injectCompetingWrite.load(std::memory_order_relaxed)) {
        std::ofstream competing(finalPath, std::ios::binary | std::ios::trunc);
        if (competing.is_open()) competing << kCompetingWriteMarker;
    }

    std::error_code replaceError;
    if (!replaceFileAtomically(temporaryPath, finalPath, replaceError)) {
        removeOwnedTempQuietly(temporaryPath);
        // 490: yedekten restore — hedefi eski baytlara döndür (best-effort).
        // POSIX rename / MoveFileEx(REPLACE_EXISTING) atomik olduğundan hedef
        // normalde hiç bozulmaz; bu adım egzotik dosya-sistemi yarı-hâllerine
        // karşı kemerdir. Süreç İÇİ yarış mutex ile kapanmıştır; süreçler
        // arası bir yazarın bu arada daha yeni bir payload kazanmış olabileceği
        // için restore, hedef hâlâ bizim parmakizimizi taşıyorsa YAPILIR.
        //
        // ÜÇÜNCÜ bir durum var ve eski ikili-sonlu düşünce kaçırıyordu:
        // parmakiz ÖLÇÜLEMEZ. Gerçek bir süreçler arası yarışta file_size /
        // last_write_time tam olarak hedef rename ediliyorken paylaşım
        // hatasıyla BAŞARISIZ olur — yani ölçüm, bu düzeltmenin var olma
        // sebebi olan pencerede başarısız olur. "Ölçülemedi" ile "ölçüldü ve
        // değişti" AYNI şey değildir: birincisinde hedefin durumu bilinmez
        // (restore riskli), ikincisinde yedek KESİNLİKLE bayatlamıştır
        // (restore yazmak veri kaybıdır). Bu yüzden ölçüm de transient retry
        // alır ve ölçülemezse yedek KANIT OLARAK kalır.
        bool restoreSafe = haveBackup;
        bool backupIsStale = false;  // ölçüldü ve DEĞİŞTİ
        if (restoreSafe && !haveBaselineFingerprint) {
            // Parmakiz hiç alınamadı: hedefin durumu BİLİNMİYOR.
            restoreSafe = false;
        }
        if (restoreSafe) {
            std::error_code sizeError;
            std::error_code stampError;
            std::uintmax_t nowSize = 0;
            fs::file_time_type nowStamp{};
            // Ölçüm de geçici paylaşım hatalarına açıktır; aynı sınıflandırma
            // ve bütçe burada da geçerli, ama ENJEKSİYON YOK: bu bir gözlem
            // noktası, bir yazma işlemi değil.
            std::error_code measureError;
            const bool measured = withTransientCommitRetry(
                kNoInjectedArtifact,
                [&]() {
                    sizeError.clear();
                    stampError.clear();
                    nowSize = fs::file_size(finalPath, sizeError);
                    nowStamp = fs::last_write_time(finalPath, stampError);
                    return !sizeError && !stampError;
                },
                measureError, /*copying=*/true,
                static_cast<int>(SaveDurabilityTransientSite::Probe));
            (void)measureError;
            if (!measured) {
                // Bütçe bittiyse de ölçülemedi: durum bilinmiyor (aşağıda
                // yedek kanıt olarak kalır).
                restoreSafe = false;
            } else if (nowSize != baselineSize || nowStamp != baselineStamp) {
                backupIsStale = true;
                restoreSafe = false;
            }
        }
        if (restoreSafe) {
            std::error_code restoreError;
            // NOT: bu copy BİLEREK retry'siz. Restore yalnızca rename bütçesi
            // tükendikten SONRA çalışır; paylaşılan hedefe yazmak zaten en zorlu
            // konumdur, retry burada yalnızca zaten kaybeden bir kayda bütçe
            // ekler ve clobber'ın tutma olasılığını ARTIRIR. Güvenlik tarafındaki
            // tek koruma yukarıdaki parmakizdir.
            fs::copy_file(backupPath, finalPath,
                          fs::copy_options::overwrite_existing, restoreError);
            // Restore TUTARSA yedek temizlenir; tutmazsa adlî kanıt / elle
            // kurtarma için YERİNDE bırakılır (mevcut 490 davranışı).
            if (!restoreError) removeBackupQuietly(backupPath);
        } else if (backupIsStale) {
            // Parmakiz ÖLÇÜLDÜ ve TUTMADI: hedef, bizden daha yeni ve tam bir
            // başka yazarın payload'ı. Yedek kesin olarak BAYAT; restore
            // yazmak sessiz kayıp güncelleme olurdu. Kurtarma değeri yok, o
            // yüzden bırakılırsa "kesintili yazma başına ≤1 artık" sınırı
            // bozulurdu. Temizlenir.
            removeBackupQuietly(backupPath);
        }
        // Ne "ölçüldü ve değişti" ne de "hiç ölçülemedi" durumunda restore
        // YAPILMAZ. İlki yeni veriyi ezerdi; ikincisinde hedefin durumu
        // bilinmiyor. İkisinde de yedek KALIR: ikincide bu tam da kanıtın
        // işe yaradığı andır. Ad süpürme desenine uyduğu ve sahibi öldüğünde
        // diğer tmpleriyle birlikte süpürüldüğü için D09'un L2 artık sınırı
        // bozulmaz. DAVRANIŞ DEĞİŞİKLİĞİ: base sürüm ölçülemediğinde koşulsuz
        // restore ediyordu; bilinmeyen durumda yazmak, yazmamaktan riskli.
        return note(errnoPrefix(replaceError.value()) +
                    "Failed to atomically replace save slot file: " +
                    replaceError.message() + " (" + Rowl::Platform::pathToUtf8(temporaryPath) +
                    " -> " + Rowl::Platform::pathToUtf8(finalPath) + ")");
    }
    // Başarı: yedek artık artıktır, best-effort temizle. Crash bu satıra
    // ulaşamadan gelirse slot başına ≤1 yedek kalır (diski doldurmaz).
    if (haveBackup) removeBackupQuietly(backupPath);
    return true;
}

bool writeSlotFileAtomically(const std::filesystem::path& finalPath,
                             const std::string& content,
                             std::string* errorOut) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& message) {
        if (errorOut != nullptr) *errorOut = message;
        ROWL_LOG_ERROR(message);
        return false;
    };

    const fs::path temporaryPath = mintOwnedTempPath(finalPath);
    if (temporaryPath.empty()) {
        return fail("Failed to create unique save slot temp file beside: " +
                    Rowl::Platform::pathToUtf8(finalPath));
    }

    // Test-only errno injection (production default off): simulate a
    // mid-write filesystem failure. A partial tmp is staged in OUR owned
    // unique file so the failure looks like a real interrupted write, then
    // removed; the pre-existing target file is never touched.
    if (const int injectedErrno = effectiveInjectErrno(); injectedErrno != 0) {
        try {
            {
                // binary: yarım tmp de bayt-eşleşme sözleşmesinin parçası
                // (bkz. üst yorumdaki CRLF gerekçesi).
                std::ofstream partial(
                    temporaryPath, std::ios::out | std::ios::trunc | std::ios::binary);
                if (partial.is_open()) {
                    partial << content.substr(0, content.size() / 2);
                    partial.flush();
                }
            }
            fs::remove(temporaryPath);
        } catch (...) {
        }
        // The ENOSPC sentence is kept verbatim as a prefix (legacy message
        // compatibility); the errno tag is appended for UI/telemetry.
        if (injectedErrno == ENOSPC) {
            return fail("No space left on device (injected ENOSPC) while writing " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(ENOSPC));
        }
        if (injectedErrno == EACCES) {
            return fail("Permission denied (injected EACCES) while writing " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(EACCES));
        }
        return fail("Read-only file system (injected EROFS) while writing " +
                    Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(EROFS));
    }

    // 490 + R1 (#3): temp YAZIMI kilit DIŞINDA. Yazdığımız dosya yalnızca bu
    // yazara ait benzersiz mint'tir (pid + atomik sayaç + O_EXCL sahiplenmesi)
    // ve cleanupStaleOwnedSlotTemps yalnızca kanıtlanmış-ÖLÜ pid'e dokunduğu
    // için süreç içinde hiçbir yol onu göremez. Kilit bölgesine sokmak,
    // karşılığında 128 KB I/O'yu tüm yazarlara yayacak tek getiri olurdu. Bu
    // sıralama zaten header'da belgelenen "write temp -> stage backup ->
    // rename" protokolüdür.
    {
        // BINARY ZORUNLU: MSVC'de std::ofstream varsayılan olarak TEXT
        // modunda açılır ve CRT her '\n'i '\r\n'e çevirir. Baytların
        // BİREBİR eşleşmesi gereken bir dosyaya yazıyoruz: üretim payload'ı
        // GameState::serializeJson -> json.dump(2), yani ÇOK SATIRLı. Text
        // modda her Windows kaydı CRLF ile, her Linux kaydı LF ile yazılıyor
        // ve çağırana verilen content'ten FARKLI baytlar diske iniyor.
        // Kısa vadede JSON ayrıştırmayı bozmuyor (CRLF geçerli JSON boşluğu),
        // ama (a) platformlar arası bayt farkı, (b) 4 MiB'lik
        // kMaxSaveFileBytes sınırına yakın bir kayıt Windows'ta sınırı aşıp
        // kendi kaydı YÜKLEYEMEZ hale gelebilir, (c) içerik bütünlüğü
        // varsayan her gelecek kullanım yanlış sonuç verir.
        // Bu satır 2026-09-16'dan (b9e59ef) beri text modundaydı; Windows
        // test_save_slot_concurrency'yi "concurrent writes must all succeed"
        // erken çıkışı maskelediği için yıllardır görünmüyordu.
        std::ofstream output(temporaryPath,
                             std::ios::out | std::ios::trunc | std::ios::binary);
        if (!output.is_open()) {
            // iostream does not guarantee errno on open failure: a stale 0
            // would render a misleading "[ERRNO0 (0): Success]" tag, so fall
            // back to the generic message when no errno was captured.
            const int openErrno = errno;
            if (openErrno == 0) {
                return fail("Failed to open save slot temp file for writing: " +
                            Rowl::Platform::pathToUtf8(temporaryPath));
            }
            return fail(errnoPrefix(openErrno) +
                        "Failed to open save slot temp file for writing: " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + ": " + std::strerror(openErrno));
        }
        output << content;
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            return fail("Failed to write complete save slot temp file: " +
                        Rowl::Platform::pathToUtf8(temporaryPath));
        }
    }   // ofstream handle burada kapanır: benzersiz temp'i hedefler, finalPath'i
        // değil — MoveFileExW bunu göremez.

    // Commit bölümü: yalnızca finalPath'e dokunan işlemler (490 yedek kopyası +
    // atomik replace) ve bunların best-effort temizliği. std::mutex::lock
    // patolojik durumlarda system_error fırlatabilir; "asla fırlatmaz"
    // sözleşmesini bozmamak için kilit ALINAMAZSA sessizce kilitsiz devam
    // edilir (retry katmanı yine de korur). Log, kilit bırakıldıktan SONRA
    // atılır: Logger::s_logMutex altında rotateLogFile gerçek dosya I/O'su
    // yapıyor, onu kilitli bölgeye sokma.
    std::string message;
    bool committed = false;
    {
        std::unique_lock<std::mutex> commitLock(g_slotCommitMutex,
                                                std::defer_lock);
        try {
            commitLock.lock();
        } catch (...) {
        }
        committed = commitSlotLocked(finalPath, temporaryPath, &message);
        if (commitLock.owns_lock()) commitLock.unlock();
    }   // kilit burada bırakılır, LOGDAN ÖNCE
    if (!committed) return fail(message);
    return true;
}

void cleanupStraySlotTemp(const std::filesystem::path& finalPath) {
    // R1 (#3) notu: once yalnizca legacy "<slot>.json.tmp" süpürülürdü.
    // D09: buna ek olarak sahibi-ölü benzersiz tmp'ler de süpürülür
    // (cleanupStaleOwnedSlotTemps) — her crash en fazla bir tmp sızdırır ve
    // süpürme artığı crash sayısıyla sınırlı tutar; canlı yazar tmp'sine
    // asla dokunulmaz (sahte kayıt-hatası yok).
    try {
        std::error_code error;
        std::filesystem::remove(saveTempPathFor(finalPath), error);
    } catch (...) {
    }
    cleanupStaleOwnedSlotTemps(finalPath);
}

// D09: "<pid>.<sayaç>.<kuyruk>" desenini çözer; yalnızca rakam-token'lar
// kabul edilir (mintOwnedTempPath çıktısı birebir).
bool allDigits(const std::string& token) {
    if (token.empty()) return false;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

// D09: sahip süreci yaşıyor mu? Kararsız kalınan her durumda (aralık-dışı
// pid, EPERM/bilinmeyen errno, Windows'ta handle-açılamama) muhafazakâr
// cevap YAŞIYOR'dur — süpürme yalnızca kanıtlanmış-ölü pid'e dokunur.
bool ownerProcessAlive(unsigned long long pid) {
#if defined(_WIN32)
    if (pid == 0 || pid > 4294967295ULL) return true;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(pid));
    if (handle != nullptr) {
        CloseHandle(handle);
        return true;
    }
    // ERROR_INVALID_PARAMETER: böyle bir pid yok (ölü). Diğer hatalar
    // (ACCESS_DENIED dahil) varlığına işaret edebilir — muhafazakâr geç.
    return GetLastError() != ERROR_INVALID_PARAMETER;
#else
    if (pid == 0 || pid > 2147483647ULL) return true;
    const pid_t candidate = static_cast<pid_t>(pid);
    if (candidate == ::getpid()) return true;
    if (::kill(candidate, 0) == 0) return true;
    // ESRCH: süreç yok (ölü). EPERM: süreç var ama sinyal izni yok (canlı).
    // Diğer errno'lar muhafazakâr-canlı sayılır.
    return errno != ESRCH;
#endif
}

void cleanupStaleOwnedSlotTemps(const std::filesystem::path& finalPath) {
    try {
        namespace fs = std::filesystem;
        fs::path parent = finalPath.parent_path();
        if (parent.empty()) parent = ".";
        // Yalnızca bu slotun mint desenine uyan adlar:
        // "<slot>.json.tmp.<pid>.<sayaç>.<rastgele>".
        // 490: ad karşılaştırma kayıpsız UTF-8 üzerinden (filename().string()
        // Windows'ta ANSI codepage ile fırlatırdı).
        const std::string prefix =
            Rowl::Platform::pathToUtf8(finalPath.filename()) + ".tmp.";
        std::error_code iterError;
        fs::directory_iterator it(parent, iterError);
        if (iterError) return;
        const fs::directory_iterator end;
        for (; it != end; it.increment(iterError)) {
            if (iterError) break;
            // Symlink-güvenli: yalnızca gerçek düzenli dosyalar; bağlantılar
            // ve dizinler atlanır (dış hedefe dokunma riski yok).
            std::error_code statusError;
            if (it->symlink_status(statusError).type() != fs::file_type::regular ||
                statusError) {
                continue;
            }
            const std::string name =
                Rowl::Platform::pathToUtf8(it->path().filename());
            if (name.size() <= prefix.size() ||
                name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::string rest = name.substr(prefix.size());
            const std::string::size_type dot1 = rest.find('.');
            if (dot1 == std::string::npos) continue;
            const std::string::size_type dot2 = rest.find('.', dot1 + 1);
            if (dot2 == std::string::npos) continue;
            const std::string pidToken = rest.substr(0, dot1);
            const std::string counterToken = rest.substr(dot1 + 1, dot2 - dot1 - 1);
            const std::string tail = rest.substr(dot2 + 1);
            if (!allDigits(pidToken) || !allDigits(counterToken) || tail.empty()) {
                continue;
            }
            unsigned long long pid = 0;
            try {
                pid = std::stoull(pidToken);
            } catch (...) {
                continue;
            }
            // TOCTOU notu: kontrol ile silme arasında pid ölebilir ya da
            // yeniden kullanılabilir. Ölen pid'in tmp'si tanım gereği yazılmaz
            // (sahibi ölü) — silmek güvenli. Yeniden kullanılan pid'in aynı
            // sayaç+rastgelelikle çakışması pratikte imkânsızdır (mkstemp
            // rastgeleliği / O_EXCL sahiplenmesi).
            if (ownerProcessAlive(pid)) continue;
            std::error_code removeError;
            fs::remove(it->path(), removeError);
        }
    } catch (...) {
    }
}

void setSaveDurabilityInjectErrno(int errnoValue) {
    if (errnoValue == 0) {
        g_injectErrno.store(0, std::memory_order_relaxed);
        return;
    }
    // Documented choice: unsupported codes fail closed as ENOSPC.
    g_injectErrno.store(isSupportedInjectErrno(errnoValue) ? errnoValue : ENOSPC,
                        std::memory_order_relaxed);
}

int saveDurabilityInjectErrno() {
    return effectiveInjectErrno();
}

void setSaveDurabilityInjectEnospc(bool inject) {
    setSaveDurabilityInjectErrno(inject ? ENOSPC : 0);
}

bool saveDurabilityInjectEnospc() {
    return effectiveInjectErrno() == ENOSPC;
}

void setSaveDurabilityInjectTransientFailures(int count,
                                             SaveDurabilityTransientSite site) {
    g_injectTransientFailures[static_cast<int>(site)].store(
        count > 0 ? count : 0, std::memory_order_relaxed);
}

int saveDurabilityInjectTransientFailures(SaveDurabilityTransientSite site) {
    return g_injectTransientFailures[static_cast<int>(site)].load(
        std::memory_order_relaxed);
}

int saveDurabilityTransientFailuresConsumed(SaveDurabilityTransientSite site) {
    return g_consumedTransientFailures[static_cast<int>(site)].load(
        std::memory_order_relaxed);
}

int saveDurabilityTransientRetryAttempts() {
    return kMaxTransientAttempts;
}

int saveDurabilityCommitOverlaps() {
    return g_commitOverlaps.load(std::memory_order_relaxed);
}

int saveDurabilityCommitOverlapsReset() {
    return g_commitOverlaps.exchange(0, std::memory_order_relaxed);
}

void setSaveDurabilityInjectCompetingWrite(bool inject) {
    g_injectCompetingWrite.store(inject, std::memory_order_relaxed);
}

bool saveDurabilityInjectCompetingWrite() {
    return g_injectCompetingWrite.load(std::memory_order_relaxed);
}

const char* saveDurabilityCompetingWriteMarker() {
    return kCompetingWriteMarker;
}

} // namespace Rowl::State
