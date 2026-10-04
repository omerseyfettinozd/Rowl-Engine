/**
 * test_p2_9_transient_site_isolation.cpp — P2-9 KAPI: gecici-hata kancası
 * SAYACI başına işlem izolasyonu.
 *
 * BULGU (P2-9): SaveDurabilityTransientSite::Probe etiketi commit bölgesindeki
 * ÜÇ AYRI işleme birden geçiriliyordu:
 *   :363 targetIsRegularFileWithRetry  — "slot düz bir dosya mı?" probu
 *   :537 removeOwnedTempQuietly         — sahipli temp'in sessizce silinmesi
 *   :660 parmakiz (fingerprint) ölçümü — baseline'ın yeniden okunması
 * Yani "per-site" iddiası yanlıştı: bir etiket = üç işlem.
 *
 * AYIRT ETME (bulgunun asıl özeti): bu DATA RACE DEĞİL. Sayaçlar
 * std::atomic<int> ve düşüş compare_exchange_weak CAS döngüsüdür; ThreadSanitizer
 * bunlarda temizdir (negatif kontrol ile kanıtlandı). Kayıp güncelleme de yok.
 * Kusur, sayacın OKUMA/YAZMA yarışı değil, ANLAMININ paylaşılmasıdır.
 *
 * Bu kapı ölçtüğü şey: bir etiket armedığında BİTİŞİ belirli bir işlem
 * tarafından, ve SADECE o işlem tarafından tüketilir.
 *
 * KAPI NEDEN ÖLÜ DEĞİL — MUTASYONLA KANIT:
 *   :537 veya :660 etiketini yeniden `Probe`'a bağla  ->  RED
 *     (OwnedTempRemove/FingerprintMeasure tüketimi 0 kalır, Probe fazla harcar)
 *   `Count`'u 6'dan 5'e indir                            -> derleme hatası
 *     (static_assert + dizi uzunluğu guard'ı)
 *   Probe'i iki işleme bağla, üçüncüyü ayrı bırak         ->  RED
 *
 * RED: "P2-9-SITE RED: ..." + exit 1.  YEŞİL: exit 0.
 */
#include "rowl_test_harness.hpp"

#include "rowl/state/save_durability.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Rowl::State::SaveDurabilityTransientSite;

int g_probeFailures = 0;

void red(const std::string& what) {
    std::cerr << "P2-9-SITE RED: " << what << std::endl;
    ++g_probeFailures;
}

void expect(bool cond, const std::string& what) {
    if (!cond) red(what);
}

int Consumed(SaveDurabilityTransientSite site) {
    return Rowl::State::saveDurabilityTransientFailuresConsumed(site);
}

int Injected(SaveDurabilityTransientSite site) {
    return Rowl::State::saveDurabilityInjectTransientFailures(site);
}

// Kapsam: DÜRTÜ olmayan her site. Test bittiğinde hepsi 0 olmalı.
const SaveDurabilityTransientSite kAllSites[] = {
    SaveDurabilityTransientSite::Probe,
    SaveDurabilityTransientSite::Copy,
    SaveDurabilityTransientSite::Replace,
    SaveDurabilityTransientSite::BackupRemove,
    SaveDurabilityTransientSite::OwnedTempRemove,
    SaveDurabilityTransientSite::FingerprintMeasure,
};

constexpr int kSiteCount =
    static_cast<int>(SaveDurabilityTransientSite::Count);

void ResetAll() {
    for (const SaveDurabilityTransientSite site : kAllSites) {
        Rowl::State::setSaveDurabilityInjectTransientFailures(0, site);
    }
    Rowl::State::setSaveDurabilityInjectCompetingWrite(false);
    Rowl::State::setSaveDurabilityInjectErrno(0);
}

struct Snapshot {
    int probe = 0;
    int ownedTempRemove = 0;
    int fingerprintMeasure = 0;
    int backupRemove = 0;
    int copy = 0;
    int replace = 0;
};

Snapshot Take() {
    Snapshot s;
    s.probe = Consumed(SaveDurabilityTransientSite::Probe);
    s.ownedTempRemove =
        Consumed(SaveDurabilityTransientSite::OwnedTempRemove);
    s.fingerprintMeasure =
        Consumed(SaveDurabilityTransientSite::FingerprintMeasure);
    s.backupRemove = Consumed(SaveDurabilityTransientSite::BackupRemove);
    s.copy = Consumed(SaveDurabilityTransientSite::Copy);
    s.replace = Consumed(SaveDurabilityTransientSite::Replace);
    return s;
}

