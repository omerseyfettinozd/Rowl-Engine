/**
 * test_rewind_history_budget_probe.cpp — dialogueHistory tahsis bütçesi probu.
 *
 * KÖK NEDEN (bu kapının varlık sebebi):
 *   GameState::withDialogueHistory her append'de geçmişin TAM KOPYASINI
 *   alır ve bu kopya previousState zincirinin her halkasında TUTULUR.
 *   std::vector büyüme politikası 2x olduğu için kopya boyutunun ~2 KATI
 *   kapasite ayırır; kuyruktan erase() bu kapasiteyi geri vermez. Zincir
 *   N halka olduğunda boşa ayrılan alan O(N · 500 · 112) bayttır.
 *
 *   ÖLÇÜM (probe_rw_accounting, Linux, deterministik capacity muhasebesi —
 *   RSS DEĞİL: glibc serbest bıraktığı sayfaları geri vermediği için RSS
 *   ölçümü Linux'ta gerçek büyümeyi görmez):
 *       300 advance : 10 234 112 B  ->  5 210 912 B   (1,96x azalma)
 *       1200 advance: 106 936 512 B -> 53 764 400 B   (1,99x azalma)
 *   Kırpma YAPMADAN: 1200 halkanın düğümleri yalnızca 480 400 B tutuyor,
 *   yani toplamın %0,9'u. Asıl maliyet halkanın dialogueHistory kopyası.
 *
 *   ÖNCEKİ AJANIN "42x / 9.7 MB -> 0.23 MB" İDDİASI YANLIŞTI: gerçek
 *   oran ~2x'tir. 2x, std::vector'ün büyüme politikasından gelir ve
 *   tam olarak shrink_to_fit'in kazandığı miktardır. Kapı bu oranı kilitler.
 *
 *   NEDEN KIRPMA YAPILMADI (OLÇÜLDÜ, probe_rw_prune): zincir K halkaya
 *   kırpılırsa test_rc_soak 7. bölümün sözleşmesi KIRILIR — 1200 advance
 *   sonrası rewind(2000) köke (stepId=1) ulaşmak yerine K adım geride
 *   durur:  K=4 -> stepId 1197, K=8 -> 1193, K=64 -> 1137, K=512 -> 689.
 *   Derin-rewind bir ÖZELLİK (kok'a dek geri al), bir yan etki değil.
 *   Bu yüzden kırpma uygulanmadı; bunun yerine tahsis bütçesi daraltıldı.
 *
 * KIRMIZI: bir halka size() < capacity() taşır (gizli kapasite geri gelir)
 *          VEYA toplam tutulan kapasite bütçeyi aşar.
 * YEŞİL  : her halka size()==capacity() ve toplam bütçe içinde.
 *
 * MUTASYON (OLÇÜLDÜ): game_state.cpp'deki shrink_to_fit() satırı silinirse
 *   kapı KIRMIZI döner — "ring 0 carries hidden capacity: size=500
 *   capacity=512 (1344B wasted)".
 *
 * Bu kapı zincir DERİNLİĞİNE dokunmaz: rewind kapasitesi aynen korunur,
 * bu yüzden test_rc_soak 7. bölümdeki derin-rewind sözleşmesi bozulmaz.
 */
#include "rowl_test_harness.hpp"
#include "rowl/state/game_state.hpp"

#include <iostream>
#include <vector>

namespace {

// Üst bütçe: 64 halka × 500 girdi × sizeof(DialogueHistoryEntry).
// shrink_to_fit OLMADANDA kapasite bunun ~2 katına çıkar ve kapı kırmızı
// döner; shrink_to_fit İLE gerçek tutulan kapasite tam olarak budur.
constexpr size_t kRings = 64;
constexpr size_t kMaxHistoryEntries = 500;
constexpr size_t kPerEntryBudget = sizeof(Rowl::State::DialogueHistoryEntry);

// 64 halka, her biri 500 girdiye kadar dolu: en kötü yasal durum.
std::vector<Rowl::State::DialogueHistoryEntry> makeEntries(size_t n) {
    std::vector<Rowl::State::DialogueHistoryEntry> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Rowl::State::DialogueHistoryEntry e;
        e.nodeId = static_cast<uint64_t>(i + 1);   // 0 değil — decode reddiyor
        e.speaker = "S";
        e.dialogue = "line " + std::to_string(i);
        e.contentId = "00000000-0000-4000-8000-000000000000";
        v.push_back(std::move(e));
    }
    return v;
}

} // namespace

int main() {
    TEST_SECTION("rewind history allocation budget");

    const auto full = makeEntries(kMaxHistoryEntries);

    // 64 halka kur, her append tam 500 girdilik kopya üretir (kuyruk hep dolu).
    auto state = Rowl::State::GameState::createInitialState(101);
    size_t totalCapacity = 0;
    size_t totalSize = 0;
    for (size_t i = 0; i < kRings; ++i) {
        state = Rowl::State::GameState::withDialogueHistory(state, full);
        if (!state) {
            std::cerr << "withDialogueHistory returned null at ring " << i << std::endl;
            return 1;
        }
        const auto& h = *state->dialogueHistory;
        // Kırmızı koşulu 1: gizli kapasite. size()==capacity() değilse
        // vektör büyüme politikasının 2x payı hâlâ tutuluyor demektir.
        if (h.capacity() != h.size()) {
            std::cerr << "RED: ring " << i << " carries hidden capacity: size="
                      << h.size() << " capacity=" << h.capacity() << " ("
                      << (h.capacity() - h.size()) * kPerEntryBudget
                      << "B wasted)" << std::endl;
            return 1;
        }
        totalCapacity += h.capacity();
        totalSize += h.size();
    }

    // Kırmızı koşulu 2: toplam bütçe (BAYT cinsinden; totalCapacity girdi
    // sayısıdır, byte'a çeviriyoruz).
    const size_t expectedEntries = kRings * kMaxHistoryEntries;
    const size_t budgetBytes = expectedEntries * kPerEntryBudget;
    if (totalCapacity * kPerEntryBudget > budgetBytes) {
        std::cerr << "RED: total retained capacity " << totalCapacity * kPerEntryBudget
                  << "B > budget " << budgetBytes << "B ("
                  << (totalCapacity * kPerEntryBudget - budgetBytes) << "B over)"
                  << std::endl;
        return 1;
    }

    // Kuyruk sınırı korunuyor: her halka tam 500 girdi tutar.
    if (totalSize != expectedEntries) {
        std::cerr << "RED: total entries " << totalSize << " != expected "
                  << expectedEntries << " (kMaxDialogueHistoryEntries cap regressed)"
                  << std::endl;
        return 1;
    }

    TEST_PASS("withDialogueHistory retains no hidden capacity (size==capacity per ring)");
    TEST_PASS("64 rings x 500 entries stays inside the " + std::to_string(budgetBytes / 1024) +
              "KB allocation budget");
    return 0;
}