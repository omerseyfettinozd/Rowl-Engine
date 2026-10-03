#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/vfs/detail/rowl_sha256.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include <zstd.h>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <algorithm>
#include <optional>
#include <sstream>
#include <array>
#include <streambuf>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rowl::VFS {

namespace {

constexpr uint16_t kSupportedPackageVersion = 1;
constexpr uint64_t kMaxPackageEntryBytes = 128ULL * 1024 * 1024;
constexpr uint32_t kMaxPackageFileCount = 100'000;
constexpr uint64_t kMaxCompressionExpansionRatio = 1'024;
constexpr size_t kCompressedReadChunkBytes = 64 * 1024;
constexpr size_t kDecompressedReadChunkBytes = 64 * 1024;
// D18a-runtime: the packer's embedded manifest always lives here, stored
// uncompressed (flags=0). Its flags=1 records carry `compressed_sha256` —
// the SHA-256 of the compressed bytes — which the reader now verifies.
constexpr const char* kManifestEntryPath = "rowl/manifest.json";

// Integrity closure for BOTH entry classes. The packer writes one digest key
// per record (tools/package_assets.py:347): `sha256` is the hash of the
// UNCOMPRESSED content and is written for EVERY record, while
// `compressed_sha256` (:354) is the hash of the STORED bytes and is added only
// for flags=1 records. Which key applies depends on how the bytes are stored:
//
//   flags=1 (zstd) — the gate hashes the STORED (compressed) bytes, so
//                    `compressed_sha256` is the applicable key.
//   flags=0 (raw)  — loadIndexTable already forces compressedSize ==
//                    uncompressedSize (:479), so the stored bytes ARE the
//                    uncompressed content and the packer's `sha256` applies
//                    directly to them.
//
// Both keys are snapshotted here so the hash gate runs for every entry that
// has one; a mismatch (silently substituted, truncated or corrupted payload)
// fails the read. A missing key keeps the legacy skip — v1 packages and
// pre-D18a manifests warn-open exactly as before. The binary format is
// untouched: the hashes arrive through the manifest, not a new index field.
struct ManifestDigestIndex {
    std::unordered_map<std::string, std::string> digestsByPath;
    std::unordered_map<std::string, std::string> compressedDigestsByPath;

