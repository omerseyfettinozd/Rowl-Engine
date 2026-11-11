/**
 * test_rewind_history_budget_probe.cpp — dialogueHistory bellek bütçesi probu.
 *
 * KÖK NEDEN (F1 / denetim A bulgu 3): GameState::withDialogueHistory her
 * append'de geçmişin (en çok 500 girdi) TAM KOPYASINI alıp previousState
 * zincirinin her halkasında tutuyordu: halka başına ~95 KB, 8000 ilerlemede
 * 778 MB RSS. Geçmiş artık kalıcı (persistent) bir liste: append tek düğüm
 * ayırır, eski düğümler önceki halkayla paylaşılır.
 *
 * ÖLÇÜM (deterministik bayt/düğüm sayacı, RSS DEĞİL — RSS makineye ve
 * allocator'a bağlı): zincirdeki TÜM halkaların geçmişlerinde erişilen
 * farklı düğüm adresleri sayılır.
 *   eski tasarım: halka sayısı x 500 özel girdi (4000 halka -> 2 000 000)
 *   yeni tasarım: eklenen satır sayısı kadar (4000)
 * Bayt bütçesi = düğüm sayısı x (sizeof(girdi) + 120 baytlık satır + sabit
 * düğüm yükü); 8000 ilerlemede < 150 MB hedefinin çok altındadır.
 *
 * KIRMIZI: halkalar arasında paylaşılmayan (özel kopya) düğüm sayısı eklenen
 *          satır sayısını aşar.
 * MUTASYON: GameState::withDialogueHistory içinde listeyi her halkada
 *   `DialogueHistory(history.toVector())` ile yeniden kurmak bu kapıyı
 *   KIRMIZI yapar. Zincir DERİNLİĞİ değişmez (test_rc_soak derin-rewind
 *   sözleşmesi korunur).
 */
#include "rowl_test_harness.hpp"
#include "rowl/state/game_state.hpp"

#include <iostream>
#include <unordered_set>
#include <vector>

int main() {
    TEST_SECTION("rewind history memory budget (structural sharing)");

    constexpr size_t kLinks = 4000;   // 8x the 500 cap: every link past 500 trims
    constexpr size_t kLineBytes = 120;
    auto state = Rowl::State::GameState::createInitialState(101);
    size_t appended = 0;
    for (size_t i = 0; i < kLinks; ++i) {
        state = Rowl::State::GameState::createNextState(state, 101 + i % 60);
        Rowl::State::DialogueHistoryEntry entry;
        entry.nodeId = 101 + i % 60;
        entry.speaker = "Speaker";
        entry.dialogue = std::string(kLineBytes, static_cast<char>('a' + i % 26));
        state = Rowl::State::GameState::withDialogueHistory(state, {entry});
        if (!state) {
            std::cerr << "withDialogueHistory returned null at link " << i << std::endl;
            return 1;
        }
        ++appended;
    }
    if (state->dialogueHistory.size() != Rowl::State::DialogueHistory::kMaxEntries) {
        std::cerr << "RED: visible history is " << state->dialogueHistory.size()
                  << ", expected the 500 cap" << std::endl;
        return 1;
    }

    // Distinct entry storage reachable from the WHOLE rewind chain.
    std::unordered_set<const void*> distinct;
    size_t links = 0;
    std::vector<const void*> scratch;
    for (auto link = std::shared_ptr<const Rowl::State::GameState>(state); link;
         link = link->previousState) {
        scratch.clear();
        link->dialogueHistory.collectNodeAddresses(scratch);
        distinct.insert(scratch.begin(), scratch.end());
        ++links;
    }
    if (distinct.size() > appended) {
        std::cerr << "RED: " << distinct.size() << " distinct history nodes across " << links
                  << " rewind links for " << appended << " appended lines (private copies: "
                  << (distinct.size() - appended) << ")" << std::endl;
        return 1;
    }

    const size_t perNodeBytes = sizeof(Rowl::State::DialogueHistoryEntry) + kLineBytes +
                                3 * sizeof(void*) + 2 * sizeof(size_t);
    const size_t totalBytes = distinct.size() * perNodeBytes;
    constexpr size_t kBudgetBytes = 8u * 1024u * 1024u;
    if (totalBytes > kBudgetBytes) {
        std::cerr << "RED: history storage " << totalBytes << " B > budget " << kBudgetBytes
                  << " B" << std::endl;
        return 1;
    }

    TEST_PASS("4000 rewind links share their history nodes (" + std::to_string(distinct.size()) +
              " distinct for " + std::to_string(appended) + " lines)");
    TEST_PASS("history storage " + std::to_string(totalBytes / 1024) + " KB inside the " +
              std::to_string(kBudgetBytes / 1024) + " KB budget");
    return 0;
}
