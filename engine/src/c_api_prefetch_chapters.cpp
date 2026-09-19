/**
 * c_api_prefetch_chapters.cpp
 *
 * Faz 5 Dilim 4 — butceli asset prefetch + chapter-sinirli yukleme C API
 * yuzeyi (ROWL_ENGINE_CAPABILITY_PREFETCH_CHAPTERS = 65536). Eklemeli; eski
 * giris noktalarina dokunulmaz. engine.cpp / window.cpp buyumez: tum kurallar
 * Rowl::Core::AssetPrefetch / ChapterLoader'dadir, burasi yalnizca ABI
 * siniridir (handle dogrulama + giris tasiyicisi + boyut-sorgu/cagiran-tamponu
 * + istisna yutma).
 *
 * Durum handle basina tutulur (bellekte; kalicilik yok):
 * - Chapter index + chapter dosyalariyla beslenen pencere (aktif +-1
 *   resident, uzak chapter unload, seffaf reload + tani).
 * - Aktif + sonraki sahne icin bayt/sure butceli senkron prefetch kuyrugu.
 *
 * Hepsi fail-closed: null-handle -> INVALID_HANDLE (0/""/0 tasiyicilarda),
 * bilinmeyen chapter -> INVALID_ARGUMENT, butce clamp (red degil), asiri
 * buyuk giris red.
 */

#include "c_api_internal.hpp"
#include "rowl/core/chapter_loader.hpp"
#include "rowl/core/prefetch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace {

/// C API giris tasiyicisi (byte), Dilim 1-3 deseni: 256 KiB + 1 cap.
constexpr std::size_t kPrefetchInputLimitBytes = 262144;
/// Chapter dosyasi JSON ust siniri: story parser limitiyle ayni (16 MiB).
constexpr std::size_t kChapterFileInputLimitBytes = 16 * 1024 * 1024;

struct PrefetchChapterRuntime {
    Rowl::Core::ChapterLoader loader;
    Rowl::Core::AssetPrefetch prefetch;
    std::string lastError;
};

std::mutex g_prefetchMutex;
std::unordered_map<RowlEngineHandle, PrefetchChapterRuntime> g_prefetchStates;