    /// Digest of the bytes as stored in the archive. zstd entries verify the
    /// compressed payload; raw entries verify the payload itself.
    const std::string* findStoredDigest(const std::string& path, uint32_t flags) const {
        const auto& table = (flags == 1) ? compressedDigestsByPath : digestsByPath;
        const auto it = table.find(path);
        return it != table.end() ? &it->second : nullptr;
    }
};

std::optional<std::string> normalizePackagePath(std::string path) {
    if (path.empty() || path.find('\0') != std::string::npos) return std::nullopt;

    std::replace(path.begin(), path.end(), '\\', '/');
    // A2a: entry names are UTF-8 — same narrow-ctor trap as the mount root.
    const std::filesystem::path normalized =
        std::filesystem::path(std::u8string(path.begin(), path.end())).lexically_normal();
    if (normalized.empty() || normalized.is_absolute() || normalized.has_root_name() ||
        normalized.has_root_directory()) {
        return std::nullopt;
    }

    const auto first = normalized.begin();
    if (first == normalized.end() || *first == "..") return std::nullopt;
    // A2a-fix5: generic_string() converts through the ANSI codepage on
    // Windows and THROWS (ERROR_NO_UNICODE_TRANSLATION) on non-ASCII entry
    // names — that throw killed every Windows package mount (5 reds). Keys
    // are UTF-8 by contract: return the u8string bytes verbatim.
    //
    // KANONIK AYIRICI (Windows sha256 bypass düzeltmesi): u8string() yolu
    // PLATFORMUN YEREL ayırıcısını kullanır ('\\' MSVC'de, '/' POSIX'te).
    // Yukarıdaki std::replace'in yaptığı normalizasyonu tam olarak geri
    // alıyordu: anahtarlar Windows'ta "rowl\\manifest.json" oluyor,
    // buildManifestDigestIndex ise sabit "rowl/manifest.json" arıyor,
    // eşleşme olmuyor, buildManifestDigestIndex nullopt dönüyor, hiçbir
    // compressed_sha256 bağlanmıyor ve sha256 kapısındaki
    // "!compressedSha256Hex.empty()" kısa devresi doğrulamayı sessizce
    // atlayıp DEĞİŞTİRİLMİŞ payload'ı sunuyordu.
    //
    // Anahtarlar SÖZLEŞME gereği her yerde '/' olmalı: packer
    // (tools/package_assets.py) '/' üretir, sabitler '/' kullanır, bu
    // fonksiyon da '/' üretmelidir. u8string()'in yerel ayırıcısını
    // döndürmesini beklemek platforma göre sessiz bir veri eşleştirme
    // hatasıdır — Linux'te doğrulanamaz, Windows'ta güvenlik kapısını
    // düşürür. Bu yüzden dönüşte yeniden kanonikleştiriyoruz.
    // NOT: u8string() GECICI bir std::u8string dondurur; iki ayri cagrinin
    // yineleyicilerini ayni range yapicisina gecirmek farkli nesneler
    // arasinda iterator karsilastirmasi (tanimsiz davranis) olurdu.
    // Once yerel degiskene aliyoruz.
    const std::u8string utf8 = normalized.u8string();
    std::string key(utf8.begin(), utf8.end());
    std::replace(key.begin(), key.end(), '\\', '/');
    return key;
}

std::optional<ManifestDigestIndex> buildManifestDigestIndex(
    const std::string& filepath,
    const std::unordered_map<std::string, PackageEntry>& indexTable) {
    const auto manifestIt = indexTable.find(kManifestEntryPath);
    if (manifestIt == indexTable.end()) return std::nullopt;

    // The packer always stores the manifest raw (flags=0, compressed ==
    // uncompressed); anything else is not a manifest this reader trusts.
    const PackageEntry& manifestEntry = manifestIt->second;
    if (manifestEntry.flags != 0 ||
        manifestEntry.compressedSize != manifestEntry.uncompressedSize) {
        return std::nullopt;
    }

    // Separate ifstream: the shared member stream is mutex-guarded for entry
    // reads, and this scan runs once during construction anyway.
    std::ifstream file(Rowl::Platform::pathFromUtf8(filepath), std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    file.seekg(static_cast<std::streamoff>(manifestEntry.offset), std::ios::beg);
    std::vector<char> buffer(manifestEntry.compressedSize);
    if (manifestEntry.compressedSize > 0) {
        file.read(buffer.data(), static_cast<std::streamsize>(manifestEntry.compressedSize));
        if (file.gcount() != static_cast<std::streamsize>(manifestEntry.compressedSize)) {
            return std::nullopt;
        }
    }

    ManifestDigestIndex index;
    try {
        const auto document = nlohmann::json::parse(
            std::string_view(buffer.data(), buffer.size()));
        if (!document.is_object()) return std::nullopt;
        const auto formatIt = document.find("format");
        if (formatIt == document.end() || !formatIt->is_number_integer() ||
            formatIt->get<int>() != 1) {
            return std::nullopt;
        }
        const auto filesIt = document.find("files");
        if (filesIt == document.end() || !filesIt->is_array()) return std::nullopt;
        for (const auto& record : *filesIt) {
            if (!record.is_object()) continue;
            const auto pathIt = record.find("path");
            if (pathIt == record.end() || !pathIt->is_string()) continue;
            // Anahtarı AYNI kanonikleştirici'den geçir: index tablosunun
            // anahtarları da buradan üretiliyor, böylece eşleşme yapısal
            // olarak garanti altında (sözleşmeye/packer ayırıcısına bağlı
            // değil). Ham manifest `path` string'i Windows'ta ayırıcı
            // yüzünden tabloyla uyuşmayabilirdi.
            const auto canonical = normalizePackagePath(pathIt->get<std::string>());
            if (!canonical) continue;

            // P2-7: packer HER kayıt için `sha256` (ham içerik hash'i) yazıyor,
            // `compressed_sha256`'i yalnız flags=1 için ekliyor. İkisi de
            // toplanır; hangisinin geçerli olduğu girişin saklanma biçimine
            // bağlıdır (findStoredDigest). Yalnız compressed_sha256 okumak
            // flags=0 kayıtları ölçülemez bırakıyordu — sessiz bozulma.
            if (const auto digestIt = record.find("sha256");
                digestIt != record.end() && digestIt->is_string()) {
                index.digestsByPath.emplace(*canonical, digestIt->get<std::string>());
            }
            if (const auto compressedDigestIt = record.find("compressed_sha256");
                compressedDigestIt != record.end() && compressedDigestIt->is_string()) {
                index.compressedDigestsByPath.emplace(*canonical,
                                                      compressedDigestIt->get<std::string>());
            }
        }
    } catch (const nlohmann::json::exception&) {
        // A malformed embedded manifest simply carries no verifiable keys —
        // entries behave like the legacy no-key case. There is nothing to
        // verify against, so nothing is silently trusted either.
        return std::nullopt;
    }
    return index;
}

// P2-8 kilit sayaclari: readEntry()'nin GORUNUR birakildigi olcum.
// Yayimlanan zstd akis sayaclari SADECE ZstdEntryStreamBuf::fill() icinde
// artar; readEntry'in ZSTD_decompress() cagrisi onlara hic dokunmaz. Bu
// yuzden "stream acilisi materyalize bir decode yapmadi" iddiasi o
// sayaclarla OLÇULEMEZDI — tryOpenStream'i eski haline (readEntry'i dogrulama
// gecisi olarak cagirmaya) geri almak testi yesil birakti (mutant canli
// kalmadi). Bu iki sayac tam olarak o gecisi gorur:
//  - Materialized: readEntry'in entry-genislik tampona kopyaladigi bayt.
//  - Decoded:      readEntry'in ZSTD_decompress ile urettigi bayt.
// Ucuncu sayaç, kapinin okudugu bayt toplamini olcer ve IKI yolun ikisinde
// de artar (buradaki verifyHashHex ve asagidaki verifyEntryDigest), boylece
// "bir acilis/okuma sakli baytlari tam olarak bir kez hashler" olcumu
// uygulamadan bagimsiz olur; cift hash regresyonu onu 2x'e cikarir.
// Hepsi surec-geneli monoton, yalnizca gozler, davranisi degistirmez; yine
// baz-deger alip delta okunur.
std::atomic<uint64_t> g_pkgEntryMaterializedBytes{0};
std::atomic<uint64_t> g_pkgEntryDecodedBytes{0};
std::atomic<uint64_t> g_pkgEntryDigestBytes{0};

/// Shape check for a manifest digest key: exactly 64 hex characters. Kept
/// separate from the comparison so a caller can fail closed BEFORE spending
/// any I/O on a key that can never match.
bool isSha256Hex(const std::string& expectedHex) {
    if (expectedHex.size() != 32 * 2) return false;
    for (const char character : expectedHex) {
        if (!std::isxdigit(static_cast<unsigned char>(character))) return false;
    }
    return true;
}

/// Compares a raw 32-byte digest against a 64-hex-character SHA-256 string
/// (case-insensitive). Returns false for a wrong-length or non-hex string.
bool digestMatchesHex(const uint8_t digest[32], const std::string& expectedHex) {
    if (!isSha256Hex(expectedHex)) return false;
    std::array<char, 64> lowered{};
    for (size_t i = 0; i < lowered.size(); ++i) {
        const char character = expectedHex[i];
        lowered[i] = (character >= 'A' && character <= 'F')
                         ? static_cast<char>(character - 'A' + 'a')
                         : character;
    }
    static constexpr char kHexDigits[] = "0123456789abcdef";
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < 32; ++i) {
        difference = static_cast<uint8_t>(
            difference |
            (kHexDigits[(digest[i] >> 4) & 0xF] ^ lowered[i * 2]) |
            (kHexDigits[digest[i] & 0xF] ^ lowered[i * 2 + 1]));
    }
    return difference == 0;
}

/// Verifies `bytes` against a 64-hex-character SHA-256 digest
/// (case-insensitive, shape-validated; a wrong-length or non-hex string
/// never verifies).
bool verifyHashHex(const std::vector<uint8_t>& bytes, const std::string& expectedHex) {
    if (!isSha256Hex(expectedHex)) return false;
    g_pkgEntryDigestBytes.fetch_add(bytes.size(), std::memory_order_relaxed);
    Detail::RowlSha256 context;
    Detail::rowlSha256Init(&context);
    if (!bytes.empty()) {
        Detail::rowlSha256Update(&context, bytes.data(), bytes.size());
    }
    uint8_t digest[32];
    Detail::rowlSha256Final(&context, digest);
    return digestMatchesHex(digest, expectedHex);
}

// Hedef #80 üretim metriği (performans kilidi): Zstd giriş-akışlarının GERÇEK
// I/O olay sayaçları (süreç-geneli monoton). Üretim bu olayları zaten yaşar;
// sayaçlar yalnızca gözler, davranışı değiştirmez:
//  - rewind: başarılı decoder (yeniden-)kurulumu (akış-açılışı + geri-seek),
//  - compressed: paketten tüketilen sıkıştırılmış bayt,
//  - decompressed: üretilen sıkıştırılmamış bayt.
// İleri-seek kurulum yapmaz (discardForward artımlı ilerler); koşulsuz-reset
// mutantı rewind sayacında ve yeniden-decompress sınırında ölür.
std::atomic<uint64_t> g_zstdEntryStreamRewinds{0};
std::atomic<uint64_t> g_zstdEntryStreamCompressedBytes{0};
std::atomic<uint64_t> g_zstdEntryStreamDecompressedBytes{0};

// A bounded, seekable decoder stream for a single Zstd package entry.  The
// decoder retains only two fixed-size chunks; seeking rewinds and discards
// bytes rather than materializing the entry in memory.
class ZstdEntryStreamBuf final : public std::streambuf {
public:
    ZstdEntryStreamBuf(const std::string& filepath, const PackageEntry& entry)
        // A2a: open through UTF-8 — the narrow string ctor would reinterpret
        // a non-ASCII package path in the ANSI codepage on Windows.
        : m_file(Rowl::Platform::pathFromUtf8(filepath), std::ios::binary), m_entry(entry),
          m_dstream(ZSTD_createDStream()) {
        setg(m_output.data(), m_output.data(), m_output.data());
        if (!m_file || !m_dstream || !reset()) m_error = true;
    }

