#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
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
    ROWL_LOG_INFO("VFS Mounted directory: '" + physicalPath + "' under virtual prefix '" + virtualPrefix + "'");
}

void VFSManager::mountPackage(const std::string& virtualPrefix, const std::string& pkgPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto source = std::make_shared<RowlPkgDataSource>(pkgPath);
    if (source->isValid()) {
        m_mountPoints.emplace_back(virtualPrefix, source);
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

/// Bir mount'un FİZİKSEL kökü. remountProject aynı kökü bilerek birden çok
/// ALIAS altında mount eder ('', 'mods', 'Assets', 'images'), yani mount listesi
/// kök başına TEKRARLAR. Aynı kökten iki mount aynı varlık için aynı yanıtı
/// verir — bu yüzden gölgeleme taraması kök başına TEKİL tutulur (aşağıda).
/// Hem doğru hem de yarı maliyetli.
std::string rootIdentity(const std::shared_ptr<IDataSource>& source) {
    if (const auto* loose = dynamic_cast<const LooseDirectorySource*>(source.get())) {
        return fs::path(loose->getPhysicalPath()).lexically_normal().generic_string();
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
        return fs::path(loose->getPhysicalPath()).lexically_normal()
            .append(subPath).generic_string();
    }
    // Paket ayrı bir fiziksel kap: aynı ada sahip bir kayıt, loose bir dosyadan
    // farklı içerik taşıyabilir — her zaman ayrı kimlik sayılır.
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

} // namespace

bool VFSManager::exists(const std::string& vfsPath) {
    if (vfsPath.empty()) return false;

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // Snapshot under lock; IO runs lock-free (A2a) — reportShadowing da IO
    // yaptığı için kilidi tutmak burada yanlış olurdu.
    const auto mounts = getMountPoints();
    const auto hit = resolveInMountOrder(
        mounts, cleanPath,
        [](const std::shared_ptr<IDataSource>& s, const std::string& p) {
            return s->exists(p);
        });
    if (!hit) return false;
    reportShadowing(cleanPath, mounts, *hit);
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
    reportShadowing(cleanPath, mounts, *hit);
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
    reportShadowing(cleanPath, mounts, *hit);
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

} // namespace Rowl::VFS
