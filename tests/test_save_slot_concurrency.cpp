// Save-slot concurrency tests — R1 bulgu #3.
//
// Eski kod her yazar için aynı "<slot>.json.tmp" adını O_TRUNC ile
// paylaşıyordu: aynı slota eşzamanlı yazan yazarlar birbirinin baytını
// ezer, son rename sessizce önce yazanları kaybederdi (yırtık/karışık
// final, ama her yazar true dönerdi).
//
// Kural: aynı slota çekiçle vuran yazarların ardından final dosya,
// yazarlardan BİRİNİN tam payload'uyla bayt-bayt aynı olmalı (yapısal
// bütünlük + FNV-1a sağlama). Yırtık/karışık/boş final = kırmızı.
#include <atomic>
#include <barrier>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "rowl/state/save_durability.hpp"
#include "rowl_test_harness.hpp"

namespace {

int g_concFailures = 0;

void checkConc(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "SAVE-SLOT-CONC FAIL: " << what << std::endl;
        ++g_concFailures;
    }
}

// Retry probu kendi sayacını tutar: aynı test dosyasında iki farklı konu
// (eşzamanlılık + geçici-hata retry) var ve ikisi de aynı exit(1) yolunu
// paylaşıyor.
int g_retryFailures = 0;

void checkRetry(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "SAVE-SLOT-RETRY FAIL: " << what << std::endl;
        ++g_retryFailures;
    }
}

