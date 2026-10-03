#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <optional>
#include <system_error>

namespace Rowl::VFS {

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kMaxLooseAssetBytes = 128ULL * 1024 * 1024;

fs::path pathFromUtf8(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::optional<fs::path> resolveInsideRoot(const fs::path& canonicalRoot,
                                          const std::string& relativePath) {
    if (relativePath.empty() || relativePath.find('\0') != std::string::npos) return std::nullopt;

    std::string normalizedRel = relativePath;
    std::replace(normalizedRel.begin(), normalizedRel.end(), '\\', '/');

    // All graph, package and C-API paths are UTF-8. The narrow path
    // constructor uses the active Windows code page and loses valid names.
    fs::path requested = pathFromUtf8(normalizedRel);
    if (requested.is_absolute() || requested.has_root_name() || requested.has_root_directory()) {
        return std::nullopt;
    }

    std::error_code error;
    if (canonicalRoot.empty()) return std::nullopt;
    fs::path candidate = fs::weakly_canonical(canonicalRoot / requested, error);
    if (error) return std::nullopt;

    fs::path relative = candidate.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    const auto first = relative.begin();
    if (first != relative.end() && *first == "..") return std::nullopt;
    return candidate;
}

} // namespace

// A2a: quiet single-lookup loose-file read. nullopt = miss/unreadable (no
// diagnostics — multi-source probing must stay log-clean); an engaged,
// possibly empty vector is a REAL asset. Loud callers (read()) add their
// own single diagnostic on top.
std::optional<std::vector<uint8_t>> readLooseFileQuiet(const fs::path& fullPath) {
    std::error_code error;
    if (!fs::is_regular_file(fullPath, error) || error) return std::nullopt;
    const auto fileSize = fs::file_size(fullPath, error);
    if (error || fileSize > kMaxLooseAssetBytes) return std::nullopt;

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::vector<uint8_t> buffer(static_cast<std::size_t>(fileSize));
    if (fileSize > 0 &&
        !file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(fileSize))) {
        return std::nullopt;
    }
    return buffer;
}

LooseDirectorySource::LooseDirectorySource(std::string physicalPath)
    : m_physicalPath(std::move(physicalPath)) {
    std::error_code error;
    // A2a: canonicalize through UTF-8 — the narrow string ctor would
    // reinterpret a non-ASCII mount root in the ANSI codepage on Windows.
    m_canonicalRoot = fs::weakly_canonical(pathFromUtf8(m_physicalPath), error);
    if (error) {
        m_canonicalRoot.clear();
        ROWL_LOG_WARN("VFS could not canonicalize mount root: " + m_physicalPath);
    }
}

bool LooseDirectorySource::exists(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return false;
    std::error_code error;
    return fs::exists(*fullPath, error) && !error &&
           fs::is_regular_file(*fullPath, error) && !error;
}

std::vector<uint8_t> LooseDirectorySource::read(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) {
        ROWL_LOG_WARN("VFS rejected path outside mount root: " + path);
        return {};
    }
    auto data = readLooseFileQuiet(*fullPath);
    if (!data) {
        // A2a-fix5: .string() throws on non-ASCII paths through the ANSI
        // codepage on Windows — a failed read of a unicode asset must log,
        // never throw. pathToUtf8 is the proven u8-roundtrip.
        ROWL_LOG_WARN("VFS could not read loose asset: " + Rowl::Platform::pathToUtf8(*fullPath));
        return {};
    }
    return std::move(*data);
}

std::optional<std::vector<uint8_t>> LooseDirectorySource::tryRead(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return std::nullopt;
    return readLooseFileQuiet(*fullPath);
}

std::unique_ptr<std::istream> LooseDirectorySource::openStream(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return nullptr;
    std::error_code error;
    if (!fs::is_regular_file(*fullPath, error) || error) return nullptr;
    auto stream = std::make_unique<std::ifstream>(*fullPath, std::ios::binary);
    if (!stream->is_open()) return nullptr;
    return stream;
}

void VFSManager::initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    ROWL_LOG_INFO("Initializing Hybrid Virtual File System (VFS)...");

    // R1 (#14): bare init — SIFIR mount. Eski kod buradan CWD'ye göre
    // "mods/" + current_path/"Assets" mount'luyordu: aynı binary farklı
    // dizinden çalışınca farklı varlık çözüyordu (belirlenimsiz). Varlık
    // kökü artık yalnızca explicit remountProject(projectRoot) ile gelir
    // (proje yükleme yolu); proje yoksa VFS çıplak kalır, okumalar miss
    // döner. once-guard + kilit disiplini aynen durur.
    m_initialized = true;
    ROWL_LOG_INFO("VFS Initialization Complete (" + std::to_string(m_mountPoints.size()) + " mount points).");
}

void VFSManager::clearMountPoints() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_mountPoints.clear();
    invalidateShadowDiagnosesLocked();
    ROWL_LOG_INFO("VFS Mount Points Cleared.");
}

