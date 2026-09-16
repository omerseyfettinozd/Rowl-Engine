/**
 * prefetch.cpp — Faz 5 Dilim 4 butceli asset prefetch uygulamasi.
 *
 * Yeni dosya: engine.cpp / window.cpp / story imzalarina dokunulmaz.
 * Decode calistirmaz; yalnizca VFS okumasi yapar (hazir = baytlar okundu).
 */

#include "rowl/core/prefetch.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "rowl/vfs/vfs.hpp"

namespace Rowl::Core {

namespace {

void addAsset(std::vector<PrefetchAsset>& out, std::unordered_set<std::string>& seen,
              const nlohmann::json& data, const char* key, std::string kind,
              uint64_t nodeId, const std::string& chapterId) {
    if (!data.is_object()) return;
    const auto it = data.find(key);
    if (it == data.end() || !it->is_string()) return;
    const std::string path = it->get<std::string>();
    if (path.empty() || path.find('\0') != std::string::npos) return;
    if (!seen.insert(path).second) return;
    out.push_back(PrefetchAsset{path, std::move(kind), nodeId, chapterId});
}

/// Character slot turleri (Dilim 3 sabit sira): body < face < outfit < accessory.
const char* slotKind(const std::string& slot) noexcept {
    if (slot == "body") return "character-body";
    if (slot == "face") return "character-face";
    if (slot == "outfit") return "character-outfit";
    if (slot == "accessory") return "character-accessory";
    return nullptr;
}

void addLayerAssets(std::vector<PrefetchAsset>& out, std::unordered_set<std::string>& seen,
                    const nlohmann::json& data, uint64_t nodeId,
                    const std::string& chapterId) {
    if (!data.is_object()) return;
    const auto layersIt = data.find("layers");
    if (layersIt == data.end() || !layersIt->is_object()) return;
    for (const auto& [slot, value] : layersIt->items()) {
        const char* kind = slotKind(slot);
        if (kind == nullptr) continue;
        std::string asset;
        if (value.is_string()) {
            asset = value.get<std::string>();
        } else if (value.is_object()) {
            const auto assetIt = value.find("asset");
            if (assetIt != value.end() && assetIt->is_string()) {
                asset = assetIt->get<std::string>();
            }
        }
        if (asset.empty() || asset.find('\0') != std::string::npos) continue;
        if (!seen.insert(asset).second) continue;
        out.push_back(PrefetchAsset{asset, kind, nodeId, chapterId});
    }
}

} // namespace

std::vector<PrefetchAsset> collectNodeAssets(const StoryNode& node) {
    std::vector<PrefetchAsset> out;
    std::unordered_set<std::string> seen;

    auto pushTop = [&](const std::string& path, const std::string& kind) {
        if (path.empty() || path.find('\0') != std::string::npos) return;
        if (!seen.insert(path).second) return;
        out.push_back(PrefetchAsset{path, kind, node.id, node.chapterId});
    };

    // Legacy top-level alanlar.
    pushTop(node.background, "image");
    pushTop(node.character, "image");

    for (const ComponentData& component : node.components) {
        if (!component.data.is_object()) continue;
        const auto& data = component.data;
        const std::string& type = component.type;
        if (type == "background") {
            addAsset(out, seen, data, "texture", "image", node.id, node.chapterId);
        } else if (type == "character") {
            // Legacy tek-sprite body slotuna duser (Dilim 3 migration kurali).
            addAsset(out, seen, data, "sprite", "character-body", node.id, node.chapterId);
            addLayerAssets(out, seen, data, node.id, node.chapterId);
            addAsset(out, seen, data, "voice_blip_sound", "audio-voice", node.id,
                     node.chapterId);
            addAsset(out, seen, data, "typewriter_sound", "audio-voice", node.id,
                     node.chapterId);
        } else if (type == "dialogue") {
            addAsset(out, seen, data, "custom_box_texture", "image", node.id,
                     node.chapterId);
            addAsset(out, seen, data, "typewriter_sound", "audio-voice", node.id,
                     node.chapterId);
            addAsset(out, seen, data, "voice_blip_sound", "audio-voice", node.id,
                     node.chapterId);
        } else if (type == "audio") {
            addAsset(out, seen, data, "bgm_track", "audio-bgm", node.id, node.chapterId);
            addAsset(out, seen, data, "sfx_track", "audio-sfx", node.id, node.chapterId);
            addAsset(out, seen, data, "voice_track", "audio-voice", node.id,
                     node.chapterId);
            addAsset(out, seen, data, "ambience_track", "audio-ambience", node.id,
                     node.chapterId);
        } else if (type == "choice") {
            const auto optionsIt = data.find("options");
            if (optionsIt == data.end() || !optionsIt->is_array()) continue;
            for (const auto& option : *optionsIt) {
                if (!option.is_object()) continue;
                addAsset(out, seen, option, "normal_image", "image", node.id,
                         node.chapterId);
                addAsset(out, seen, option, "background_image", "image", node.id,
                         node.chapterId);
            }
        }
    }
    return out;
}

std::vector<PrefetchAsset> collectDocumentAssets(const StoryGraphDocument& document,
                                                 const std::vector<uint64_t>& nodeIds) {
    std::vector<PrefetchAsset> out;
    std::unordered_set<std::string> seen;
    for (uint64_t nodeId : nodeIds) {
        const auto it = document.nodes.find(nodeId);
        if (it == document.nodes.end()) continue;
        for (PrefetchAsset& asset : collectNodeAssets(it->second)) {
            if (!seen.insert(asset.path).second) continue;
            out.push_back(std::move(asset));
        }
    }
    return out;
}

std::vector<std::string> orderedChapterIds(const StoryGraphDocument& document) {
    std::vector<const GraphChapter*> sorted;
    sorted.reserve(document.chapters.size());
    for (const GraphChapter& chapter : document.chapters) sorted.push_back(&chapter);
    std::sort(sorted.begin(), sorted.end(), [](const GraphChapter* a, const GraphChapter* b) {
        if (a->order != b->order) return a->order < b->order;
        return a->id < b->id;
    });
    std::vector<std::string> ids;
    ids.reserve(sorted.size());
    for (const GraphChapter* chapter : sorted) ids.push_back(chapter->id);
    return ids;
}

std::string prefetchProgressJson(std::size_t totalAssets, std::size_t readyAssets,
                                 std::size_t missingAssets, uint64_t readyBytes,
                                 uint64_t budgetBytes,
                                 const std::vector<std::string>& missingPaths,
                                 const std::string& lastDiagnostic) {
    nlohmann::json doc;
    doc["total_assets"] = totalAssets;
    doc["ready_assets"] = readyAssets;
    doc["missing_assets"] = missingAssets;
    doc["queued_assets"] =
        totalAssets >= readyAssets + missingAssets ? totalAssets - readyAssets - missingAssets : 0;
    doc["ready_bytes"] = readyBytes;
    doc["budget_bytes"] = budgetBytes;
    doc["complete"] = (readyAssets + missingAssets >= totalAssets);
    nlohmann::json missing = nlohmann::json::array();
    for (std::size_t i = 0; i < missingPaths.size() && i < 64; ++i) {
        missing.push_back(missingPaths[i]);
    }
    doc["missing_paths"] = std::move(missing);
    doc["last_diagnostic"] = lastDiagnostic;
    return doc.dump();
}

uint64_t AssetPrefetch::clampBudget(uint64_t requestedBytes) noexcept {
    if (requestedBytes == 0) return kPrefetchDefaultBudgetBytes;
    if (requestedBytes > kPrefetchMaxBudgetBytes) return kPrefetchMaxBudgetBytes;
    return requestedBytes;
}

void AssetPrefetch::enqueue(std::vector<PrefetchAsset> assets, uint64_t budgetBytes) {
    m_queue = std::move(assets);
    m_total = m_queue.size();
    m_ready = 0;
    m_missing = 0;
    m_readyBytes = 0;
    m_budget = clampBudget(budgetBytes);
    m_missingPaths.clear();
    m_lastDiagnostic.clear();
}

namespace {

/// Seek probu: tam okuma yapmadan bayt buyuklugu. Desteklenmiyorsa nullopt.
std::optional<uint64_t> probeSize(Rowl::VFS::VFSManager* vfs, const std::string& path) {
    try {
        std::unique_ptr<std::istream> stream = vfs->openReadStream(path);
        if (!stream || !(*stream)) return std::nullopt;
        stream->seekg(0, std::ios::end);
        if (!(*stream)) return std::nullopt;
        const auto end = stream->tellg();
        if (end < 0) return std::nullopt;
        return static_cast<uint64_t>(end);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

std::size_t AssetPrefetch::pump(Rowl::VFS::VFSManager* vfs, double maxMilliseconds) {
    if (vfs == nullptr) return 0;
    double budget = maxMilliseconds;
    if (!std::isfinite(budget) || budget <= 0.0) budget = kPrefetchDefaultPumpMilliseconds;
    if (budget > kPrefetchMaxPumpMilliseconds) budget = kPrefetchMaxPumpMilliseconds;

    const auto now = m_now ? m_now : [] { return std::chrono::steady_clock::now(); };
    const auto deadline =
        now() + std::chrono::duration<double, std::milli>(budget);

    std::size_t newlyReady = 0;
    while (!m_queue.empty()) {
        if (now() >= deadline) {
            m_lastDiagnostic = "prefetch deferred: time budget exhausted with " +
                               std::to_string(m_queue.size()) + " assets queued";
            break;
        }
        const PrefetchAsset current = m_queue.front();

        bool missing = false;
        uint64_t assetBytes = 0;
        try {
            if (!vfs->exists(current.path)) {
                missing = true;
            } else if (const auto probed = probeSize(vfs, current.path)) {
                if (m_readyBytes + *probed > m_budget) {
                    m_lastDiagnostic = "prefetch paused: byte budget exhausted at '" +
                                       current.path + "' (" + std::to_string(m_readyBytes) +
                                       "/" + std::to_string(m_budget) + " bytes)";
                    break; // Butce-disi asset kuyrukta kalir.
                }
                const std::vector<uint8_t> bytes = vfs->readBytes(current.path);
                assetBytes = static_cast<uint64_t>(bytes.size());
                if (m_readyBytes + assetBytes > m_budget) {
                    m_lastDiagnostic = "prefetch paused: byte budget exhausted at '" +
                                       current.path + "' (" + std::to_string(m_readyBytes) +
                                       "/" + std::to_string(m_budget) + " bytes)";
                    break; // Okunan bayt cop'e atilir, asset kuyrukta kalir.
                }
            } else {
                // Prob desteklenmiyor: tam oku, butceyi sonradan uygula.
                const std::vector<uint8_t> bytes = vfs->readBytes(current.path);
                if (bytes.empty() && !vfs->exists(current.path)) {
                    missing = true;
                } else {
                    assetBytes = static_cast<uint64_t>(bytes.size());
                    if (m_readyBytes + assetBytes > m_budget) {
                        m_lastDiagnostic =
                            "prefetch paused: byte budget exhausted at '" + current.path +
                            "' (" + std::to_string(m_readyBytes) + "/" +
                            std::to_string(m_budget) + " bytes)";
                        break;
                    }
                }
            }
        } catch (...) {
            missing = true;
        }

        m_queue.erase(m_queue.begin());
        if (missing) {
            ++m_missing;
            if (m_missingPaths.size() < 64) m_missingPaths.push_back(current.path);
            m_lastDiagnostic =
                "prefetch missing asset '" + current.path + "' (node " +
                std::to_string(current.nodeId) + "); queue continues";
            continue; // Eksik asset prefetch'i durdurmaz.
        }
        m_readyBytes += assetBytes;
        ++m_ready;
        ++newlyReady;
    }
    return newlyReady;
}

std::string AssetPrefetch::progressJson() const {
    return prefetchProgressJson(m_total, m_ready, m_missing, m_readyBytes, m_budget,
                                m_missingPaths, m_lastDiagnostic);
}

} // namespace Rowl::Core