uint64_t fnv1a(const std::string& bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Payload: "WRITER:<i> ROUND:<r>\n" + <boy> bayt dolgu + "CHECKSUM:<fnv>\n".
// Dolgu baytı yazara özgüdür ('A'+i), sağlama başlık+govdeyi kapsar.
std::string makePayload(int writer, int round, std::size_t bodySize) {
    std::ostringstream head;
    head << "WRITER:" << writer << " ROUND:" << round << "\n";
    const std::string header = head.str();
    const std::string body(bodySize, static_cast<char>('A' + writer));
    std::ostringstream out;
    out << header << body << "CHECKSUM:" << fnv1a(header + body) << "\n";
    return out.str();
}

// Final dosya yazarlardan birinin TAM payload'u mu? Başlığı parse et,
// gövde tekdüzeliğini ve sağlama toplamını doğrula.
int parseDigits(const std::string& s) {
    if (s.empty()) return -1;
    int value = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

uint64_t parseU64(const std::string& s, bool* ok) {
    uint64_t value = 0;
    *ok = !s.empty();
    for (char c : s) {
        if (c < '0' || c > '9') {
            *ok = false;
            return 0;
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return value;
}

// Tanı: "torn/mixed" tek başına teşhis değildir. HANGI alt kontrolün
// düştüğünü ve gözlenen değerleri döndürür, böylece bir sonraki Windows koşusu
// "kırmızı" yerine gerçekten ne olduğunu söyler. isOneCompletePayload ile
// aynı kontrol sırasını ve aynı eşikleri kullanır.
std::string describePayloadMismatch(const std::string& bytes,
                                    std::size_t bodySize) {
    std::ostringstream out;
    out << "bytes=" << bytes.size() << " (header+" << bodySize << "+tail) ";
    if (bytes.size() < 32) { out << "| too short"; return out.str(); }
    const std::string::size_type nl = bytes.find('\n');
    if (nl == std::string::npos) { out << "| no newline in header"; return out.str(); }
    {
        std::istringstream head(bytes.substr(0, nl));
        std::string wtok, rtok, extra;
        if (!(head >> wtok >> rtok) || (head >> extra)) {
            out << "| header token count wrong: [" << bytes.substr(0, nl) << "]";
            return out.str();
        }
        if (wtok.rfind("WRITER:", 0) != 0 || rtok.rfind("ROUND:", 0) != 0) {
            out << "| header prefix wrong: [" << bytes.substr(0, nl) << "]";
            return out.str();
        }
    }
    const std::string::size_type bodyEnd = nl + 1 + bodySize;
    if (bytes.size() < bodyEnd + 10) {
        out << "| SHORT: needs >= " << (bodyEnd + 10) << " bytes, has "
            << bytes.size() << " -> payload truncated or partially written";
        return out.str();
    }
    const std::string tail = bytes.substr(bodyEnd);
    const std::string::size_type tailNl = tail.find('\n');
    if (tailNl == std::string::npos) {
        out << "| no newline in checksum tail";
        return out.str();
    }
    if (tailNl + 1 != tail.size()) {
        out << "| LONG: " << (tail.size() - (tailNl + 1))
            << " trailing byte(s) after checksum -> two payloads concatenated "
               "or a second append; tail=[" << tail.substr(0, 48) << "]";
        return out.str();
    }
    out << "| checksum/body mismatch (length is right)";
    return out.str();
}

bool isOneCompletePayload(const std::string& bytes, std::size_t bodySize) {
    if (bytes.size() < 32) return false;
    const std::string::size_type nl = bytes.find('\n');
    if (nl == std::string::npos) return false;
    // Başlık: "WRITER:<i> ROUND:<r>" (tek satır, iki token).
    int writer = -1, round = -1;
    {
        std::istringstream head(bytes.substr(0, nl));
        std::string wtok, rtok, extra;
        if (!(head >> wtok >> rtok) || (head >> extra)) return false;
        if (wtok.rfind("WRITER:", 0) != 0 || rtok.rfind("ROUND:", 0) != 0) {
            return false;
        }
        writer = parseDigits(wtok.substr(7));
        round = parseDigits(rtok.substr(6));
    }
    if (writer < 0 || writer >= 8 || round < 0) return false;
    const std::string::size_type bodyEnd = nl + 1 + bodySize;
    if (bytes.size() < bodyEnd + 10) return false;
    const char fill = static_cast<char>('A' + writer);
    for (std::size_t i = nl + 1; i < bodyEnd; ++i) {
        if (bytes[i] != fill) return false;
    }
    // Kuyruk: "CHECKSUM:<u64>\n" ve fazlası yok.
    const std::string tail = bytes.substr(bodyEnd);
    const std::string::size_type tailNl = tail.find('\n');
    if (tailNl == std::string::npos || tailNl + 1 != tail.size()) return false;
    const std::string ctok = tail.substr(0, tailNl);
    if (ctok.rfind("CHECKSUM:", 0) != 0) return false;
    bool ok = false;
    const uint64_t sum = parseU64(ctok.substr(9), &ok);
    if (!ok) return false;
    return sum == fnv1a(bytes.substr(0, bodyEnd));
}

}  // namespace

void test_save_slot_concurrency() {
    namespace fs = std::filesystem;
    static std::atomic<int> rootCounter{0};
    const fs::path root = fs::temp_directory_path() /
                          ("rowl_save_slot_conc_" +
                           std::to_string(rootCounter.fetch_add(1)));
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }
    const fs::path slotPath = root / "slot.json";

    constexpr int kWriters = 8;
    constexpr int kRounds = 25;
    constexpr std::size_t kBodySize = 128 * 1024;

    // Mutex'in kapısı. Linux'ta Windows paylaşım hatası üretilemediği için
    // mutex'i KALDIRMAK başka hiçbir iddiayı kırmızıya düşürmez; bu sayaç
    // tek gözlenebilir işaret. Mutex varken sayı KESİNLİKLE 0'dır, yani bu
    // asla yanlış-pozitif üretmez — yalnızca mutex eksikken yakalar.
    const int overlapsBefore = Rowl::State::saveDurabilityCommitOverlapsReset();

    std::barrier<> startGate(kWriters);
    std::atomic<int> writeFailures{0};
    std::vector<std::thread> threads;
    threads.reserve(kWriters);
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w] {
            startGate.arrive_and_wait();  // tüm yazarlar aynı anda başlar
            for (int r = 0; r < kRounds; ++r) {
                std::string error;
                if (!Rowl::State::writeSlotFileAtomically(
                        slotPath, makePayload(w, r, kBodySize), &error)) {
                    writeFailures.fetch_add(1);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    checkConc(writeFailures.load() == 0, "concurrent writes must all succeed");
    checkConc(Rowl::State::saveDurabilityCommitOverlaps() == 0,
              "two writers entered the commit section at once: the commit "
              "mutex is not excluding them");
    std::string finalBytes;
    {
        std::ifstream input(slotPath, std::ios::binary);
        checkConc(input.is_open(), "final slot file missing after hammer");
        if (input.is_open()) {
            finalBytes.assign((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
        }
    }
    checkConc(!finalBytes.empty(), "final slot file empty after hammer");
    if (!finalBytes.empty()) {
        if (!isOneCompletePayload(finalBytes, kBodySize)) {
            checkConc(false,
                      "final slot is torn/mixed: not one writer's complete "
                      "payload [" + describePayloadMismatch(finalBytes, kBodySize) + "]");
        }
    }

    // Hiçbir artık kalmamalı: 200 yazının tamamı kendi benzersiz tmp'sini
    // rename ile tüketti ve kendi benzersiz .pre-save-bak'ını sildi. Dizin
    // tam olarak slot.json'u içermelidir. Mevcut test yalnızca
    // writeFailures==0'a baktığı için kendi artık sızıntısını göremez; bu
    // blok düzeltmenin TEMİZLİK yollarını geriye dönük korur.
    // Yalnızca yazmaların TAMAMI başarılıysa anlamlıdır: aksi halde artık
    // kalması doğru davranış olurdu ve gürültü çıkarırdı.
    if (writeFailures.load() == 0) {
        std::error_code iterError;
        for (const auto& entry : fs::directory_iterator(root, iterError)) {
            if (iterError) break;
            if (entry.path().filename() != "slot.json") {
                // Test kökü saf ASCII'dir (sayaç + sabit ad), .string() güvenli.
                std::cerr << "SAVE-SLOT-CONC FAIL: stray file left after hammer: "
                          << entry.path().filename().string() << std::endl;
                ++g_concFailures;
            }
        }
    }
    (void)overlapsBefore;

    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    if (g_concFailures != 0) {
        std::cerr << "SAVE-SLOT-CONC: " << g_concFailures << " failure(s)"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Save-slot concurrency: winner is always one complete payload (R1 #3)");
}

// Retry yolu probu — Windows paylaşım yarışının ikinci katmanı.
//
// Eşzamanlı yazar, hedefin DELETE erişimini (ERROR_ACCESS_DENIED) ya da bizim
// kendi kaynak okumamızı (ERROR_SHARING_VIOLATION) geçici olarak reddeder.
// Bu kalıcı bir hata değil, sıradır: writeSlotFileAtomically bu kodları
// sınıflandırır ve kısa bir bütçe içinde yeniden dener.
//
// Kanca (setSaveDurabilityInjectTransientFailures) bu durumu syscall
// üretmeden üretir, böylece yeniden-dene yolu HER platformda koşar. Retry
// döngüsü olmayan bir build'de bu test KIRMIZIDIR: armalanan hatalar yazmayı
// doğrudan düşürür. Yani iddia kendi kendini doğrulamaz, kapı olur.
//
// İki uç birlikte sınanır:
//   (a) bütçe içi sayı  -> yazma BAŞARILI, yeni payload bayt-bayt yerinde
//   (b) bütçeyi aşan sayı -> yazma fail-closed, önceki iyi slot bayt-bayt
//                           korunur, artık tmp/backup bırakılmaz
void test_save_slot_transient_retry() {
    namespace fs = std::filesystem;
    static std::atomic<int> rootCounter{0};
    const fs::path root = fs::temp_directory_path() /
                          ("rowl_save_slot_retry_" +
                           std::to_string(rootCounter.fetch_add(1)));
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
    }
    const fs::path slotPath = root / "slot.json";

    auto readBytes = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return std::string();
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    };

    constexpr std::size_t kBody = 96 * 1024;
    const std::string first = makePayload(1, 0, kBody);
    const std::string second = makePayload(2, 1, kBody);

    const int attempts = Rowl::State::saveDurabilityTransientRetryAttempts();
    checkRetry(attempts > 1, "retry budget must exceed a single attempt");

    // Taban: hedef dosya YOK. (a) bölümünün yedek yolunu (kaynak okuma)
    // gerçekten geçmesi için önce iyi bir slot yazılır.
    {
        std::string baseError;
        const bool baseOk = Rowl::State::writeSlotFileAtomically(
            slotPath, first, &baseError);
        checkRetry(baseOk, "baseline save must succeed: " + baseError);
        checkRetry(readBytes(slotPath) == first,
                   "baseline payload must land byte-for-byte");
    }

    // (a) Bütçeden az geçici hata: yeniden deneme yolu hepsini yutar.
    constexpr int kTransient = 3;
    using Rowl::State::SaveDurabilityTransientSite;
    using Rowl::State::kAllSaveDurabilityTransientSites;
    const int consumedBefore = Rowl::State::saveDurabilityTransientFailuresConsumed(
        SaveDurabilityTransientSite::Copy);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        kTransient, SaveDurabilityTransientSite::Copy);
    std::string retryError;
    const bool retryOk = Rowl::State::writeSlotFileAtomically(
        slotPath, second, &retryError);
    const int consumedAfter = Rowl::State::saveDurabilityTransientFailuresConsumed(
        SaveDurabilityTransientSite::Copy);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        0, SaveDurabilityTransientSite::Copy);

    checkRetry(consumedAfter - consumedBefore == kTransient,
               "the retry loop must absorb every armed transient failure "
               "(consumed delta != " +
                   std::to_string(kTransient) + ")");
    checkRetry(retryOk, "a save under the retry budget must still succeed: " +
                            retryError);
    checkRetry(readBytes(slotPath) == second,
               "retried save must land the new payload byte-for-byte");

    // (b) Yedekleme bütçeyi tüketir: fail-closed. Kanca her başarısız
    // kopyada hedefe yarım bir dosya bırakır (gerçek bir yarıda kesilmiş
    // CopyFile2'nin bıraktığı gibi), dolayısıyla aşağıdaki artık kontrolü
    // "yarım yedek temizleniyor" iddiasının GERÇEK kapısı olur.
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::Copy);
    const std::string good = readBytes(slotPath);
    const int consumedBefore2 = Rowl::State::saveDurabilityTransientFailuresConsumed(
        SaveDurabilityTransientSite::Copy);
    std::string fatalError;
    const bool fatalOk = Rowl::State::writeSlotFileAtomically(
        slotPath, first, &fatalError);
    const int consumedAfter2 = Rowl::State::saveDurabilityTransientFailuresConsumed(
        SaveDurabilityTransientSite::Copy);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        0, SaveDurabilityTransientSite::Copy);

    checkRetry(!fatalOk, "a save over the retry budget must fail closed");
    checkRetry(fatalError.find("Failed to stage pre-save backup") !=
                   std::string::npos,
               "budget exhaustion must fail at the backup stage, got: " +
                   fatalError);
    checkRetry(consumedAfter2 - consumedBefore2 == attempts,
               "the copy must consume its whole budget (expected " +
                   std::to_string(attempts) + " injections)");
    checkRetry(readBytes(slotPath) == good,
               "over-budget failure must leave the previous good slot "
               "byte-identical");

    // (c) Parmakiz koruması: süreçler arası kazanan ezilmemeli. Replace
    // bütçeyi tükettiği için rename hiç çalışmaz, ama yedek HAZIRLANMIŞTIR ve
    // araya "rakip yazar" girer. Eski kodda bu noktada hedef yeni veriyle
    // değişmiş olurdu; restore onu ESKİ yedekle ezerdi — sessiz kayıp
    // güncelleme. Parmakiz koruması farkı görür, restore'u ATLAR ve rakibin
    // verisi durur. Kancanın var olma sebebi tam olarak bu ölçüm.
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::Replace);
    Rowl::State::setSaveDurabilityInjectCompetingWrite(true);
    std::string clobberError;
    const bool clobberOk = Rowl::State::writeSlotFileAtomically(
        slotPath, first, &clobberError);
    Rowl::State::setSaveDurabilityInjectCompetingWrite(false);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        0, SaveDurabilityTransientSite::Replace);

    const std::string marker = Rowl::State::saveDurabilityCompetingWriteMarker();
    checkRetry(!clobberOk, "a rename over budget must fail closed");
    checkRetry(clobberError.find("Failed to atomically replace save slot file") !=
                   std::string::npos,
               "rename budget exhaustion must report the replace failure: " +
                   clobberError);
    checkRetry(readBytes(slotPath) == marker,
               "a failed writer must NOT restore its backup over a newer "
               "cross-process winner (fingerprint guard); slot holds the "
               "competing writer's bytes");

    // Kanca temizliği: TÜM site'ler silintide kalmamalı (yalnız Copy'e
    // bakmak, en son silintide bırakılan siteyi görmezdi). P2-9 sonrası site
    // sayısı 6: liste BAŞLIKTAKİ kAllSaveDurabilityTransientSites'tan gelir;
    // test_p2_9_transient_site_isolation da AYNI listeyi kullanır, böylece iki
    // kapının kapsamı yapısal olarak aynıdır. Liste enum'un Count elemanıyla
    // static_assert ile bağlıdır: yeni site eklenip liste güncellenmezse
    // DERLEME HATASI verir (kapı sessizce daralmaz).
    for (const SaveDurabilityTransientSite site :
         kAllSaveDurabilityTransientSites) {
        checkRetry(Rowl::State::saveDurabilityInjectTransientFailures(site) == 0,
                   "transient injection leaked past cleanup on a site");
        Rowl::State::setSaveDurabilityInjectTransientFailures(0, site);
    }
    checkRetry(!Rowl::State::saveDurabilityInjectCompetingWrite(),
               "competing-write injection leaked past cleanup");

    // Artık kontrolü: başarı da başarısızlık da yalnızca slot.json bırakmalı.
    {
        std::error_code iterError;
        for (const auto& entry : fs::directory_iterator(root, iterError)) {
            if (iterError) break;
            if (entry.path().filename() != "slot.json") {
                checkRetry(false,
                           "stray residue left by a failed/retried save: " +
                               entry.path().filename().string());
            }
        }
    }

    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    if (g_retryFailures != 0) {
        std::cerr << "SAVE-SLOT-RETRY: " << g_retryFailures << " failure(s)"
                  << std::endl;
        std::exit(1);
    }
    TEST_PASS("Save-slot transient retry: under budget succeeds, over budget fails closed");
}