    ~ZstdEntryStreamBuf() override { ZSTD_freeDStream(m_dstream); }
    bool valid() const { return m_file.is_open() && m_dstream && !m_error; }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (!fill()) return traits_type::eof();
        return traits_type::to_int_type(*gptr());
    }

    std::streampos seekoff(std::streamoff offset, std::ios_base::seekdir direction,
                           std::ios_base::openmode which) override {
        if (!(which & std::ios_base::in)) return std::streampos(std::streamoff(-1));
        const std::streamoff base = direction == std::ios_base::beg ? 0 :
            direction == std::ios_base::cur ? static_cast<std::streamoff>(m_position - (egptr() - gptr())) :
            direction == std::ios_base::end ? static_cast<std::streamoff>(m_entry.uncompressedSize) : -1;
        if (base < 0 || offset < -base) return std::streampos(std::streamoff(-1));
        return seekTo(static_cast<uint64_t>(base + offset));
    }

    std::streampos seekpos(std::streampos position, std::ios_base::openmode which) override {
        if (!(which & std::ios_base::in) || position < 0) return std::streampos(std::streamoff(-1));
        return seekTo(static_cast<uint64_t>(position));
    }

private:
    // Hedef #80 kilit-yapısı: decoder (yeniden-)kurulumunun TEK sarmalayıcısı.
    // Bütün ZSTD_initDStream çağrıları buradan geçer; başarılı kurulum burada
    // sayılır. reset() ya da gelecekteki başka bir yol doğrudan
    // ZSTD_initDStream çağırırsa sayaç baypas edilir (doğrudan-init bypass) —
    // bu dosyada ikinci bir çağrı noktası açılmamalıdır (grep ile denetlenir).
    bool initDecoder() {
        m_error = !m_file || ZSTD_isError(ZSTD_initDStream(m_dstream));
        // Yalnız başarılı (yeniden-)kurulum sayılır (bozuk akışın düşen
        // kurulumu geriye-sarma değildir). Akış-açılışı dahildir; gözlemler
        // baz-değeri alıp delta okur.
        if (!m_error) ++g_zstdEntryStreamRewinds;
        return !m_error;
    }