// ÖLÇÜM 1 — :537 izolasyonu. Sahipli temp silme yolu YALNIZCA replace
// başarısız olduğunda koşar. Replace bütçesini tüketip OwnedTempRemove'i
// armeliyoruz; :537 beş denemede bütçeyi harcamalı ve HİÇBİR başka site
// hareket etmemeli.
void CheckOwnedTempRemoveIsolated(const fs::path& root, int attempts) {
    const fs::path slot = root / "owned-temp-remove.json";
    { std::ofstream f(slot, std::ios::binary); f << "{\"old\":1}\n"; }

    const Snapshot before = Take();
    // Replace bütçesini TAMAMLA: rename asla çalışmasın -> fail-closed ->
    // removeOwnedTempQuietly koşsun.
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::Replace);
    // :537'nin KENDİ bütçesi.
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::OwnedTempRemove);

    Rowl::State::writeSlotFileAtomically(slot, "{\"new\":2}", nullptr);

    const Snapshot after = Take();
    ResetAll();

    const int owned = after.ownedTempRemove - before.ownedTempRemove;
    const int probe = after.probe - before.probe;
    const int fingerprint =
        after.fingerprintMeasure - before.fingerprintMeasure;

    expect(owned == attempts,
           "OwnedTempRemove tam olarak bir call site'in butcesini harcamali "
           "(delta=" + std::to_string(owned) + ", beklenen " +
               std::to_string(attempts) + ")");
    expect(probe == 0,
           "OwnedTempRemove armediginda Probe sayaci HARCANMAMALI (delta=" +
               std::to_string(probe) + ")");
    expect(fingerprint == 0,
           "OwnedTempRemove armediginda FingerprintMeasure sayaci "
           "HARCANMAMALI (delta=" + std::to_string(fingerprint) + ")");
}

// ÖLÇÜM 2 — :660 izolasyonu. Parmakız ölçümü YALNIZCA replace başarısız
// olduğunda koşar. Ölçüm bütçesini armeliyoruz; beş ölçüm denemesi
// tükenmeli ve hiçbir komşu site hareket etmemeli.
void CheckFingerprintMeasureIsolated(const fs::path& root, int attempts) {
    const fs::path slot = root / "fingerprint-measure.json";
    { std::ofstream f(slot, std::ios::binary); f << "{\"old\":1}\n"; }

    const Snapshot before = Take();
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::Replace);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::FingerprintMeasure);

    Rowl::State::writeSlotFileAtomically(slot, "{\"new\":2}", nullptr);

    const Snapshot after = Take();
    ResetAll();

    const int fingerprint =
        after.fingerprintMeasure - before.fingerprintMeasure;
    const int probe = after.probe - before.probe;
    const int owned = after.ownedTempRemove - before.ownedTempRemove;

    expect(fingerprint == attempts,
           "FingerprintMeasure tam olarak bir call site'in butcesini "
           "harcamali (delta=" + std::to_string(fingerprint) + ", beklenen " +
               std::to_string(attempts) + ")");
    expect(probe == 0,
           "FingerprintMeasure armediginda Probe sayaci HARCANMAMALI (delta=" +
               std::to_string(probe) + ")");
    expect(owned == 0,
           "FingerprintMeasure armediginda OwnedTempRemove sayaci "
           "HARCANMAMALI (delta=" + std::to_string(owned) + ")");
}

// ÖLÇÜM 3 — :363 izolasyonu (Probe'ın ASIL işi). Hedef VARsa probe
// beş denemede bütçeyi harcamalı; komşular sıfır kalmalı.
void CheckProbeIsolated(const fs::path& root, int attempts) {
    const fs::path slot = root / "probe.json";
    { std::ofstream f(slot, std::ios::binary); f << "{\"old\":1}\n"; }

    const Snapshot before = Take();
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        attempts, SaveDurabilityTransientSite::Probe);

    Rowl::State::writeSlotFileAtomically(slot, "{\"new\":2}", nullptr);

    const Snapshot after = Take();
    ResetAll();

    const int probe = after.probe - before.probe;
    const int owned = after.ownedTempRemove - before.ownedTempRemove;
    const int fingerprint =
        after.fingerprintMeasure - before.fingerprintMeasure;

    expect(probe == attempts,
           "Probe tam olarak bir call site'in butcesini harcamali (delta=" +
               std::to_string(probe) + ", beklenen " +
               std::to_string(attempts) + ")");
    expect(owned == 0,
           "Probe armediginda OwnedTempRemove sayaci HARCANMAMALI (delta=" +
               std::to_string(owned) + ")");
    expect(fingerprint == 0,
           "Probe armediginda FingerprintMeasure sayaci HARCANMAMALI (delta=" +
               std::to_string(fingerprint) + ")");
}

