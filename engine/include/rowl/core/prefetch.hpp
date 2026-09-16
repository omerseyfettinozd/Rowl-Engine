/**
 * rowl/core/prefetch.hpp
 *
 * Faz 5 Dilim 4 — butceli asset prefetch (native hat).
 *
 * Kapsam: aktif + sonraki sahnenin asset listesi (node bilesenlerindeki
 * image/audio yollari: sprite/texture, bgm/voice/sfx/ambience, character slot
 * assetleri), VFS uzerinden bayt-butceli (varsayilan 32 MiB, ust sinir
 * 128 MiB) + sure-butceli (frame basina ~4 ms, asinca ertele) senkron pump.
 * THREAD YOK: pump update-thread tarafindan cagrilir (Dilim 1 streaming pump
 * deseni). Ilerleme gozlemlenebilirligi: hazir/eksik sayaci + bayt. Eksik
 * asset prefetch'i durdurmaz (sayaca isler, tani uretir).
 *
 * Format degisikligi YOK. Audio streaming/mixer/character semantigine
 * DOKUNULMAZ: bu dosya yalnizca VFS okumasi yapar, decode calistirmaz.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "rowl/core/story_graph.hpp"

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Core {

/// Varsayilan bayt butcesi: 32 MiB. Uzeri override ile istenebilir.
inline constexpr uint64_t kPrefetchDefaultBudgetBytes = 32ULL * 1024 * 1024;
/// Bayt butcesi ust siniri: 128 MiB. Uzeri clamp'lenir (red degil).
inline constexpr uint64_t kPrefetchMaxBudgetBytes = 128ULL * 1024 * 1024;
/// Frame basina varsayilan pump suresi: ~4 ms. Asinca ertele (kuyruk korunur).
inline constexpr double kPrefetchDefaultPumpMilliseconds = 4.0;
/// Pump suresi ust siniri: 50 ms (tek cagrida frame'i kilitlememek icin).
inline constexpr double kPrefetchMaxPumpMilliseconds = 50.0;

/// Tek prefetch kalemi: VFS yolu + tur + kaynagi (tani icin).
struct PrefetchAsset {
    std::string path;
    /// "image" | "audio-bgm" | "audio-voice" | "audio-sfx" | "audio-ambience" |
    /// "character-body" | "character-face" | "character-outfit" | "character-accessory"
    std::string kind;
    uint64_t nodeId = 0;
    std::string chapterId;
};

/// Tek node'un asset listesi: legacy top-level alanlar (background,
/// character) + component anahtarlari (background.texture, character.sprite +
/// character.layers.*, dialogue.custom_box_texture + typewriter/voice-blip
/// sesleri, character.voice_blip sesi, audio.bgm_track/sfx_track + ileriye
/// donuk voice_track/ambience_track, choice option gorselleri).
/// Bos yollar atlanir; ayni yol tek kalem olarak tutulur (ilk gorunum kazanir).
std::vector<PrefetchAsset> collectNodeAssets(const StoryNode& node);

/// Verilen node kimliklerinin asset listesi (dokuman sirasi korunur, dokumanda
/// olmayan kimlikler sessizce atlanir). Cikti yol-bazinda tekillestirilir.
std::vector<PrefetchAsset> collectDocumentAssets(const StoryGraphDocument& document,
                                                 const std::vector<uint64_t>& nodeIds);

/// Dokumanin chapter sirasi (order, sonra id). v4 dokumanlarda bos doner.
std::vector<std::string> orderedChapterIds(const StoryGraphDocument& document);

/// Bayt + sure butceli senkron prefetch kuyrugu.
///
/// Davranis sozlesmesi:
/// - enqueue() bekleyen kuyrugu degistirir, sayaclari sifirlar, butceyi
///   clamp'ler (0 = varsayilan, ust sinir = 128 MiB).
/// - pump() sirayla okur: boyut probu butceyi asarsa o asset KUYRUKTA KALIR
///   (ilerleme durur, sonraki pump'lar devam edebilir); sure dolarsa ERTELENIR
///   (kalan kuyruk korunur). Eksik asset sayaca islenir + tani uretilir,
///   kuyruk ilerlemeye devam eder.
/// - Boyut probu: once seek'lenebilir stream ile olculur (tam okuma yok);
///   prob desteklenmiyorsa tam okuma yapilip butce sonradan uygulanir
///   (okunan bayt cop'e atilir, asset kuyrukta kalir).
std::string prefetchProgressJson(std::size_t totalAssets, std::size_t readyAssets,
                                 std::size_t missingAssets, uint64_t readyBytes,
                                 uint64_t budgetBytes,
                                 const std::vector<std::string>& missingPaths,
                                 const std::string& lastDiagnostic);

class AssetPrefetch {
public:
    using SteadyNow = std::function<std::chrono::steady_clock::time_point()>;

    AssetPrefetch() = default;

    /// 0 -> varsayilan 32 MiB; ust sinir 128 MiB'a clamp'lenir.
    static uint64_t clampBudget(uint64_t requestedBytes) noexcept;

    /// Yeni pencereyi kuyruga alir (eski bekleyenler birakilir), sayaclar
    /// sifirlanir. Bos liste gecerlidir (tamamlanmis-bos ilerleme).
    void enqueue(std::vector<PrefetchAsset> assets, uint64_t budgetBytes);

    /// En fazla maxMilliseconds suresince okuma yapar. Donus: bu cagrida
    /// hazir hale gelen asset sayisi. vfs null ise 0 doner (fail-closed).
    /// maxMilliseconds <= 0 veya non-finite ise varsayilan 4 ms kullanilir;
    /// ust sinir 50 ms'a clamp'lenir.
    std::size_t pump(Rowl::VFS::VFSManager* vfs, double maxMilliseconds);

    /// Test kancasi: saat enjekte edilir (uretimde gercek steady_clock).
    void setNowForTests(SteadyNow now) { m_now = std::move(now); }

    std::size_t totalAssets() const noexcept { return m_total; }
    std::size_t readyAssets() const noexcept { return m_ready; }
    std::size_t missingAssets() const noexcept { return m_missing; }
    std::size_t queuedAssets() const noexcept { return m_queue.size(); }
    uint64_t readyBytes() const noexcept { return m_readyBytes; }
    uint64_t budgetBytes() const noexcept { return m_budget; }
    bool complete() const noexcept { return m_queue.empty(); }
    const std::vector<std::string>& missingPaths() const noexcept { return m_missingPaths; }
    const std::string& lastDiagnostic() const noexcept { return m_lastDiagnostic; }

    std::string progressJson() const;

private:
    std::vector<PrefetchAsset> m_queue;
    std::size_t m_total = 0;
    std::size_t m_ready = 0;
    std::size_t m_missing = 0;
    uint64_t m_readyBytes = 0;
    uint64_t m_budget = kPrefetchDefaultBudgetBytes;
    std::vector<std::string> m_missingPaths;
    std::string m_lastDiagnostic;
    SteadyNow m_now;
};

} // namespace Rowl::Core