    bool reset() {
        m_file.clear();
        m_file.seekg(static_cast<std::streamoff>(m_entry.offset), std::ios::beg);
        m_compressedRead = 0;
        m_position = 0;
        m_input = {nullptr, 0, 0};
        setg(m_output.data(), m_output.data(), m_output.data());
        return initDecoder();
    }

    bool fill() {
        if (m_error || m_position >= m_entry.uncompressedSize) return false;
        ZSTD_outBuffer output{m_output.data(), m_output.size(), 0};
        while (output.pos == 0) {
            if (m_input.pos == m_input.size) {
                if (m_compressedRead >= m_entry.compressedSize) { m_error = true; return false; }
                const auto remaining = m_entry.compressedSize - m_compressedRead;
                const auto bytes = static_cast<std::streamsize>(std::min<uint64_t>(remaining, m_inputBytes.size()));
                m_file.read(reinterpret_cast<char*>(m_inputBytes.data()), bytes);
                if (m_file.gcount() != bytes) { m_error = true; return false; }
                m_compressedRead += static_cast<uint64_t>(bytes);
                // Hedef #80 üretim metriği: paketten tüketilen GERÇEK
                // sıkıştırılmış bayt (monoton; reset'te sıfırlanmaz).
                g_zstdEntryStreamCompressedBytes.fetch_add(static_cast<uint64_t>(bytes),
                                                           std::memory_order_relaxed);
                m_input = {m_inputBytes.data(), static_cast<size_t>(bytes), 0};
            }
            const size_t result = ZSTD_decompressStream(m_dstream, &output, &m_input);
            if (ZSTD_isError(result)) { m_error = true; return false; }
            if (output.pos == 0 && m_input.pos == m_input.size && m_compressedRead == m_entry.compressedSize) {
                m_error = true;
                return false;
            }
        }
        if (output.pos > m_entry.uncompressedSize - m_position) { m_error = true; return false; }
        m_position += output.pos;
        // Hedef #80 üretim metriği: üretilen GERÇEK sıkıştırılmamış bayt
        // (monoton; reset'te sıfırlanmaz — ileri-seek maliyeti buradan okunur).
        g_zstdEntryStreamDecompressedBytes.fetch_add(output.pos, std::memory_order_relaxed);
        setg(m_output.data(), m_output.data(), m_output.data() + output.pos);
        return true;
    }

    // Hedef #80: ileri-seek decoder reset'i YAPMAZ — o anki konumdan hedefe
    // artımlı discard edilir (sıkıştırılmış girdi baştan çözülmez, örn. OGG
    // akışının pump/loop dışı ileri sarımları). Yalnızca geri-seek
    // (hedef < konum) veya bozuk durum baştan reset gerektirir.
    std::streampos seekTo(uint64_t target) {
        if (target > m_entry.uncompressedSize) return std::streampos(std::streamoff(-1));
        if (!m_error) {
            const uint64_t current =
                m_position - static_cast<uint64_t>(egptr() - gptr());
            if (target >= current) {
                return discardForward(target - current);
            }
        }
        if (!reset()) return std::streampos(std::streamoff(-1));
        return discardForward(target);
    }

    // Artımlı discard: o anki konumdan `count` bayt tüketir (decoder reset'i
    // YOKTUR). Başarıda yeni mantıksal konumu, düşmede -1 döner.
    std::streampos discardForward(uint64_t count) {
        std::array<char, kDecompressedReadChunkBytes> discard{};
        while (count > 0) {
            const auto chunk = static_cast<std::streamsize>(std::min<uint64_t>(count, discard.size()));
            const auto read = sgetn(discard.data(), chunk);
            if (read != chunk) return std::streampos(std::streamoff(-1));
            count -= static_cast<uint64_t>(read);
        }
        return std::streampos(static_cast<std::streamoff>(m_position - (egptr() - gptr())));
    }

    std::ifstream m_file;
    PackageEntry m_entry;
    ZSTD_DStream* m_dstream = nullptr;
    std::array<uint8_t, kCompressedReadChunkBytes> m_inputBytes{};
    std::array<char, kDecompressedReadChunkBytes> m_output{};
    ZSTD_inBuffer m_input{nullptr, 0, 0};
    uint64_t m_compressedRead = 0;
    uint64_t m_position = 0;
    // Hedef #80 notu (2. tur): akış-üyesi ayna sayaçlar (m_rewinds /
    // m_compressedBytes / m_decompressedBytes) ÖLÜYDÜ — yalnız yazılıyor,
    // hiç okunmuyordu. Tek canlı zstd akışı + baz-delta izolasyonu varken
    // süreç-geneli monoton sayaçlar aynı gözlemi verir; üye aynalar okunmadan
    // durum şişirirdi, bu yüzden SİLİNDİ (per-stream getter açılmadı).
    bool m_error = false;
};