bool VFSManager::remountProject(const std::string& projectRoot) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    clearMountPoints();
    // #142: per-remount skipped-package diagnosis — reset before the mount
    // chain so stale counts never leak into the next project switch.
    m_skippedPackages = 0;
    m_firstSkippedPackage.clear();
    m_initialized = true;

    // Fail-open closure: an empty project root used to be accepted as a
    // successful remount (mounts silently cleared, caller told true). A blank
    // path is a caller bug, not a legitimate "clear" request — report it as
    // one so the C API story layer surfaces the diagnosis like any other
    // rejected root. Mounts are already cleared and skipped-counters reset
    // above; keep that state so a valid subsequent remount starts clean.
    if (projectRoot.empty()) {
        ROWL_LOG_WARN("VFS remountProject rejected empty project root");
        return false;
    }

    // A2a: UTF-8 contract — the narrow path ctor would lose a non-ASCII root
    // on Windows before any lookup even runs.
    fs::path root = pathFromUtf8(projectRoot);
    std::error_code fsError;
    if (!fs::exists(root, fsError) || fsError ||
        !fs::is_directory(root, fsError) || fsError) {
        // #122: report the miss to the caller instead of swallowing it — the
        // VFS stays bare (cleared above), and the C API story layer records
        // the diagnosis in the story/context error channels.
        ROWL_LOG_WARN("VFS cannot remount non-existent project root: " + projectRoot);
        return false;
    }

    // A2a-fix5: log the UTF-8 input, not root.string() — the narrow
    // conversion throws on a non-ASCII project root on Windows.
    ROWL_LOG_INFO("Remounting VFS for isolated project root: " + projectRoot);

    // Mount mods first so a release can override package content without a
    // second loose Assets tree.  The package remains the only base source.
    // A2a-fix1: clear before every probe chain — a reused error_code must
    // never carry a previous step's state into the next decision.
    fs::path modsPath = root / "mods";
    fsError.clear();
    if (fs::exists(modsPath, fsError) && !fsError &&
        fs::is_directory(modsPath, fsError) && !fsError) {
        const std::string modsUtf8 = Rowl::Platform::pathToUtf8(modsPath);
        mountDirectory("", modsUtf8);
        mountDirectory("mods", modsUtf8);
    }

    // Only expose declared runtime content. Mounting the project root at the
    // empty prefix would make project metadata and arbitrary source files
    // readable through an asset lookup after a project switch.
    // 1. Mount project Assets folder.
    fs::path assetsPath = root / "Assets";
    fsError.clear();
    if (fs::exists(assetsPath, fsError) && !fsError &&
        fs::is_directory(assetsPath, fsError) && !fsError) {
        const std::string assetsUtf8 = Rowl::Platform::pathToUtf8(assetsPath);
        mountDirectory("", assetsUtf8);
        mountDirectory("Assets", assetsUtf8);

        // 2. Mount images
        fs::path imgPath = assetsPath / "images";
        fsError.clear();
        if (fs::exists(imgPath, fsError) && !fsError &&
            fs::is_directory(imgPath, fsError) && !fsError) {
            const std::string imgUtf8 = Rowl::Platform::pathToUtf8(imgPath);
            mountDirectory("", imgUtf8);
            mountDirectory("images", imgUtf8);
        }

        // 3. Mount packages
        mountPackagesUnder(assetsPath / "packages");
    }

    ROWL_LOG_INFO("VFS Remount Complete for project '" + projectRoot + "' (" +
                  std::to_string(m_mountPoints.size()) + " mount points).");
    return true;
}

void VFSManager::mountPackagesUnder(const fs::path& pkgPath) {
    // A2a-fix3: Windows CI'da probe "present" demesine rağmen tarama sessiz
    // ölüyordu — ne mount, ne "no archives", ne WARN, üstelik "Remount
    // Complete" bile basılmadan (invokeNoexcept çağrıcıyı da yutuyordu).
    // Kod incelemesi tek sessiz çıkışı gösteriyor: exists "present" + iterator
    // no_such_file (8.3-kısa-yollu RUNNER~1 formunda exists/iterator ayrışması)
    // ya da yutulan bir istisna. İkisine de çare: önce canonicalize (aynı
    // kökte loose mount'ların KANITLANMIŞ çalışan primitive'i — exists+iterator
    // artık özdeş formda koşar), tarama try/catch kalkanında, skip dalı INFO.
    std::error_code canonError;
    const fs::path canonPath = fs::weakly_canonical(pkgPath, canonError);
    if (canonError || canonPath.empty()) {
        ROWL_LOG_WARN("VFS package scan skipped: cannot resolve '" +
                      Rowl::Platform::pathToUtf8(pkgPath) + "': " + canonError.message());
        return;
    }
    const std::string pkgUtf8 = Rowl::Platform::pathToUtf8(canonPath);
    // A2a-fix2: exists() Windows CI'da MEVCUT bir dizin için FALSE döndü;
    // exists artık sadece tanı bilgisidir — kararı bağımsız syscall olan
    // iterator verir. Yokluk normaldir (loose-only projeler).
    std::error_code probeError;
    const bool present = fs::exists(canonPath, probeError) && !probeError;
    const std::string probeDetail =
        present ? "present"
                : "negative (" + (probeError ? probeError.message() : "absent") +
                      ") — scan attempted anyway";
    ROWL_LOG_INFO("VFS packages probe for '" + pkgUtf8 + "': " + probeDetail);

    std::error_code iterError;
    bool mountedAny = false;
    try {
        // A2a-fix4: canonical tarama, girdi-seviyesinde izole edilir. Windows
        // CI'da STL iterasyonu ERROR_NO_UNICODE_TRANSLATION fırlattı
        // (wide→ANSI-codepage çevirisi); tek patlayan girdi BÜTÜN taramayı
        // öldürmemeli — game.rowlpkg yine mount'lanmalı. increment ec'li
        // (throw etmez); deref+işleme iç try'da (patlarsa WARN+continue).
        fs::directory_iterator it(canonPath, iterError);
        const fs::directory_iterator end;
        while (!iterError && it != end) {
            try {
                const fs::directory_entry entry = *it;
                std::error_code entryError;
                const bool regular = entry.is_regular_file(entryError);
                if (entryError) {
                    // A2a-fix1: an iterator/entry failure used to end the scan
                    // silently (Windows CI mounted a verified game.rowlpkg
                    // never, with no log line at all). Loud now — a skipped
                    // scan must explain itself.
                    ROWL_LOG_WARN("VFS package scan skipped unreadable entry in '" + pkgUtf8 +
                                  "': " + entryError.message());
                } else if (regular && entry.path().extension().wstring() == L".rowlpkg") {
                    // Wide-karşılaştırma: hot-loop'ta narrow-literal/path
                    // dönüşümü sıfır. pathToUtf8 temiz u8-roundtrip'tir
                    // (kanıtlı) ve o da iç try'ın kalkanındadır.
                    mountPackage("", Rowl::Platform::pathToUtf8(entry.path()));
                    mountedAny = true;
                }
            } catch (const std::exception& ex) {
                ROWL_LOG_WARN("VFS package scan skipped throwing entry in '" + pkgUtf8 +
                              "': " + ex.what());
            } catch (...) {
                ROWL_LOG_WARN("VFS package scan skipped throwing entry in '" + pkgUtf8 + "'");
            }
            it.increment(iterError);
        }
        if (iterError) {
            ROWL_LOG_WARN("VFS package scan failed in '" + pkgUtf8 + "': " + iterError.message());
            return;
        }
    } catch (const std::exception& ex) {
        // A2a-fix3: çağrıcı invokeNoexcept altında — yutulan istisna hem
        // mount'u hem teşhisi öldürüyordu. Artık her çıkış konuşur.
        ROWL_LOG_WARN("VFS package scan threw in '" + pkgUtf8 + "': " + ex.what());
        return;
    } catch (...) {
        ROWL_LOG_WARN("VFS package scan threw in '" + pkgUtf8 + "'");
        return;
    }
    if (iterError) {
        // A2a-fix3: eskiden no_such_file burada SESSİZ dönüyordu — Windows
        // CI'daki kör nokta buydu. Skip normaldir, ama sessiz değildir.
        ROWL_LOG_INFO("VFS package scan skipped '" + pkgUtf8 + "': " + iterError.message());
        return;
    }
    if (!mountedAny) {
        ROWL_LOG_INFO("VFS package scan found no archives under '" + pkgUtf8 + "'");
    }
}

