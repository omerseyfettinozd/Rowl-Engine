/**
 * rowl/core/chapter_loader.hpp
 *
 * Faz 5 Dilim 4 — chapter-sinirli yukleme (native hat).
 *
 * Kapsam: chapter index (sirali chapter listesi; editor formati
 * chapters/chapter_index.json + LoadChapterFile semasi — native tarafta ayni
 * sema beklenir) + yalniz aktif ve komsu chapter'lar bellekte (uzak chapter
 * unload edilir; unload edilmis node'a erisim fail-closed geri-yukler:
 * seffaf reload + tani). Tek-doslali legacy graf aynen calisir (tek implicit
 * chapter, pencereleme yok). kMaxGraphChapters (1024) korunur.
 *
 * Semalar (editor ChapterStorageService ile birebir):
 * - Index: {format_version?, start_node_id?, node_order?[], chapters[] ({id,
 *   title?, order?, summary?, start_node_id?}), groups?, subgraphs?}
 * - Chapter dosyasi: {format_version?, chapter_id, nodes[] (tam node
 *   payload'lari)}. Kova atamasi dosyanin chapter_id'sine gore yapilir;
 *   node'un dolu chapter_id'si farkliysa dosya reddedilir (fail-closed).
 *   Bos chapter_id'li node'lar "default" kovasina duser (editor Split
 *   davranisi).
 *
 * Format degisikligi YOK: yuklenen node'lar StoryGraphParser'dan gecer,
 * mevcut limitler (10.000 node, bilesen sayisi) aynen uygulanir.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "rowl/core/story_graph.hpp"

namespace Rowl::Core {

/// Bellekte tutulan pencere genisligi: aktif +-1. Aktif +-2 (ve otesi)
/// unload edilir.
inline constexpr std::size_t kChapterNeighborDistance = 1;
/// Tani kuyrugu ust siniri (sinirsiz buyumeyi engeller).
inline constexpr std::size_t kMaxChapterDiagnostics = 32;
/// Eksik-asset / tani listelerinde disari acilan yol sayisi ust siniri.
inline constexpr std::size_t kMaxChapterListedPaths = 64;

class ChapterLoader {
public:
    ChapterLoader() = default;

    // ── Yukleme (hepsi fail-closed: red halinde onceki durum korunur) ──

    /// Tek-doslali legacy veya tam v5 graf JSON'u. Chapter tanimsizsa tek
    /// implicit chapter moduna girer (pencereleme yok, her sey resident).
    bool loadFullGraphJson(const std::string& json, std::string& error);

    /// Editor chapter_index.json semasi. Onceki yuklu durum sifirlanir;
    /// basarida aktif chapter = sirali ilk chapter (yalniz o + komsulari
    /// resident olmaz — henuz chapter dosyasi yok; setActiveChapter pencereyi
    /// kurar).
    bool loadIndexJson(const std::string& json, std::string& error);

    /// Tek chapter dosyasi (LoadChapterFile semasi). Ayni chapter yeniden
    /// eklenirse degistirir (replace); duplicate node id reddedilir.
    bool appendChapterFileJson(const std::string& json, std::string& error);

    /// Aktif chapter'i secer: yalniz aktif +-1 resident kalir, uzak chapter'lar
    /// unload edilir. Bilinmeyen id reddedilir (aktif degismez).
    bool setActiveChapter(const std::string& chapterId, std::string& error);

    /// Tek chapter'i resident yapar (pencereyi genisletir, aktif degismez).
    /// Bilinmeyen id reddedilir.
    bool loadChapter(const std::string& chapterId, std::string& error);

    /// Tek chapter'i unload eder. Aktif chapter unload EDILEMEZ (fail-closed
    /// red). Komsu-pencere disi chapter icin no-op basaridir.
    bool unloadChapter(const std::string& chapterId, std::string& error);

    // ── Erisim ──

    /// Node erisimi. Resident degilse ama bilinen bir kovadaysa SEFFAF
    /// RELOAD yapar (kova resident'e alinir + tani uretilir) ve node'u
    /// doner. Bilinmeyen id -> nullptr + tani.
    const StoryNode* node(uint64_t nodeId);

    /// Chapter-sinir sorgusu: node bir chapter'in start node'u ise veya
    /// successor'larindan herhangi biri farkli chapter'daysa true.
    /// Bilinmeyen node -> false (fail-closed).
    bool isChapterBoundaryNode(uint64_t nodeId) const;

    // ── Gozlemlenebilirlik ──

    bool hasChapters() const noexcept { return !m_order.empty(); }
    bool isLegacySingleGraph() const noexcept { return m_legacySingleGraph; }
    const std::string& activeChapterId() const noexcept { return m_active; }
    /// Sirali chapter kimlikleri (order, sonra id).
    const std::vector<std::string>& orderedChapters() const noexcept { return m_order; }
    /// Su an resident chapter'lar (sirali).
    std::vector<std::string> loadedChapters() const;
    std::size_t residentNodeCount() const noexcept { return m_resident.size(); }
    std::size_t totalNodeCount() const noexcept { return m_totalNodes; }
    uint64_t startNodeId() const noexcept { return m_startNodeId; }
    const std::string& lastDiagnostic() const noexcept { return m_lastDiagnostic; }
    std::vector<std::string> diagnostics() const;

    /// Aktif + komsu listesi JSON: {active, loaded[], neighbors[],
    /// node_counts{}, resident_nodes, total_nodes, last_diagnostic}.
    std::string loadedChaptersJson() const;
    /// Sirali chapter icin kova node kimlikleri (test/rapor icin).
    std::vector<uint64_t> chapterNodeIds(const std::string& chapterId) const;

private:
    struct ChapterMeta {
        std::string id;
        std::string title;
        int order = 0;
        uint64_t startNodeId = 0;
        bool hasStartNodeId = false;
    };

    void rebuildOrder();
    void applyWindow();
    bool bucketOf(uint64_t nodeId, std::string& out) const;
    void recordDiagnostic(const std::string& message);

    std::map<std::pair<int, std::string>, ChapterMeta> m_chapters;
    std::vector<std::string> m_order;
    /// Kova: chapter -> node listesi (parse edilmis, tam payload).
    std::unordered_map<std::string, std::vector<StoryNode>> m_buckets;
    /// node -> kova (hizli reload cozumu).
    std::unordered_map<uint64_t, std::string> m_nodeBucket;
    /// Resident: pencere + seffaf-reload ile yuklenmis node'lar.
    std::unordered_map<uint64_t, StoryNode> m_resident;
    /// Resident'a alinmis kovalar.
    std::unordered_map<std::string, bool> m_residentChapters;

    std::string m_active;
    uint64_t m_startNodeId = 0;
    bool m_hasStartNode = false;
    std::size_t m_totalNodes = 0;
    bool m_legacySingleGraph = false;
    std::string m_lastDiagnostic;
    std::deque<std::string> m_diagnostics;
};

/// Belge-uzerinden chapter-sinir sorgusu (loader durumuna dokunmaz; C API
/// fallback yolu ve legacy tek-dosya graflari icin). Kurallar
/// ChapterLoader::isChapterBoundaryNode ile birebirdir: chapter start node'u
/// veya successor'i farkli chapter'da olan node sinirdir; bilinmeyen node
/// false doner (fail-closed).
bool documentChapterBoundary(const StoryGraphDocument& document, uint64_t nodeId);

} // namespace Rowl::Core