class ZstdEntryIStream final : public std::istream {
public:
    ZstdEntryIStream(const std::string& filepath, const PackageEntry& entry)
        : std::istream(&m_buffer), m_buffer(filepath, entry) {
        if (!m_buffer.valid()) setstate(std::ios::badbit);
    }
private:
    ZstdEntryStreamBuf m_buffer;
};


} // namespace

RowlPkgDataSource::RowlPkgDataSource(std::string pkgFilepath)
    : m_filepath(std::move(pkgFilepath)) {
    // A2a: UTF-8 open (bkz. ZstdEntryStreamBuf).
    m_fileStream.open(Rowl::Platform::pathFromUtf8(m_filepath), std::ios::binary);
    if (!m_fileStream.is_open()) {
        ROWL_LOG_WARN("Failed to open package archive: " + m_filepath);
        return;
    }

    m_isValid = loadIndexTable();
}

RowlPkgDataSource::~RowlPkgDataSource() {
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

bool RowlPkgDataSource::loadIndexTable() {
    m_fileStream.seekg(0, std::ios::end);
    const auto archiveEnd = m_fileStream.tellg();
    if (archiveEnd < static_cast<std::streamoff>(sizeof(RowlPkgHeader))) {
        ROWL_LOG_ERROR("Package is smaller than its header: " + m_filepath);
        return false;
    }
    const auto archiveSize = static_cast<uint64_t>(archiveEnd);
    m_fileStream.seekg(0, std::ios::beg);

    RowlPkgHeader header;
    m_fileStream.read(reinterpret_cast<char*>(&header), sizeof(RowlPkgHeader));

    if (m_fileStream.gcount() < static_cast<std::streamsize>(sizeof(RowlPkgHeader))) {
        ROWL_LOG_ERROR("Package header read failed: " + m_filepath);
        return false;
    }

    if (std::memcmp(header.magic, "ROWL", 4) != 0) {
        ROWL_LOG_ERROR("Invalid package magic cookie in file: " + m_filepath);
        return false;
    }

    if (header.specVersion != kSupportedPackageVersion) {
        ROWL_LOG_ERROR("Unsupported package version: " + std::to_string(header.specVersion));
        return false;
    }

    // Validate header values
    if (header.fileCount > kMaxPackageFileCount) {
        ROWL_LOG_ERROR("Package file count too large: " + std::to_string(header.fileCount));
        return false;
    }

    if (header.indexOffset < sizeof(RowlPkgHeader) || header.indexOffset > archiveSize) {
        ROWL_LOG_ERROR("Package index offset suspiciously large: " + std::to_string(header.indexOffset));
        return false;
    }
    // Every entry needs its fixed record plus at least one path byte. Check
    // this before reserving vectors/maps from untrusted fileCount metadata.
    constexpr uint64_t kMinIndexEntryBytes = sizeof(RowlPkgEntryRaw) + 1;
    const uint64_t availableIndexBytes = archiveSize - header.indexOffset;
    if (header.fileCount > availableIndexBytes / kMinIndexEntryBytes) {
        ROWL_LOG_ERROR("Package file count cannot fit in the declared index: " + m_filepath);
        return false;
    }

    // Seek to index table offset
    m_fileStream.seekg(header.indexOffset, std::ios::beg);
    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to seek to index offset in package: " + m_filepath);
        return false;
    }

    std::vector<std::pair<uint64_t, uint64_t>> payloadRanges;
    payloadRanges.reserve(header.fileCount);
    for (uint32_t i = 0; i < header.fileCount; ++i) {
        RowlPkgEntryRaw rawEntry;
        m_fileStream.read(reinterpret_cast<char*>(&rawEntry), sizeof(RowlPkgEntryRaw));

        if (!m_fileStream.good()) {
            ROWL_LOG_ERROR("Failed to read package entry: " + std::to_string(i));
            return false;
        }

        if (rawEntry.pathLength == 0 || rawEntry.pathLength > 4096) {
            ROWL_LOG_ERROR("Invalid path length in package entry: " + std::to_string(rawEntry.pathLength));
            return false;
        }

        std::string relPath(rawEntry.pathLength, '\0');
        m_fileStream.read(&relPath[0], rawEntry.pathLength);

        if (!m_fileStream.good() || m_fileStream.gcount() < static_cast<std::streamsize>(rawEntry.pathLength)) {
            ROWL_LOG_ERROR("Failed to read path data for package entry");
            return false;
        }

        const auto normalizedPath = normalizePackagePath(relPath);
        if (!normalizedPath) {
            ROWL_LOG_ERROR("Unsafe path in package entry: " + relPath);
            return false;
        }
        // pathHash is deliberately not used as an authority boundary. Older
        // package writers stored a 32-bit legacy hash here, while current
        // writers use FNV-1a 64. The canonical path is the lookup key, and
        // duplicate canonical paths are rejected below.

        // Validate sizes
        if (rawEntry.compressedSize > kMaxPackageEntryBytes ||
            rawEntry.uncompressedSize > kMaxPackageEntryBytes) {
            ROWL_LOG_ERROR("Package entry size too large, possible corruption");
            return false;
        }
        if (rawEntry.offset < sizeof(RowlPkgHeader) || rawEntry.offset > header.indexOffset ||
            rawEntry.compressedSize > header.indexOffset - rawEntry.offset) {
            ROWL_LOG_ERROR("Package entry points outside archive: " + relPath);
            return false;
        }
        if (rawEntry.flags > 1 ||
            (rawEntry.flags == 0 && rawEntry.compressedSize != rawEntry.uncompressedSize) ||
            (rawEntry.flags == 1 && (rawEntry.compressedSize == 0 || rawEntry.uncompressedSize == 0))) {
            ROWL_LOG_ERROR("Invalid compression metadata for package entry: " + relPath);
            return false;
        }
        if (rawEntry.flags == 1 &&
            rawEntry.uncompressedSize / rawEntry.compressedSize > kMaxCompressionExpansionRatio) {
            ROWL_LOG_ERROR("Package entry compression ratio is too large: " + relPath);
            return false;
        }

        PackageEntry entry;
        entry.relativePath = *normalizedPath;
        entry.offset = rawEntry.offset;
        entry.compressedSize = rawEntry.compressedSize;
        entry.uncompressedSize = rawEntry.uncompressedSize;
        entry.flags = rawEntry.flags;

        // D18a-runtime: populate the digest AFTER the table insert succeeds —
        // the digest index walks m_indexTable, so a rejected duplicate must
        // not leave a poisoned digest behind.
        if (!m_indexTable.emplace(entry.relativePath, entry).second) {
            ROWL_LOG_ERROR("Duplicate package path: " + entry.relativePath);
            return false;
        }
        payloadRanges.emplace_back(entry.offset, entry.offset + entry.compressedSize);
    }

    std::sort(payloadRanges.begin(), payloadRanges.end());
    for (size_t i = 1; i < payloadRanges.size(); ++i) {
        if (payloadRanges[i].first < payloadRanges[i - 1].second) {
            ROWL_LOG_ERROR("Overlapping payload ranges in package: " + m_filepath);
            return false;
        }
    }

    // D18a-runtime: the table is complete — snapshot the embedded manifest's
    // digest keys and attach them to the matching entries.
    // P2-7: previously ONLY flags=1 entries were bound, which left every raw
    // (flags=0) entry permanently unverified even though the packer writes a
    // `sha256` key for them. Now every entry binds the digest that applies to
    // how its bytes are stored (see ManifestDigestIndex::findStoredDigest).
    // A v1/pre-D18a manifest (no keys) leaves every entry empty → skip, and
    // a package without an embedded manifest behaves exactly as before.
    if (auto digests = buildManifestDigestIndex(m_filepath, m_indexTable)) {
        for (auto& [path, tableEntry] : m_indexTable) {
            // The manifest entry itself carries no record of its own; binding
            // a digest to it would make the index verify against itself.
            if (path == kManifestEntryPath) continue;
            if (const std::string* digest = digests->findStoredDigest(path, tableEntry.flags)) {
                tableEntry.compressedSha256Hex = *digest;
            }
        }
    }

    ROWL_LOG_INFO("Successfully loaded package index from '" + m_filepath + "' (" + std::to_string(header.fileCount) + " files)");
    return true;
}