void VFSManager::mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto source = std::make_shared<LooseDirectorySource>(physicalPath);
    // A2a: refuse dead mounts loudly. Previously an unreachable root was
    // mounted anyway and every lookup silently missed.
    if (!source->isValid()) {
        ROWL_LOG_ERROR("VFS refused to mount unreachable directory: '" + physicalPath +
                       "' under virtual prefix '" + virtualPrefix + "'");
        return;
    }
    m_mountPoints.emplace_back(virtualPrefix, source);
    // P2-6: topoloji değişti -> üretilmiş gölgeleme teşhisleri geçersiz.
    invalidateShadowDiagnosesLocked();
    ROWL_LOG_INFO("VFS Mounted directory: '" + physicalPath + "' under virtual prefix '" + virtualPrefix + "'");
}

void VFSManager::mountPackage(const std::string& virtualPrefix, const std::string& pkgPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto source = std::make_shared<RowlPkgDataSource>(pkgPath);
    if (source->isValid()) {
        m_mountPoints.emplace_back(virtualPrefix, source);
        // P2-6: topoloji değişti -> üretilmiş gölgeleme teşhisleri geçersiz.
        invalidateShadowDiagnosesLocked();
        ROWL_LOG_INFO("VFS Mounted package: '" + pkgPath + "' under virtual prefix '" + virtualPrefix + "'");
    } else {
        // #142: WARN-only used to be the whole story — a corrupt package left
        // the mounts quietly short with no host-visible signal. Count the skip
        // (and keep the first path) so the C API layer can surface it on the
        // context channel after remountProject.
        ++m_skippedPackages;
        if (m_firstSkippedPackage.empty()) m_firstSkippedPackage = pkgPath;
        ROWL_LOG_WARN("VFS Failed to mount package: '" + pkgPath + "'");
    }
}