RowlEngine_ResultCode checkSizedInput(const char* input, std::string_view& out,
                                      std::size_t limitBytes) noexcept {
    if (input == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    const void* terminator = std::memchr(input, '\0', limitBytes + 1u);
    if (terminator == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    out = std::string_view(input, static_cast<const char*>(terminator) - input);
    return ROWL_RESULT_OK;
}

RowlEngine_ResultCode checkChapterId(const char* chapterId, std::string_view& out) noexcept {
    const RowlEngine_ResultCode inputCheck =
        checkSizedInput(chapterId, out, kPrefetchInputLimitBytes);
    if (inputCheck != ROWL_RESULT_OK) return inputCheck;
    if (out.empty() || out.size() > Rowl::Core::kMaxGraphIdChars ||
        out.find('\0') != std::string_view::npos) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return ROWL_RESULT_OK;
}

bool isParseError(const std::string& error) {
    return error.find("not valid JSON") != std::string::npos;
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_LoadChapterIndexJson(RowlEngineHandle handle,
                                                      const char* indexJsonUtf8) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view view;
    if (checkSizedInput(indexJsonUtf8, view, kPrefetchInputLimitBytes) != ROWL_RESULT_OK) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        std::string error;
        if (!runtime.loader.loadIndexJson(std::string(view), error)) {
            runtime.lastError = error;
            return isParseError(error) ? ROWL_RESULT_PARSE_ERROR
                                       : ROWL_RESULT_VALIDATION_ERROR;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_AppendChapterFileJson(RowlEngineHandle handle,
                                                       const char* chapterJsonUtf8) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view view;
    if (checkSizedInput(chapterJsonUtf8, view, kChapterFileInputLimitBytes) !=
        ROWL_RESULT_OK) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        std::string error;
        if (!runtime.loader.appendChapterFileJson(std::string(view), error)) {
            runtime.lastError = error;
            return isParseError(error) ? ROWL_RESULT_PARSE_ERROR
                                       : ROWL_RESULT_VALIDATION_ERROR;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_LoadChapter(RowlEngineHandle handle,
                                             const char* chapterIdUtf8) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view view;
    if (checkChapterId(chapterIdUtf8, view) != ROWL_RESULT_OK) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        std::string error;
        if (!runtime.loader.loadChapter(std::string(view), error)) {
            runtime.lastError = error;
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_UnloadChapter(RowlEngineHandle handle,
                                               const char* chapterIdUtf8) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view view;
    if (checkChapterId(chapterIdUtf8, view) != ROWL_RESULT_OK) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        std::string error;
        if (!runtime.loader.unloadChapter(std::string(view), error)) {
            runtime.lastError = error;
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLoadedChaptersJson(RowlEngineHandle handle, char* buffer,
                                                       uint32_t bufferSize,
                                                       uint32_t* outRequiredSize) {
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        return copyUtf8ToCaller(runtime.loader.loadedChaptersJson(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_IsChapterBoundaryNode(RowlEngineHandle handle, uint64_t nodeId) {
    if (nodeId == 0) return 0;
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return 0;
    }
    return invokeNoexcept<int>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        if (runtime.loader.hasChapters() || runtime.loader.isLegacySingleGraph()) {
            return runtime.loader.isChapterBoundaryNode(nodeId) ? 1 : 0;
        }
        // Loader henuz beslenmemis: motorun aktif grafigine bak (salt-okunur).
        auto* engine = toEngineChecked(handle);
        if (engine == nullptr) return 0;
        // D1 (#131): mountsuz VFS'te okuma hayalet-missing üretirdi.
        if (!requireEngineInitialized(engine, "is_chapter_boundary")) return 0;
        return Rowl::Core::documentChapterBoundary(engine->getStoryGraphDocument(), nodeId)
                   ? 1
                   : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_PrefetchChapterAssets(RowlEngineHandle handle,
                                                       const char* chapterIdUtf8,
                                                       uint64_t budgetBytes) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_prefetchMutex);
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    // Bos/null chapterId = aktif chapter (loader aktif, yoksa motorun aktif
    // chapter'i). Dolu chapterId 128 karakter siniriyla dogrulanir.
    std::string requested;
    if (chapterIdUtf8 != nullptr && *chapterIdUtf8 != '\0') {
        std::string_view view;
        if (checkChapterId(chapterIdUtf8, view) != ROWL_RESULT_OK) {
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        requested.assign(view.data(), view.size());
    }
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        auto* engine = toEngineChecked(handle);
        if (engine == nullptr) return ROWL_RESULT_INVALID_HANDLE;
        // D1 (#131): mountsuz VFS'te pump tüm kuyruğu missing işaretleyip
        // OK dönüyordu (OK yalanı). Fail-closed: STATE_ERROR.
        if (!requireEngineInitialized(engine, "prefetch_chapter_assets")) {
            return ROWL_RESULT_STATE_ERROR;
        }

        std::vector<Rowl::Core::PrefetchAsset> assets;
        if (runtime.loader.hasChapters()) {
            const std::string anchor =
                requested.empty() ? runtime.loader.activeChapterId() : requested;
            if (anchor.empty()) {
                runtime.lastError = "no active chapter to prefetch; rejected";
                return ROWL_RESULT_INVALID_ARGUMENT;
            }
            const std::vector<std::string>& order = runtime.loader.orderedChapters();
            const auto pos = std::find(order.begin(), order.end(), anchor);
            if (pos == order.end()) {
                runtime.lastError = "unknown chapter '" + anchor + "'; rejected";
                return ROWL_RESULT_INVALID_ARGUMENT;
            }
            // Pencere: istenen chapter + sonraki chapter (aktif + sonraki sahne).
            std::vector<std::string> window{anchor};
            if (pos + 1 != order.end()) window.push_back(*(pos + 1));
            std::unordered_map<std::string, bool> seen;
            for (const std::string& chapterId : window) {
                for (uint64_t nodeId : runtime.loader.chapterNodeIds(chapterId)) {
                    const Rowl::Core::StoryNode* node = runtime.loader.node(nodeId);
                    if (node == nullptr) continue;
                    for (Rowl::Core::PrefetchAsset& asset :
                         Rowl::Core::collectNodeAssets(*node)) {
                        if (!seen.emplace(asset.path, true).second) continue;
                        assets.push_back(std::move(asset));
                    }
                }
            }
        } else {
            const Rowl::Core::StoryGraphDocument& document =
                engine->getStoryGraphDocument();
            if (document.nodes.empty()) {
                runtime.lastError = "no story graph loaded; rejected";
                return ROWL_RESULT_INVALID_ARGUMENT;
            }
            const std::string anchor =
                requested.empty() ? engine->getCurrentChapterId() : requested;
            if (anchor.empty()) {
                // Legacy tek-dosya: aktif node + successor'lari (aktif + sonraki sahne).
                const uint64_t current = engine->getCurrentNodeId();
                std::vector<uint64_t> scope{current};
                const auto currentIt = document.nodes.find(current);
                if (currentIt == document.nodes.end()) {
                    runtime.lastError = "current node is not in the graph; rejected";
                    return ROWL_RESULT_INVALID_ARGUMENT;
                }
                for (const auto& next : currentIt->second.nextNodes) {
                    scope.push_back(next.nodeId);
                }
                assets = Rowl::Core::collectDocumentAssets(document, scope);
            } else {
                const std::vector<std::string> order =
                    Rowl::Core::orderedChapterIds(document);
                const auto pos = std::find(order.begin(), order.end(), anchor);
                if (pos == order.end()) {
                    runtime.lastError = "unknown chapter '" + anchor + "'; rejected";
                    return ROWL_RESULT_INVALID_ARGUMENT;
                }
                std::vector<uint64_t> scope;
                for (const auto& [id, node] : document.nodes) {
                    if (node.chapterId == anchor) scope.push_back(id);
                }
                if (pos + 1 != order.end()) {
                    for (const auto& [id, node] : document.nodes) {
                        if (node.chapterId == *(pos + 1)) scope.push_back(id);
                    }
                }
                std::sort(scope.begin(), scope.end());
                assets = Rowl::Core::collectDocumentAssets(document, scope);
            }
        }

        runtime.prefetch.enqueue(std::move(assets), budgetBytes);
        runtime.lastError.clear();
        // Senkron pump: tetikleme tek 4 ms'lik dilimi hemen calistirir; kalani
        // RowlEngine_PumpPrefetch ile update-thread'den pompalanir.
        runtime.prefetch.pump(engine->getVfs(),
                              Rowl::Core::kPrefetchDefaultPumpMilliseconds);
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_PumpPrefetch(RowlEngineHandle handle, float maxMilliseconds) {
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return 0;
    }
    return invokeNoexcept<int>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        auto* engine = toEngineChecked(handle);
        if (engine == nullptr) return 0;
        // D1 (#131): pump guard'ı — PrefetchChapterAssets ile aynı delik.
        if (!requireEngineInitialized(engine, "pump_prefetch")) return 0;
        const std::size_t pumped =
            runtime.prefetch.pump(engine->getVfs(), static_cast<double>(maxMilliseconds));
        return pumped > static_cast<std::size_t>((std::numeric_limits<int>::max)())
                   ? (std::numeric_limits<int>::max)()
                   : static_cast<int>(pumped);
    }, 0);
}

RowlEngine_ResultCode RowlEngine_GetPrefetchProgressJson(RowlEngineHandle handle, char* buffer,
                                                         uint32_t bufferSize,
                                                         uint32_t* outRequiredSize) {
    std::lock_guard<std::mutex> lock(g_prefetchMutex);
    if (!isLiveHandle(handle)) {
        g_prefetchStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        PrefetchChapterRuntime& runtime = g_prefetchStates[handle];
        return copyUtf8ToCaller(runtime.prefetch.progressJson(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