bool RowlPkgDataSource::exists(const std::string& path) {
    if (!m_isValid) return false;
    const auto normalizedPath = normalizePackagePath(path);
    return normalizedPath && m_indexTable.find(*normalizedPath) != m_indexTable.end();
}

std::vector<uint8_t> RowlPkgDataSource::read(const std::string& path) {
    if (!m_isValid) return {};

    const auto normalizedPath = normalizePackagePath(path);
    if (!normalizedPath) return {};
    auto it = m_indexTable.find(*normalizedPath);
    if (it == m_indexTable.end()) {
        return {};
    }

    auto data = readEntry(it->second, path);
    return data ? std::move(*data) : std::vector<uint8_t>{};
}

std::optional<std::vector<uint8_t>> RowlPkgDataSource::tryRead(const std::string& path) {
    if (!m_isValid) return std::nullopt;
    const auto normalizedPath = normalizePackagePath(path);
    if (!normalizedPath) return std::nullopt;
    auto it = m_indexTable.find(*normalizedPath);
    if (it == m_indexTable.end()) return std::nullopt;
    return readEntry(it->second, path);
}

std::optional<std::vector<uint8_t>> RowlPkgDataSource::readEntry(const PackageEntry& entry,
                                                                 const std::string& path) {
    std::vector<uint8_t> compressedBuffer(entry.compressedSize);
    {
        // Thread-safe file access for multi-threaded VFS. A2a: the mutex
        // covers only the shared-stream seek+read — ZSTD_decompress below
        // touches locals only, so decodes no longer serialize on IO.
        std::lock_guard<std::mutex> lock(m_fileMutex);

        // #143: sticky failbit — truncated/kısa okuma fail+eof kurunca
        // sonraki her seekg no-op olup tüm paket remount'a kadar
        // kararıyordu. Her teşebbüs bayrakları temizler: kalıcı hata bu
        // girdiye sınırlanır, akış bir sonraki çağrıda yeniden denenir.
        m_fileStream.clear();
        m_fileStream.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);

        if (!m_fileStream.good()) {
            ROWL_LOG_ERROR("Failed to seek to entry offset in package: " + path);
            return std::nullopt;
        }

        if (entry.compressedSize > 0) {
            m_fileStream.read(reinterpret_cast<char*>(compressedBuffer.data()),
                              static_cast<std::streamsize>(entry.compressedSize));

            if (m_fileStream.gcount() != static_cast<std::streamsize>(entry.compressedSize)) {
                ROWL_LOG_ERROR("Failed to read compressed data for: " + path);
                return std::nullopt;
            }
        }
    }

    // P2-8 kilidi: readEntry gercekten bu entry'yi TAMAMEN materyalize ediyor.
    // Bu sayac gecisi gorunur kilar. tryOpenStream'in ZSTD kolu readEntry'i
    // KULLANMAMALIDIR (o kol verifyEntryDigest ile dogrulanir) — ham kol ise
    // readEntry'e duser ve materyalizasyon zaten zorunludur.
    g_pkgEntryMaterializedBytes.fetch_add(entry.compressedSize, std::memory_order_relaxed);

    // D18a-runtime: verify the bytes as stored in the archive against the
    // manifest digest BEFORE decoding — a silently substituted, truncated or
    // hash-invalidating corrupted payload fails the read instead of serving
    // content. No key (legacy record) skips exactly as before.
    // P2-7: the `entry.flags == 1 &&` guard is GONE. It meant raw (flags=0)
    // entries were never hashed at all, so a corrupted raw payload was served
    // silently while its flags=1 twin failed hard. The digest is chosen by the
    // binding step for the entry's storage class, and flags=0 entries store
    // their content verbatim (compressedSize == uncompressedSize, enforced in
    // loadIndexTable), so the same stored-bytes hash applies to both classes.
    if (!entry.compressedSha256Hex.empty() &&
        !verifyHashHex(compressedBuffer, entry.compressedSha256Hex)) {
        ROWL_LOG_ERROR("Package entry failed manifest sha256 verification: " + path);
        return std::nullopt;
    }

    if (entry.flags == 0) {
        // Raw uncompressed file data (an engaged empty vector is a real
        // empty entry, never a miss).
        return compressedBuffer;
    } else if (entry.flags == 1) {
        // Zstd compressed chunk
        std::vector<uint8_t> decompressedBuffer(entry.uncompressedSize);
        size_t result = ZSTD_decompress(
            decompressedBuffer.data(), entry.uncompressedSize,
            compressedBuffer.data(), entry.compressedSize
        );

        if (ZSTD_isError(result)) {
            ROWL_LOG_ERROR("Zstd decompression failed for asset '" + path + "': " + std::string(ZSTD_getErrorName(result)));
            return std::nullopt;
        }
        if (result != entry.uncompressedSize) {
            ROWL_LOG_ERROR("Zstd output size mismatch for asset '" + path + "'");
            return std::nullopt;
        }

        // P2-8 kilidi: tam-bos decode olcumu. Bir akis acilisinda bu sayac
        // ARTMAYI gorunur kilar — akis, donen decode eden akis kendisi
        // uretir, dogrulama gecisi uretmez.
        g_pkgEntryDecodedBytes.fetch_add(entry.uncompressedSize, std::memory_order_relaxed);

        return decompressedBuffer;
    }

    ROWL_LOG_WARN("Unsupported compression flag for asset: " + path);
    return std::nullopt;
}