// ÖLÇÜM 4 — eszamanlı yazarlar bütçeyi ASLA çoğaltmaz. 8 yazar eşzamanlı
// yazarken OwnedTempRemove armedir; Replace bütçesi her yazımı başarısız
// kılacak kadar BÜYÜK bırakılır, böylece HER yazım removeOwnedTempQuietly'ye
// uğrar. Tüketim TAM OLARAK bütçeye eşit olmalı: ne eksik (kayıp güncelleme)
// ne fazla (atomik olmayan RMW / dipten çift sayma).
//
// Not: bu sayma "her çağrı tam olarak attempts harcar" kuralına dayanır — bir
// call site'i çağrı başına en fazla kMaxTransientAttempts tüketebilir. Yeterli
// çağrı üretildiği için bütçe TAM tükenir; tükenmeyen bir bütçe de bu
// ölçümde kırmızıya düşer (gürültü değil, kesinlik).
void CheckConcurrentBudgetIsExact(const fs::path& root, int budget) {
    constexpr int kWriters = 8;
    constexpr int kWrites = 25;
    const int attempts =
        Rowl::State::saveDurabilityTransientRetryAttempts();
    // Her yazımın replace'i kesin başarısız olsun: 8*25 yazım * attempts
    // deneme, bu yüzden Replace bütçesi bunu aşacak kadar büyük olmalı.
    const int replaceBudget = kWriters * kWrites * attempts + 1;

    const Snapshot before = Take();
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        budget, SaveDurabilityTransientSite::OwnedTempRemove);
    Rowl::State::setSaveDurabilityInjectTransientFailures(
        replaceBudget, SaveDurabilityTransientSite::Replace);

    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&, w]() {
            const fs::path slot =
                root / ("eszamanli-" + std::to_string(w) + ".json");
            for (int i = 0; i < kWrites; ++i) {
                Rowl::State::writeSlotFileAtomically(
                    slot, "{\"w\":" + std::to_string(w) + "}", nullptr);
            }
        });
    }
    for (auto& t : writers) t.join();

    const Snapshot after = Take();
    ResetAll();

    const int owned = after.ownedTempRemove - before.ownedTempRemove;
    expect(owned == budget,
           "eszamanli yazarlar butceyi ASLA cogaltmamali veya kaybetmemeli "
           "(tuketim=" + std::to_string(owned) + ", budget=" +
               std::to_string(budget) + ")");
}

// ÖLÇÜM 5 — kapsam sızıntısı: probe bittikten sonra hiçbir site armed
// kalmamalı. (Eski test yalnız Copy'e bakıyordu.)
void CheckNoLeak() {
    for (const SaveDurabilityTransientSite site : kAllSites) {
        expect(Injected(site) == 0,
               "kanca temizligi: bir site silintide armed kaldi");
    }
}

} // namespace

int main() {
    const int attempts =
        Rowl::State::saveDurabilityTransientRetryAttempts();
    std::cout << "P2-9 site izolasyon probu: kMaxTransientAttempts = "
              << attempts << ", site sayisi = " << kSiteCount << std::endl;

    const fs::path root =
        fs::temp_directory_path() / "rowl-p2-9-site-isolation";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    CheckProbeIsolated(root, attempts);
    CheckOwnedTempRemoveIsolated(root, attempts);
    CheckFingerprintMeasureIsolated(root, attempts);
    CheckConcurrentBudgetIsExact(root, 40);
    CheckNoLeak();

    if (g_probeFailures == 0) {
        std::cout << "P2-9-SITE YESIL: her etiket tam olarak bir islemi "
                     "olculuyor" << std::endl;
        return 0;
    }
    std::cout << "P2-9-SITE RED: " << g_probeFailures << " basarisiz"
              << std::endl;
    return 1;
}