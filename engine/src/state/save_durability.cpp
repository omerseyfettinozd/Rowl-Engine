#include "rowl/state/save_durability.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
#include <fcntl.h>
#include <signal.h>
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

bool replaceFileAtomically(const std::filesystem::path& temporaryPath,
                           const std::filesystem::path& finalPath,
                           std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(temporaryPath.c_str(), finalPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporaryPath, finalPath, error);
    return !error;
#endif
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
std::filesystem::path mintOwnedTempPath(const std::filesystem::path& finalPath) {
    const std::string stem = finalPath.string() + ".tmp." + currentProcessTag() +
                             "." + std::to_string(g_tempCounter.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
    // _sopen_s O_CREAT|O_EXCL: dosya varsa EEXIST ile başarısız olur; sayaç
    // her çağrıda arttığı için çakışma pratikte imkânsız, attempt döngüsü
    // sayaç-sarma/MMAP kalıntısına karşı kemerdir.
    for (int attempt = 0; attempt < 64; ++attempt) {
        const std::string candidate = stem + "." + std::to_string(attempt);
        int fd = -1;
        const int opened = _sopen_s(&fd, candidate.c_str(),
                                    _O_CREAT | _O_EXCL | _O_WRONLY, _SH_DENYRW,
                                    _S_IREAD | _S_IWRITE);
        if (opened == 0) {
            _close(fd);
            return std::filesystem::path(candidate);
        }
        if (errno != EEXIST) return std::filesystem::path{};
    }
    return std::filesystem::path{};
#else
    // mkstemp: O_CREAT|O_EXCL ile 0600 kipinde atomik üretir, XXXXXX'i
    // yerinde rastgele adla değiştirir.
    std::string pattern = stem + ".XXXXXX";
    const int fd = ::mkstemp(pattern.data());
    if (fd < 0) return std::filesystem::path{};
    ::close(fd);
    return std::filesystem::path(pattern);
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
                std::ofstream partial(temporaryPath, std::ios::out | std::ios::trunc);
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

    {
        std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
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
    }

    std::error_code replaceError;
    if (!replaceFileAtomically(temporaryPath, finalPath, replaceError)) {
        std::error_code removeError;
        fs::remove(temporaryPath, removeError);
        return fail(errnoPrefix(replaceError.value()) +
                    "Failed to atomically replace save slot file: " +
                    replaceError.message() + " (" + Rowl::Platform::pathToUtf8(temporaryPath) +
                    " -> " + Rowl::Platform::pathToUtf8(finalPath) + ")");
    }
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
        const std::string prefix = finalPath.filename().string() + ".tmp.";
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
            const std::string name = it->path().filename().string();
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

} // namespace Rowl::State