std::unique_ptr<std::istream> RowlPkgDataSource::openStream(const std::string& path) {
    return tryOpenStream(path);
}

std::unique_ptr<std::istream> RowlPkgDataSource::tryOpenStream(const std::string& path) {
    if (!m_isValid) return nullptr;
    const auto normalizedPath = normalizePackagePath(path);
    if (!normalizedPath) return nullptr;
    const auto it = m_indexTable.find(*normalizedPath);
    if (it == m_indexTable.end()) return nullptr;

    // P2-8: streams must satisfy the SAME manifest-hash gate as read(), and
    // they must satisfy it over the exact bytes the caller will consume.
    //
    // Previously this called readEntry() purely as a verification pass: it
    // materialized the whole entry AND ran a full ZSTD_decompress, then threw
    // every byte away and handed back a ZstdEntryIStream that decoded the
    // entry a second time. That made the gate both useless (the verified bytes
    // were not the bytes read — a second decode from the file happened after)
    // and expensive (~240x the consumer's own read, ~16 MiB of dead temporary
    // memory for an 8 MiB entry).
    //
    // The gate is a hash of the bytes AS STORED, so decompression is not part
    // of it at all. verifyEntryDigest streams the stored bytes in bounded
    // chunks and hashes them in place — no entry-wide buffer, no decoder —
    // and the same on-disk range is then decoded once by the returned stream.
    // Verified bytes and consumed bytes are therefore the same bytes.
    if (it->second.flags == 1) {
        if (!verifyEntryDigest(it->second, path)) return nullptr;
        auto stream = std::make_unique<ZstdEntryIStream>(m_filepath, it->second);
        return stream->good() ? std::move(stream) : nullptr;
    }
    // Raw entries: NO verifyEntryDigest here. The raw branch falls through to
    // readEntry() below, and readEntry() already runs the SAME manifest gate
    // (unconditional since P2-7) before it hands back a single byte. Calling
    // verifyEntryDigest as well hashed the identical stored range twice — a
    // pure 2x regression on the raw stream path (measured 37x wall clock for a
    // 32 MiB raw entry, because the two SHA-256 passes over 32 MiB cost ~2x233 ms).
    // This is NOT a security regression: raw is still verified, one hash pass,
    // inside readEntry. It only holds for raw; the zstd branch above cannot
    // use readEntry without resurrecting the dead decode, so it verifies here.
    //
    // Raw entries remain bounded by package validation. They do not require a
    // decoder. A2a: single lookup via readEntry (no exists probe), and the
    // bytes move into the stream — the old read()+copy+copy is one copy now.
    // A present-but-empty entry yields a valid empty stream, not nullptr.
    auto data = readEntry(it->second, path);
    if (!data) return nullptr;
    std::string bytes(reinterpret_cast<const char*>(data->data()), data->size());
    return std::make_unique<std::istringstream>(std::move(bytes), std::ios::binary);
}