// ============================================================================
// P2-6 — MOUNT ÖNCELİK KURALI (TEK KAYNAK)
// ============================================================================
// ESKİ DAVRANIŞ: iki GEÇİŞ. GEÇİŞ 1 tüm mount'larda prefix-strip dener ve
// ilen bulduğu anda DÖNER; GEÇİŞ 2 ancak GEÇİŞ 1 herkesi kaçırdıysa çalışır.
// Bunun sonucu: mount LİSTESİ DEĞİL, prefix ÖZGÜLLÜĞÜ öncelik belirler.
// Yani 6. sıradaki 'images' alias'ı, 0. sıradaki mods mount'unu GÖLGELER.
// mount'un üstündeki "correct priority: mods > data > packages" yorumu bu
// kodun yaptığının TAM TERSİYDİ (ayrıca "data" prefix'i hiç üretilmiyor).
//
// KURAL (artık tek yerde, üç çağrı yerinde de aynı):
//   1) Mount LİSTESİ öncelik sırasıdır. remountProject() (vfs.cpp:184-217)
//      mods'u, sonra Assets'i, en son paketleri mount EDER ve yorumu bunu
//      açıkça söyler: "Mount mods first so a release can override package
//      content". Sıra, override mekanizmasının kendisidir.
//   2) Varlık yolu, liste BAŞINDAN SONA taranır. Her mount için önce
//      prefix-strip'li biçim, sonra doğrudan biçim denenir.
//   3) İLK bulan mount kazanır.
//   4) Prefix bir ÖNCELİK iddiası DEĞİLDİR — sadece bir ALIAS'tır.
//      'images' -> Assets/images, 'mods' -> mods, 'Assets' -> Assets yazan
//      alias'lar aynı fiziksel kökü gösterir; "en uzun prefix kazanır" kuralı
//      burada ANLAMSIZDIR (uzun bir alias daha yüksek bir katman değildir).
//      Bu yüzden kural 1 her zaman prefix özgüllüğünü yener.
//
// FAIL-OPEN / FAIL-CLOSED:
//   Mount katmanı FAIL-OPEN'dır (ilk bulan kazanır, çakışma reddedilmez).
//   Gerekçe: burada sorulan soru "bu baytlar güvenilir mi?" değil, "hangi
//   içeriği gösterelim?" — bir içerik SEÇİMİ kararıdır. Fail-closed olsaydı
//   mod + base çakışan her varlık okunamaz olurdu, yani modlama sistemi
//   tamamen çalışmazdı. Paket/manifest katmanı fail-closed KALIR (bayt
//   bütünlüğü = yetki kararı); o katman bu dosyanın kapsamı dışındadır.
//   AMA fail-open sessiz olmak zorunda değildir: aşağıda her gölgeleme WARN
//   üretir. Sessiz fallback, sessiz hatadan kötüdür.
//
// MALİYET AYRIMI — öncelik DÜZELTMESİ ile teşhîs FARKLI işler:
//   resolveInMountOrder() (çözümleme) UYGUNLAMA düzeltmesidir: ucuz ve doğru,
//   ek yükü yoktur. Eğer eski iki geçişli kodu bir kez daha okursan aynı işi
//   yapar — sadece kararı yanlış verir. KORUNUR.
//   reportShadowing() ise bir TEŞHİS üretir ve çözümlemeden SONRA, her
//   başarılı okumada kalan mount'ları tarar. 510f8c7'de bu tarama HER okumada
//   koşuyordu; vfs_io benchmark'ı tek bir yolu 5000 kez okuduğu için ölçüm
//   26.7us -> 128.3us (+%381) ile kırıldı (CI: %35 eşiği, %282 ölçüm).
//   Bu bir DOĞRULUK maliyeti değil, tekrarlanan bir TANİ maliyetidir: aynı
//   soruya verilen aynı cevap. Bu yüzden maliyet teşhisin ÜZERİNE
//   yönlendirilir (claimShadowDiagnosis) ve çözümlemeye dokunulmaz.
//
//   Varsayılan-KAPALI (opt-in) bir ortam değişkeni KULLANILMADI. Gerekçe:
//   bu projede bugün 4 "sessizce yeşil veren ölü kapı" bulundu ve bu
//   proje tam da sessiz düşmeyi kapatmak için yazıldı — gölgeleme teşhisini
//   varsayılan olarak susturmak, düzeltilen hatanın kendisini geri getirir.
//   Teşhis zaten bir KAPI değil, bir TANI: doğru/yanlış ölçmez, yalnızca
//   bilgi verir. Varsayılanı açık tutmak bedava olduğu için (aşağıda) ve
//   susturmanın bir şey kazandırmadığı için opt-in savunulabilir bulunmadı.
//   Teşhisin maliyeti indirgendiği için kapatma düğmesine de gerek kalmadı.
namespace {

using MountList = std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>>;

/// Kazanan mount'un hangi biçimle eşleştiği (yalnız teşhis/log için).
struct MountHit {
    size_t index = 0;
    bool prefixStripped = false;
};

/// 'path', mount'un 'prefix' ALIAS'ının altında mı? Kural 4: bu yalnızca
/// "hangi biçimi deneyeceğiz" sorusunu yanıtlar, öncelik kararı değildir.
bool pathUnderPrefix(const std::string& path, const std::string& prefix) {
    return !prefix.empty() && path.size() > prefix.size() &&
           path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/';
}

/// P2-6: ÖNCELİK KURALININ TEK UYGULAMASI. exists()/readBytes/openStream
/// üçü de BURAYI çağırır — eskiden aynı karar kopyala-yapıştırla üç yere
/// yayılmış ve üçü de birlikte tersinmişti; tek noktaya toplamak aynı
/// kararın yeniden ayrışmasını yapısal olarak imkânsız kılar.
///
/// `probe(source, path)` true dönerse o mount'ta varlık bulundu demektir.
/// Kural 2/3: liste sırası korunur, ilk bulan kazanır.
template <typename ProbeFn>
std::optional<MountHit> resolveInMountOrder(const MountList& mounts,
                                            const std::string& cleanPath,
                                            ProbeFn&& probe) {
    for (size_t i = 0; i < mounts.size(); ++i) {
        const auto& [prefix, source] = mounts[i];
        // Bu mount'un alias'ı yolu kapsıyorsa önce soyulmuş biçim denenir:
        // prefix eşleştiği için bu mount'un bu varlığı taşıma niyeti nettir.
        if (pathUnderPrefix(cleanPath, prefix)) {
            if (probe(source, cleanPath.substr(prefix.size() + 1))) {
                return MountHit{i, /*prefixStripped=*/true};
            }
        }
        if (probe(source, cleanPath)) {
            return MountHit{i, /*prefixStripped=*/false};
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// KİMLİK ANAHTARI — "aynı dosya" sorusunun tek doğru cevabı
// ---------------------------------------------------------------------------
// resolvedIdentity() karşılaştırması YAZI karşılaştırması değil, FİZİKSEL
// kimlik karşılaştırması olmak zorunda. lexically_normal() yalnızca sözdizimi
// normalleştirir (".", "..", fazla ayırıcı) ve şu üç ayrımı YAPAMAZ:
//
//   1) Windows 8.3 KISA ADI. "C:\Users\RUNNER~1\AppData\Local\Temp\x" ile
//      "C:\Users\runneradmin\AppData\Local\Temp\x" AYNI dizindir, ama iki
//      farklı metindir. Windows CI'da mount yolu kısa adla, gölgeleme raporu
//      uzun adla kurulduğu için resolvedIdentity() aynı dosyayı "farklı fiziksel
//      kopya" sanıyor ve SAHTE gölgeleme basıyordu.
//   2) AYIRICI. mount yolu native ("\"), log satırı generic ("/"). Aynı yol,
//      iki metin. Windows'ta bu tek başına yeterli bir sahte uyarı sebebi.
//   3) SYMLINK. Linux'ta iki farklı yol aynı dosyaya gösterebilir.
//
// Çözüm: canonical() — dosya MEVCUTSA nihai dosya tanıtıcısından (POSIX:
// readlink zinciri; Windows: GetFinalPathNameByHandle) çözümlendiği için 8.3
// kısa adı da uzun adı da, sembolik bağı da tek metne indirger. Dosya yoksa
// (taramada "burada yok" yanıtı veren mount'lar için) weakly_canonical'a
// düşülür; o da en azından göreli bileşenleri ve ayırıcıları normalleştirir.
//
// generic_string() ayırıcıyı her platformda '/' yapar. Windows'ta NTFS
// aramaları büyük/küçük harfe DUYARSIZ olduğu için anahtar ASCII harf
// katlanır; aksi halde "Assets" ile "assets" iki dizin sanılırdı.
// (Kalan sınır: ASCII dışı harf katlama. Türkçe I veya Alman ss gibi
// kenar durumlar tam katlanmaz — NTFS'in kendi karşılaştırmasıyla uyuşan
// bir katlama std::filesystem'te yoktur.)

// TEST KANAL AÇMA ARACI (BULGU A). Üretimde KAPALI ve bir "optimizasyon"
// değil: physicalPathKey()'in DAR dönüşüm adımı MSVC'de ASCII olmayan
// köklerde (C:\Kullanıcılar, C:\Users\<decomposed>) std::system_error
// FIRLATIR. Bu satır olmadan o yol Linux testlerinde hiç görünmez, çünkü
// libstdc++ dar dönüşümde baytları olduğu gibi geçirir. Kanal, Windows'taki
// o davranışı Linux'ta BİREBİR taklit eder; test_vfs_security.cpp bunu
// açıp "teşhis yutulmuyor" sözleşmesini ölçer. save_durability.cpp'teki
// ENOSPC kanalıyla aynı desen ve aynı gerekçe (bu projede sessiz düşme
// birden fazla kez gerçek hataya dönüştü).
std::atomic<bool> g_injectPathKeyThrow{false};
// physicalPathKey() üretilemediğinde kullanılan YEDEK kimlik sayacı. Ham
// yazım da çevrilemezse (olası ama savunma derinliği) anahtar yine de
// üretilir; sıfırdan farklı olsun ki iki FARKLI dosya asla aynı sanılmasın.
std::atomic<uint64_t> g_uncanonicalKeySeq{0};

// Kanonik anahtarı `key`e yazar ve HİÇBİR KOŞULDA FIRLATMAZ (P2-6 BULGU A).
// Kanonikleştirilemezse `key` bir YEDEK kimliktir: okunabilir ama açıkça
// "kanonikleştirilemedi" işaretlidir ve kanonik bir anahtarla ASLA eşit
// olamaz (işaretçi baytı). Durum bir DÖNÜŞ DEĞERİYLE değil, AÇIK BİR TANI
// ile bildirilir — çağıranın sessizce geçebileceği bir durum bırakılmaz.
//
// Yön bilinçlidir ve TEK YÖNLÜDÜR: kanonikleştirilemeyen bir yol için
// iki farklı fiziksel dosya ASLA aynı anahtarı alamaz, dolayısıyla gerçek
// bir gölgeleme hiçbir koşulda gizlenemez. Bunun bedeli, aynı dosyanın iki
// alias yazımının "farklı fiziksel kopya" sanılmasıdır — yani bir SAHTE
// uyarı. Bu projede kabul edilen yön "kayıp uyarı değil, sahte uyarı"dır
// ve sessizlik bugün defalarca gerçek hataya dönüştü.
void physicalPathKey(const fs::path& path, std::string& key) {
    if (path.empty()) {
        key.clear();
        return;
    }
    std::error_code error;
    // Sıralama: canonical (nihai kimlik) -> weakly_canonical (yoksa bile
    // sözdizimsel) -> lexically_normal (son çare, hata durumunda).
    fs::path resolved = fs::canonical(path, error);
    if (error || resolved.empty()) {
        error.clear();
        resolved = fs::weakly_canonical(path, error);
        if (error || resolved.empty()) resolved = path.lexically_normal();
    }

    std::string converted;
    bool canonical = false;
    // Kanal burada: emulated dar dönüşüm, GERÇEK kodun fırlattığı yerde.
    if (g_injectPathKeyThrow.load(std::memory_order_relaxed)) {
        // Windows'ta MSVC'nin yaptığı gibi: dar dönüşüm temsil edilemeyen
        // karakter için system_error fırlatır, çağıran yutar.
        try {
            throw std::system_error(std::make_error_code(std::errc::illegal_byte_sequence),
                                    "rowl injected narrow-conversion failure");
        } catch (const std::exception& e) {
            ROWL_LOG_WARN("VFS shadow diagnosis: path could not be canonicalised: "
                          + std::string(e.what()) +
                          ". Using a non-canonical fallback identity for this path; "
                          "its shadow check is UNVERIFIED and may report a FALSE "
                          "shadow. It cannot hide a real one.");
            key = "\x01uncanonical\x01" +
                  std::to_string(g_uncanonicalKeySeq.fetch_add(1, std::memory_order_relaxed));
            return;
        }
    }
    // generic_string() DEĞİL, pathToUtf8(): dosyanın kendi kuralı (satır 102)
    // ".string() ASCII olmayan yollarda Windows'ta ANSI kod sayfası üzerinden
    // FIRLATIR, pathToUtf8 kanıtlanmış u8-roundtrip'tir". generic_string() de
    // aynı dar dönüşümü yapar, yani aynı istisnayı fırlatır.
    try {
        converted = Rowl::Platform::pathToUtf8(resolved);
        canonical = true;
    } catch (const std::exception& e) {
        ROWL_LOG_WARN("VFS shadow diagnosis: path could not be canonicalised: "
                      + std::string(e.what()) +
                      ". Using a non-canonical fallback identity for this path; its "
                      "shadow check is UNVERIFIED and may report a FALSE shadow. It "
                      "cannot hide a real one.");
    }
    if (!canonical) {
        // Yedek kimlik: ham yazım. Aynı dosyanın iki yazımı farklı sayılır
        // (sahte uyarı — kabul edilen yön), iki farklı dosya ASLA aynı
        // sayılmaz (asla kayıp uyarı yok).
        key = "\x01uncanonical\x01";
        try {
            key += Rowl::Platform::pathToUtf8(path);
        } catch (const std::exception&) {
            key += std::to_string(
                g_uncanonicalKeySeq.fetch_add(1, std::memory_order_relaxed));
        }
        return;
    }
    key = std::move(converted);
#ifdef _WIN32
    // canonical() bazı durumlarda Win32 uzantı ön ekiyle döner
    // (\\?\C:\... veya \\?\UNC\server\share\...). Karşılaştırma için şeritle;
    // iki taraf da aynı fonksiyondan geçse de log okunur kalsın.
    if (key.rfind("\\\\?\\UNC\\", 0) == 0) {
        key = "\\\\" + key.substr(8);
    } else if (key.rfind("\\\\?\\", 0) == 0) {
        key = key.substr(4);
    }
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
}

/// RowlPkgDataSource fiziksel paket yolunu YALNIZCA getSourceName() içinde
/// yayınlar (rowlpkg_reader.hpp:55: "RowlPkgDataSource [<path>]"); bir
/// getPhysicalPath() erişimcisi yoktur. Kimlik üretmek için parçalanır ve
/// güvenlik için GERİ SARILIR: iç parça '[' veya ']' içeriyorsa ya da geri
/// sarma metni birebir getSourceName() vermiyorsa parse BAŞARISIZ sayılır ve
/// ham ada düşülür. Böylece köşeli parantez içeren bir yol (NTFS'de yasak
/// değil) kimliği bozamaz, yalnızca kanonikleştirmeden düşer.
std::optional<std::string> physicalPathInSourceName(const std::string& sourceName) {
    const auto open = sourceName.find('[');
    const auto close = sourceName.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close <= open + 1) {
        return std::nullopt;
    }
    const std::string inner = sourceName.substr(open + 1, close - open - 1);
    if (inner.find('[') != std::string::npos || inner.find(']') != std::string::npos) {
        return std::nullopt;
    }
    if (sourceName.substr(0, open + 1) + inner + sourceName.substr(close) != sourceName) {
        return std::nullopt;
    }
    return inner;
}

/// Bir mount'un FİZİKSEL kökü. remountProject aynı kökü bilerek birden çok
/// ALIAS altında mount eder ('', 'mods', 'Assets', 'images'), yani mount listesi
/// kök başına TEKRARLAR. Aynı kökten iki mount aynı varlık için aynı yanıtı
/// verir — bu yüzden gölgeleme taraması kök başına TEKİL tutulur (aşağıda).
/// Hem doğru hem de yarı maliyetli.
std::string rootIdentity(const std::shared_ptr<IDataSource>& source) {
    std::string key;
    if (const auto* loose = dynamic_cast<const LooseDirectorySource*>(source.get())) {
        physicalPathKey(pathFromUtf8(loose->getPhysicalPath()), key);
        return key;
    }
    // Paket kökü = paket dosyasının kendisi. getSourceName() ham yolu
    // yayınladığı için önce kanonik anahtara çevrilir; aksi halde aynı paket
    // 8.3 ve uzun adla mount edildiğinde İKİ FARKLI paket sanılır.
    if (const auto inner = physicalPathInSourceName(source->getSourceName())) {
        physicalPathKey(pathFromUtf8(*inner), key);
        return key;
    }
    return source->getSourceName();
}

/// Bir mount'un bu varlığı FİZİKSEL olarak nereden okuyacağını kimlik olarak
/// kurar. İki mount aynı kimliği üretiyorsa onlar ALIAS'tır — aynı fiziksel
/// dosyanın birden çok yazımıdır, çakışma DEĞİLDİR ve gölgeleme sayılmaz.
///
/// Bu ayrım şart: remountProject aynı dizini KASITLI olarak birden çok kez
/// mount eder ('', 'Assets', 'images'...). Alias'ları gölgeleme saymak, her
/// varlık okumasında bir uyarı basardı — yani "sessiz hatayı gürültüye"
/// çevirmek. Gerçek gölgeleme, FARKLI bir fiziksel kopyanın erişilemez
/// kalmasıdır: aynı ada sahip iki içerik birbirinden farklı olabilir ve
/// çağıran yalnızca birini görecek.
std::string resolvedIdentity(const std::shared_ptr<IDataSource>& source,
                             const std::string& subPath) {
    if (const auto* loose = dynamic_cast<const LooseDirectorySource*>(source.get())) {
        // Kök zaten kanonik; alt yolu da kanonikleştir ki aynı dosyaya
        // giden sembolik bağ ve ../ kalıntıları da eşleşsin.
        std::string key;
        physicalPathKey(pathFromUtf8(loose->getPhysicalPath()) / pathFromUtf8(subPath), key);
        return key;
    }
    // Paket ayrı bir fiziksel kap: aynı ada sahip bir kayıt, loose bir dosyadan
    // farklı içerik taşıyabilir — her zaman ayrı kimlik sayılır. Kötü
    // parse hâlinde ham ada düşülür (aynı paket iki kez farklı görünebilir;
    // bu, kayıp bir uyarıdan iyidir, sahte bir uyarı değil).
    if (const auto inner = physicalPathInSourceName(source->getSourceName())) {
        std::string key;
        physicalPathKey(pathFromUtf8(*inner), key);
        return key + "::" + subPath;
    }
    return source->getSourceName() + "::" + subPath;
}

/// P2-6: SESSİZ DÜŞMEYİ KALDIR. Kazanan mount bulunduktan SONRA kalan
/// mount'lar YALNIZCA exists() ile yoklanır (içerik okunmaz, maliyet bir
/// istat/indeks sorgusudur). Aynı varlık FARKLI bir fiziksel kopyada da
/// varsa, çağıran yalnızca ilkini görecek ve diğeri ERİŞİLEMEZ olacak —
/// bu, bir modun sessizce gölgelenmesidir ve kullanıcı nedenini göremezdi.
/// Artık söylüyor: kazananı, gölgelenenleri ve nedenini.
void reportShadowing(const std::string& cleanPath, const MountList& mounts,
                     const MountHit& hit) {
    const auto& [winPrefix, winSource] = mounts[hit.index];
    const std::string winSub = hit.prefixStripped
                                   ? cleanPath.substr(winPrefix.size() + 1)
                                   : cleanPath;
    const std::string winnerId = resolvedIdentity(winSource, winSub);

    std::vector<std::string> shadowed;
    // Bir mount'un bu varlığı sunabileceği iki biçim: doğrudan yol ve (alias
    // kapsıyorsa) prefix soyulmuş yol. İkisi de aynı fiziksel dosyaya varabilir;
    // consume() kimlik eşitliğiyle tekilleştirmeyi yapar.
    const auto probeBothForms = [&](const std::shared_ptr<IDataSource>& source,
                                    const std::string& prefix,
                                    const auto& consume) {
        if (source->exists(cleanPath)) consume(cleanPath);
        if (pathUnderPrefix(cleanPath, prefix)) {
            const std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (stripped != cleanPath && source->exists(stripped)) consume(stripped);
        }
    };

    // Kök başına tekil: mount listesi aynı kökü birden çok kez içerir ve aynı
    // kök aynı soruya aynı yanıtı verir. Tekrarlı yoklama hem pahalıdır hem de
    // hiçbir yeni bilgi üretmez.
    std::vector<std::string> probedRoots;

    for (size_t j = hit.index + 1; j < mounts.size(); ++j) {
        const auto& [prefix, source] = mounts[j];
        const std::string root = rootIdentity(source);
        if (std::find(probedRoots.begin(), probedRoots.end(), root) !=
            probedRoots.end()) {
            continue;
        }
        probedRoots.push_back(root);
        // Kalan mount'un hem doğrudan hem soyulmuş biçimi yoklanır: hangi
        // biçimde olursa olsun çağıran o içeriği göremeyecek.
        probeBothForms(source, prefix, [&](const std::string& sub) {
            const std::string id = resolvedIdentity(source, sub);
            // Alias aynı fiziksel dosyayı gösteriyor → çakışma değil.
            if (id == winnerId) return;
            // Aynı kopya birden çok alias mount'tan görünebilir; bir kez yaz.
            if (std::find(shadowed.begin(), shadowed.end(), id) == shadowed.end()) {
                shadowed.push_back(id);
            }
        });
    }
    if (shadowed.empty()) return;

    std::string list;
    for (const auto& id : shadowed) {
        if (!list.empty()) list += ", ";
        list += id;
    }
    ROWL_LOG_WARN("VFS mount shadow: '" + cleanPath + "' resolved from mount #" +
                  std::to_string(hit.index) + " (" + winSource->getSourceName() +
                  "), but a DIFFERENT physical copy of the same asset is unreachable in: " +
                  list + ". Earlier mount wins by priority. Remove the duplicate copy, "
                  "or mount the layer that should win first.");
}

// P2-6 maliyet tavanı: bir VFSManager ömründe tanı üretilen yol sayısı.
// Gerçek projeler yüzlerce varlıkla sınırlıdır; bu tavan yalnızca "sonsuz
// benzersiz yol" gördüğümüzde (sürekli üretilen bir yol uzayı) belleği
// sınırlar. Aşılırsa kayıt düşürülür — en kötü hal, teşhisin tekrar
// üretilmesidir, sessizlik DEĞİLDİR.
constexpr size_t kMaxDiagnosedShadowPaths = 4096;

} // namespace

// P2-6: bir yolun gölgeleme teşhisinin ÜRETİLİP ÜRETİLMEDİĞİNİ söyler.
// true dönerse bu çağıran teşhisi üretir (tarama + WARN), false ise üretilmiş
// demektir ve o yol için bir daha tarama yapılmaz, uyarı tekrar basılmaz.
//
// BU BİR VARLIK ÖNBELLEĞİ DEĞİLDİR — en önemli ayrım burada: hiçbir bayt,
// varlık, okuma veya varlık-YOK yanıtı saklanmaz. Yalnızca "bu yol için
// teşhis basıldı" bilgisi tutulur. Bu yüzden mount edilmemiş ya da sonradan
// yazılmış bir varlık her zaman TAZE okunur; önbellek hiçbir okuma
// davranışını değiştiremez.
//
// Geçerlilik mount TOPOLOJİSİNE bağlıdır: mountDirectory/mountPackage/
// clearMountPoints her seferinde m_mountGeneration'ı ilerletir ve kaydı
// atar. Mod ve paketler zaten mount anında takılır (remountProject), yani
// topoloji değişmeden alt katmanlara yeni bir kopya eklenmesi teşhisi
// bayatlatabilir — bu, "ilk okuma anındaki disk durumu" sözleşmesidir ve
// sessizlik değildir: remountProject bir kez daha çağrıldığında tüm
// teşhisler yeniden üretilir.
bool VFSManager::claimShadowDiagnosis(const std::string& cleanPath) {
    std::lock_guard<std::mutex> lock(m_shadowMutex);
    if (m_diagnosedGeneration != m_mountGeneration) {
        m_diagnosedGeneration = m_mountGeneration;
        m_diagnosedShadowPaths.clear();
    }
    if (m_diagnosedShadowPaths.size() >= kMaxDiagnosedShadowPaths) {
        m_diagnosedShadowPaths.clear();
    }
    return m_diagnosedShadowPaths.insert(cleanPath).second;
}

// m_mutex ALTINDA çağrılır. Kilit sırası her yerde m_mutex -> m_shadowMutex;
// teşhis yolu yalnızca m_shadowMutex tutar, ters sıra hiç oluşmaz.
void VFSManager::invalidateShadowDiagnosesLocked() {
    std::lock_guard<std::mutex> lock(m_shadowMutex);
    ++m_mountGeneration;
    m_diagnosedShadowPaths.clear();
}

void VFSManager::diagnoseShadowing(
    const std::string& cleanPath,
    const std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>>& mounts,
    size_t hitIndex, bool hitWasPrefixStripped) {
    // Kazananın ARKASINDA mount yoksa gölgeleme İMKÂN DIŞIDIR: hiçbir şey
    // erişilemez kalmıyor. Tarama hiç başlamaz (en dar koşul).
    if (hitIndex + 1 >= mounts.size()) return;
    // Teşhis yol başına BİR KEZ üretilir. 5000 kez aynı varlığı okuyup
    // 5000 kez aynı taramayı koşmak, aynı soruya 5000 kez aynı cevabı
    // vermektir — hem zaman hem gürültü kaybıydı.
    if (!claimShadowDiagnosis(cleanPath)) return;
    // SESSİZ YUTMA KALANI. Bu fonksiyon teşhis YOLUDUR ve teşhis
    // readBytes()/exists()/openStream() gibi düz okuma yollarından çağrılır;
    // C API katmanı invokeNoexcept ile SARAR ve istisnayı yutar. Yani
    // buradan kaçan her istisna, loga düşmeden, kullanıcıya da düşmeden
    // kaybolur — ve P2-6'nın var olma sebebi olan "sessiz mod düşmesi"
    // sessiz teşhis kaybına dönüşür. physicalPathKey() artık kendi dar
    // dönüşümünü yakalıyor, ama bu kalkan KAPANIŞTIR: ileride eklenen
    // bir std::filesystem çağrısının fırlatması teşhisi yine yutmasın.
    // Yakalanan istisna KENDİSİ bir teşhistir: açık bir tanı basılır.
    try {
        reportShadowing(cleanPath, mounts, MountHit{hitIndex, hitWasPrefixStripped});
    } catch (const std::exception& e) {
        ROWL_LOG_WARN("VFS shadow diagnosis FAILED for '" + cleanPath +
                      "': " + e.what() +
                      ". This path was NOT checked for shadowing — a mod may be "
                      "silently unreachable here. Reported rather than swallowed on "
                      "purpose: a swallowed diagnosis is indistinguishable from "
                      "'no shadow found'.");
    }
}

bool VFSManager::exists(const std::string& vfsPath) {
    if (vfsPath.empty()) return false;

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // Snapshot under lock; IO runs lock-free (A2a) — diagnoseShadowing da IO
    // yaptığı için kilidi tutmak burada yanlış olurdu.
    const auto mounts = getMountPoints();
    const auto hit = resolveInMountOrder(
        mounts, cleanPath,
        [](const std::shared_ptr<IDataSource>& s, const std::string& p) {
            return s->exists(p);
        });
    if (!hit) return false;
    diagnoseShadowing(cleanPath, mounts, hit->index, hit->prefixStripped);
    return true;
}

std::optional<std::vector<uint8_t>> VFSManager::readBytesSinglePass(const std::string& cleanPath) {
    // Snapshot under lock; IO runs lock-free (A2a).
    const auto mounts = getMountPoints();
    // Probe sonucu doğrudan tutulur: ikinci bir okuma yapmadan (TOCTOU yok)
    // ve kazanan mount'un baytlarını israf etmeden aynı anda taşınır.
    std::optional<std::vector<uint8_t>> data;
    const auto hit = resolveInMountOrder(
        mounts, cleanPath,
        [&data](const std::shared_ptr<IDataSource>& s, const std::string& p) {
            if (auto d = s->tryRead(p)) {
                data = std::move(*d);
                return true;
            }
            return false;
        });
    if (!hit) return std::nullopt;
    ROWL_LOG_TRACE("VFS Resolved '" + cleanPath + "' via mount #" +
                   std::to_string(hit->index) + " " +
                   mounts[hit->index].second->getSourceName() +
                   (hit->prefixStripped ? " (prefix-stripped)" : " (direct)"));
    diagnoseShadowing(cleanPath, mounts, hit->index, hit->prefixStripped);
    return data;
}

std::unique_ptr<std::istream> VFSManager::openStreamSinglePass(const std::string& cleanPath) {
    const auto mounts = getMountPoints();
    std::unique_ptr<std::istream> stream;
    const auto hit = resolveInMountOrder(
        mounts, cleanPath,
        [&stream](const std::shared_ptr<IDataSource>& s, const std::string& p) {
            if (auto opened = s->tryOpenStream(p)) {
                stream = std::move(opened);
                return true;
            }
            return false;
        });
    if (!hit) return nullptr;
    diagnoseShadowing(cleanPath, mounts, hit->index, hit->prefixStripped);
    return stream;
}

std::vector<uint8_t> VFSManager::readBytes(const std::string& vfsPath) {
    if (vfsPath.empty()) {
        ROWL_LOG_WARN("VFS attempted to read empty path");
        return {};
    }

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // A2a: engaged-but-empty is a REAL empty asset (no "not found" warn);
    // nullopt is a true miss on every source.
    if (auto data = readBytesSinglePass(cleanPath)) return std::move(*data);

    ROWL_LOG_WARN("VFS File not found: '" + cleanPath + "'");
    return {};
}

std::unique_ptr<std::istream> VFSManager::openReadStream(const std::string& vfsPath) {
    if (vfsPath.empty()) return nullptr;
    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
    return openStreamSinglePass(cleanPath);
}

std::string VFSManager::readString(const std::string& vfsPath) {
    auto bytes = readBytes(vfsPath);
    if (bytes.empty()) return "";
    return std::string(bytes.begin(), bytes.end());
}

void setVfsInjectPathKeyThrow(bool inject) {
    g_injectPathKeyThrow.store(inject, std::memory_order_relaxed);
}

bool vfsInjectPathKeyThrow() {
    return g_injectPathKeyThrow.load(std::memory_order_relaxed);
}

} // namespace Rowl::VFS