bool RowlPkgDataSource::verifyEntryDigest(const PackageEntry& entry, const std::string& path) {
    // Legacy package: no manifest key to verify against. Warn-open, exactly as
    // before — fail-CLOSED applies to a key that is PRESENT and does not match.
    if (entry.compressedSha256Hex.empty()) return true;

    // A present-but-malformed key (wrong length, non-hex) can never verify.
    // Check the shape before any I/O so a broken manifest fails closed even if
    // the payload range is also unreadable.
    if (!isSha256Hex(entry.compressedSha256Hex)) {
        ROWL_LOG_ERROR("Package entry has a malformed manifest sha256 key: " + path);
        return false;
    }

    // Stream the stored bytes through SHA-256 in bounded chunks. The digest
    // covers the bytes as stored, so this is exactly what read()'s gate
    // checks — without materializing the entry or running a decoder.
    Detail::RowlSha256 context;
    Detail::rowlSha256Init(&context);

    // Aynı paylaşımlı-akış disiplini readEntry'inkinin kendisi: KİLİT yalnız
    // seek+read'i kapsar, hash KİLİT DIŞINDA beslenir.
    //
    // Yarış-durum kanıtı (okunan baytlar degismez):
    //  1. Her yineleme MUTLAK bir konuma (entry.offset + okunan) seek eder.
    //     Göreli konum paylaşımlı akışın nereye baktığına bağlı olsaydı
    //     başka bir is parçacığının araya girmesi doğru olmayan baytları
    //     okutmamıza yol açardı; mutlak ofset bu bağımlılığı tamamen keser.
    //  2. seekg ile read() aynı lock_guard kapsamındadır; aralarında başka bir
    //     parçacık m_fileStream'e erişemez, dolayısıyla read() tam olarak
    //     (offset + okunan .. offset + okunan + want) aralığını okur.
    //  3. Hash, kilit bırakıldıktan SONRA, kilidi tutan parçacığın dokunamadığı
    //     yerel `chunk` tamponu üzerinde beslenir.
    // Sonuç: kilit tutma süresi I/O ile sınırlı, hash maliyeti kilit dışında —
    // eşzamanlı akış açmaları artık serileşmez.
    std::array<uint8_t, kCompressedReadChunkBytes> chunk{};
    uint64_t consumed = 0;
    while (consumed < entry.compressedSize) {
        const auto want = static_cast<std::streamsize>(
            std::min<uint64_t>(entry.compressedSize - consumed, chunk.size()));
        {
            std::lock_guard<std::mutex> lock(m_fileMutex);
            // #143: sticky failbit — her teşebbüs bayrakları temizler.
            m_fileStream.clear();
            m_fileStream.seekg(static_cast<std::streamoff>(entry.offset + consumed),
                               std::ios::beg);
            if (!m_fileStream.good()) {
                ROWL_LOG_ERROR("Failed to seek to entry offset in package: " + path);
                return false;
            }
            m_fileStream.read(reinterpret_cast<char*>(chunk.data()), want);
            if (m_fileStream.gcount() != want) {
                ROWL_LOG_ERROR("Failed to read stored data for: " + path);
                return false;
            }
        }
        // KİLİT DIŞI: yalnız yerel bağlam (context) ve yerel tampon.
        Detail::rowlSha256Update(&context, chunk.data(), static_cast<size_t>(want));
        g_pkgEntryDigestBytes.fetch_add(static_cast<uint64_t>(want), std::memory_order_relaxed);
        consumed += static_cast<uint64_t>(want);
    }

    uint8_t digest[32];
    Detail::rowlSha256Final(&context, digest);
    if (!digestMatchesHex(digest, entry.compressedSha256Hex)) {
        ROWL_LOG_ERROR("Package entry failed manifest sha256 verification: " + path);
        return false;
    }
    return true;
}

uint64_t zstdEntryStreamRewindCount() {
    return g_zstdEntryStreamRewinds.load(std::memory_order_relaxed);
}

uint64_t zstdEntryStreamCompressedBytes() {
    return g_zstdEntryStreamCompressedBytes.load(std::memory_order_relaxed);
}

uint64_t zstdEntryStreamDecompressedBytes() {
    return g_zstdEntryStreamDecompressedBytes.load(std::memory_order_relaxed);
}

uint64_t pkgEntryMaterializedBytes() {
    return g_pkgEntryMaterializedBytes.load(std::memory_order_relaxed);
}

uint64_t pkgEntryDecodedBytes() {
    return g_pkgEntryDecodedBytes.load(std::memory_order_relaxed);
}

uint64_t pkgEntryDigestBytes() {
    return g_pkgEntryDigestBytes.load(std::memory_order_relaxed);
}

} // namespace Rowl::VFS
