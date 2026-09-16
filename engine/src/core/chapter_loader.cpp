/**
 * chapter_loader.cpp — Faz 5 Dilim 4 chapter-sinirli yukleme uygulamasi.
 *
 * Yeni dosya: engine.cpp / window.cpp / story imzalarina dokunulmaz.
 * Node payload'lari StoryGraphParser'dan gecer (limitler aynen).
 */

#include "rowl/core/chapter_loader.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "rowl/core/story_graph_parser.hpp"

namespace Rowl::Core {

namespace {

constexpr const char* kImplicitDefaultChapter = "default";

struct ChapterLoaderMeta {
    std::string id;
    std::string title;
    int order = 0;
    uint64_t startNodeId = 0;
    bool hasStartNodeId = false;
};

bool readChapterMeta(const nlohmann::json& entry, ChapterLoaderMeta& out, std::string& error);

bool readChapterMeta(const nlohmann::json& entry, ChapterLoaderMeta& out, std::string& error) {
    if (!entry.is_object()) {
        error = "chapter entry must be an object; rejected";
        return false;
    }
    const auto idIt = entry.find("id");
    if (idIt == entry.end() || !idIt->is_string() || idIt->get<std::string>().empty()) {
        error = "chapter entry without a non-empty string id; rejected";
        return false;
    }
    out.id = idIt->get<std::string>();
    if (out.id.size() > kMaxGraphIdChars) {
        error = "chapter id exceeds its length limit; rejected";
        return false;
    }
    const auto orderIt = entry.find("order");
    if (orderIt != entry.end()) {
        if (!orderIt->is_number_integer()) {
            error = "chapter order must be an integer; rejected";
            return false;
        }
        out.order = static_cast<int>(orderIt->get<int64_t>());
    }
    const auto titleIt = entry.find("title");
    if (titleIt != entry.end() && titleIt->is_string()) out.title = titleIt->get<std::string>();
    const auto startIt = entry.find("start_node_id");
    if (startIt != entry.end()) {
        if (!startIt->is_number_unsigned() || startIt->get<uint64_t>() == 0) {
            error = "chapter start_node_id must be a positive node id; rejected";
            return false;
        }
        out.startNodeId = startIt->get<uint64_t>();
        out.hasStartNodeId = true;
    }
    return true;
}

/// Chapter dosyasi node'larini StoryGraphParser limitleriyle dogrula.
/// Donus: parse edilmis node listesi (sirali). Hata halinde outError dolar.
///
/// Editor Merge davranisiyla birebir: node kimlikleri globaldir, chapter'lar
/// arasi kenarlar yasaktir. Bu yuzden kenar hedefleri dosya-ici cozulemez;
/// kenarlar soyulmus payload parser'dan gecer (alan esleme + bilesen + limit
/// dogrulamasi), kenar sekli ayrica dogrulanip geri takilir. Hedef varligi
/// global oldugundan append sirasinda denetlenmez (editor Merge de
/// denetlemez; tam-belge yolu loadFullGraphJson katidir).
bool parseChapterNodes(const nlohmann::json& nodes, const std::string& fileChapter,
                       std::vector<StoryNode>& out, std::string& outError) {
    out.clear();
    if (!nodes.is_array()) {
        outError = "chapter file nodes must be an array; rejected";
        return false;
    }
    if (nodes.empty()) return true;
    // Kenarlari soy: hedef baska chapter'da olabilir.
    nlohmann::json stripped = nlohmann::json::array();
    std::vector<std::vector<StoryNode::NextNode>> edges;
    edges.reserve(nodes.size());
    for (const auto& nodeJson : nodes) {
        if (!nodeJson.is_object()) {
            outError = "chapter file contains a non-object node; rejected";
            return false;
        }
        nlohmann::json copy = nodeJson;
        std::vector<StoryNode::NextNode> nodeEdges;
        const auto nextNodesIt = copy.find("next_nodes");
        if (nextNodesIt != copy.end()) {
            if (!nextNodesIt->is_array()) {
                outError = "chapter file node next_nodes must be an array; rejected";
                return false;
            }
            for (const auto& edgeJson : *nextNodesIt) {
                if (!edgeJson.is_object()) {
                    outError = "chapter file contains a non-object edge; rejected";
                    return false;
                }
                const auto idIt = edgeJson.find("id");
                if (idIt == edgeJson.end() || !idIt->is_number_unsigned() ||
                    idIt->get<uint64_t>() == 0) {
                    outError = "chapter file contains an edge with no target node ID; rejected";
                    return false;
                }
                StoryNode::NextNode edge;
                edge.nodeId = idIt->get<uint64_t>();
                const auto labelIt = edgeJson.find("label");
                if (labelIt != edgeJson.end()) {
                    if (!labelIt->is_string()) {
                        outError = "chapter file edge label must be a string; rejected";
                        return false;
                    }
                    edge.label = labelIt->get<std::string>();
                }
                const auto optionIt = edgeJson.find("option_id");
                if (optionIt != edgeJson.end()) {
                    if (!optionIt->is_string()) {
                        outError = "chapter file edge option_id must be a string; rejected";
                        return false;
                    }
                    edge.optionId = optionIt->get<std::string>();
                }
                nodeEdges.push_back(std::move(edge));
            }
            if (nodeEdges.size() > kMaxEdgesPerStoryNode) {
                outError = "chapter file node exceeds the maximum edge count; rejected";
                return false;
            }
        } else {
            const auto nextIdIt = copy.find("next_id");
            if (nextIdIt != copy.end()) {
                if (!nextIdIt->is_number_unsigned()) {
                    outError = "chapter file node next_id must be a node id; rejected";
                    return false;
                }
                const uint64_t nextId = nextIdIt->get<uint64_t>();
                if (nextId != 0) nodeEdges.push_back({nextId, "", ""});
            }
        }
        copy.erase("next_nodes");
        copy.erase("next_id");
        stripped.push_back(std::move(copy));
        edges.push_back(std::move(nodeEdges));
    }
    // StoryGraphParser tek belge bekler: ilk node start secilerek sarilir.
    // Birinci node'un id'si sayisal degilse parser zaten reddeder.
    uint64_t firstId = 0;
    for (const auto& nodeJson : stripped) {
        if (nodeJson.is_object()) {
            const auto idIt = nodeJson.find("id");
            if (idIt != nodeJson.end() && idIt->is_number_unsigned()) {
                firstId = idIt->get<uint64_t>();
                break;
            }
        }
    }
    if (firstId == 0) {
        outError = "chapter file has no node with a positive numeric id; rejected";
        return false;
    }
    nlohmann::json wrapper;
    wrapper["format_version"] = 5;
    wrapper["start_node_id"] = firstId;
    wrapper["nodes"] = stripped;
    // Sarmalayici belge dosyanin chapter'ini deklare eder; aksi halde
    // validateGraphStructure chapter_id tasiyan node'lari yetim sayar.
    wrapper["chapters"] = nlohmann::json::array();
    wrapper["chapters"].push_back({{"id", fileChapter}});
    StoryGraphParseResult result = StoryGraphParser::parse(wrapper.dump());
    if (!result.succeeded()) {
        outError = "chapter file nodes invalid: " + result.message;
        return false;
    }
    out.reserve(result.document.nodes.size());
    // Belge sirasi korunur: giris dizisindeki id sirasiyla aktarilir.
    std::size_t position = 0;
    for (const auto& nodeJson : stripped) {
        const uint64_t nodeId = nodeJson.value("id", static_cast<uint64_t>(0));
        const auto it = result.document.nodes.find(nodeId);
        if (it == result.document.nodes.end()) {
            outError = "chapter file node vanished during parse; rejected";
            return false;
        }
        if (!it->second.chapterId.empty() && it->second.chapterId != fileChapter) {
            outError = "chapter file node belongs to chapter '" + it->second.chapterId +
                       "', expected '" + fileChapter + "'; rejected";
            return false;
        }
        StoryNode node = it->second;
        node.nextNodes = edges[position++];
        out.push_back(std::move(node));
    }
    return true;
}

} // namespace

void ChapterLoader::rebuildOrder() {
    m_order.clear();
    m_order.reserve(m_chapters.size());
    for (const auto& [key, meta] : m_chapters) {
        (void)key;
        m_order.push_back(meta.id);
    }
}

void ChapterLoader::applyWindow() {
    std::unordered_map<std::string, bool> wanted;
    if (!m_active.empty()) {
        const auto pos = std::find(m_order.begin(), m_order.end(), m_active);
        if (pos != m_order.end()) {
            const std::size_t index = static_cast<std::size_t>(pos - m_order.begin());
            const std::size_t lo =
                index > kChapterNeighborDistance ? index - kChapterNeighborDistance : 0;
            const std::size_t hi =
                std::min(m_order.size() - 1, index + kChapterNeighborDistance);
            for (std::size_t i = lo; i <= hi; ++i) wanted[m_order[i]] = true;
        }
    }
    m_resident.clear();
    m_residentChapters.clear();
    for (const auto& [chapterId, _] : wanted) {
        (void)_;
        const auto bucket = m_buckets.find(chapterId);
        if (bucket == m_buckets.end()) continue;
        for (const StoryNode& node : bucket->second) {
            m_resident.emplace(node.id, node);
        }
        m_residentChapters[chapterId] = true;
    }
}

bool ChapterLoader::bucketOf(uint64_t nodeId, std::string& out) const {
    const auto it = m_nodeBucket.find(nodeId);
    if (it == m_nodeBucket.end()) return false;
    out = it->second;
    return true;
}

void ChapterLoader::recordDiagnostic(const std::string& message) {
    m_lastDiagnostic = message;
    m_diagnostics.push_back(message);
    while (m_diagnostics.size() > kMaxChapterDiagnostics) m_diagnostics.pop_front();
}

bool ChapterLoader::loadFullGraphJson(const std::string& json, std::string& error) {
    StoryGraphParseResult result = StoryGraphParser::parse(json);
    if (!result.succeeded()) {
        error = "story graph invalid: " + result.message;
        return false;
    }
    // Tanzim oncesi gecici durumda kur: red halinde onceki durum korunur.
    ChapterLoader staged;
    if (result.document.chapters.empty()) {
        staged.m_legacySingleGraph = true;
        staged.m_startNodeId = result.document.startNodeId;
        staged.m_hasStartNode = true;
        for (const auto& [id, node] : result.document.nodes) {
            staged.m_resident.emplace(id, node);
        }
        staged.m_totalNodes = staged.m_resident.size();
    } else {
        for (const GraphChapter& chapter : result.document.chapters) {
            ChapterLoaderMeta meta;
            meta.id = chapter.id;
            meta.title = chapter.title;
            meta.order = chapter.order;
            meta.startNodeId = chapter.startNodeId;
            meta.hasStartNodeId = chapter.hasStartNodeId;
            staged.m_chapters[{meta.order, meta.id}] = {meta.id, meta.title, meta.order,
                                                        meta.startNodeId, meta.hasStartNodeId};
        }
        staged.rebuildOrder();
        if (staged.m_order.size() > kMaxGraphChapters) {
            error = "story graph exceeds the chapter limit; rejected";
            return false;
        }
        staged.m_startNodeId = result.document.startNodeId;
        staged.m_hasStartNode = true;
        for (const auto& [id, node] : result.document.nodes) {
            const std::string bucket =
                node.chapterId.empty() ? kImplicitDefaultChapter : node.chapterId;
            const bool known = std::any_of(
                staged.m_chapters.begin(), staged.m_chapters.end(),
                [&](const auto& entry) { return entry.second.id == bucket; });
            if (!known) {
                if (bucket != kImplicitDefaultChapter) {
                    // Yetim chapter: editor linter'i bunu error sayar; native
                    // fail-closed reddeder (sessiz kabul yasak).
                    error = "story graph node references unknown chapter '" + bucket +
                            "'; rejected";
                    return false;
                }
                // Editor EffectiveChapters davranisi: atanmamis node'lar
                // implicit "default" chapter'a duser (order -1, en basta).
                staged.m_chapters[{-1, kImplicitDefaultChapter}] = {
                    kImplicitDefaultChapter, "Ana Bölüm", -1, 0, false};
                staged.rebuildOrder();
            }
            staged.m_buckets[bucket].push_back(node);
            staged.m_nodeBucket[id] = bucket;
        }
        // "default" kovasi index'te yoksa ama yetim olmayan bos-chapterId'li
        // node varsa diye: yukaridaki kontrol zaten reddeder; buraya dusen
        // default'lar tanimli chapter'dir.
        staged.m_totalNodes = result.document.nodes.size();
        // Aktif: start node'un chapter'i, yoksa sirali ilk chapter.
        std::string active = staged.m_order.front();
        const auto startIt = result.document.nodes.find(result.document.startNodeId);
        if (startIt != result.document.nodes.end() && !startIt->second.chapterId.empty()) {
            active = startIt->second.chapterId;
        } else if (startIt != result.document.nodes.end() && startIt->second.chapterId.empty()) {
            active = kImplicitDefaultChapter;
        }
        staged.m_active = active;
        staged.applyWindow();
    }
    *this = std::move(staged);
    m_lastDiagnostic.clear();
    m_diagnostics.clear();
    return true;
}

bool ChapterLoader::loadIndexJson(const std::string& json, std::string& error) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        error = std::string("chapter index is not valid JSON: ") + e.what();
        return false;
    }
    if (!doc.is_object()) {
        error = "chapter index must be an object; rejected";
        return false;
    }
    const auto chaptersIt = doc.find("chapters");
    if (chaptersIt == doc.end() || !chaptersIt->is_array()) {
        error = "chapter index without a chapters array; rejected";
        return false;
    }
    if (chaptersIt->size() > kMaxGraphChapters) {
        error = "chapter index exceeds the chapter limit; rejected";
        return false;
    }
    ChapterLoader staged;
    for (const auto& entry : *chaptersIt) {
        ChapterLoaderMeta meta;
        if (!readChapterMeta(entry, meta, error)) return false;
        if (staged.m_chapters.end() !=
            std::find_if(staged.m_chapters.begin(), staged.m_chapters.end(),
                         [&](const auto& existing) { return existing.second.id == meta.id; })) {
            error = "chapter index with duplicate chapter id '" + meta.id + "'; rejected";
            return false;
        }
        staged.m_chapters[{meta.order, meta.id}] = {meta.id, meta.title, meta.order,
                                                    meta.startNodeId, meta.hasStartNodeId};
    }
    const auto startIt = doc.find("start_node_id");
    if (startIt != doc.end()) {
        if (!startIt->is_number_unsigned() || startIt->get<uint64_t>() == 0) {
            error = "chapter index start_node_id must be a positive node id; rejected";
            return false;
        }
        staged.m_startNodeId = startIt->get<uint64_t>();
        staged.m_hasStartNode = true;
    }
    staged.rebuildOrder();
    if (!staged.m_order.empty()) staged.m_active = staged.m_order.front();
    *this = std::move(staged);
    m_lastDiagnostic.clear();
    m_diagnostics.clear();
    return true;
}

bool ChapterLoader::appendChapterFileJson(const std::string& json, std::string& error) {
    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& e) {
        error = std::string("chapter file is not valid JSON: ") + e.what();
        return false;
    }
    if (!doc.is_object()) {
        error = "chapter file must be an object; rejected";
        return false;
    }
    const auto idIt = doc.find("chapter_id");
    if (idIt == doc.end() || !idIt->is_string() || idIt->get<std::string>().empty()) {
        error = "chapter file without chapter_id; rejected";
        return false;
    }
    const std::string fileChapter = idIt->get<std::string>();
    if (fileChapter.size() > kMaxGraphIdChars) {
        error = "chapter file chapter_id exceeds its length limit; rejected";
        return false;
    }
    const auto nodesIt = doc.find("nodes");
    if (nodesIt == doc.end()) {
        error = "chapter file without a nodes array; rejected";
        return false;
    }
    std::vector<StoryNode> parsed;
    if (!parseChapterNodes(*nodesIt, fileChapter, parsed, error)) return false;

    // Duplicate kontrolu (atomik: once dogrula, sonra isle).
    for (const StoryNode& node : parsed) {
        if (m_nodeBucket.find(node.id) != m_nodeBucket.end()) {
            error = "duplicate node id " + std::to_string(node.id) +
                    " across chapter files; rejected";
            return false;
        }
    }
    const bool known = std::any_of(
        m_chapters.begin(), m_chapters.end(),
        [&](const auto& entry) { return entry.second.id == fileChapter; });
    if (!known) {
        // Editor Merge toleransi: el-ile eklenmis chapter dosyasi dusmez,
        // deterministik sirayla kaydedilir.
        if (m_chapters.size() >= kMaxGraphChapters) {
            error = "chapter file would exceed the chapter limit; rejected";
            return false;
        }
        int maxOrder = 0;
        for (const auto& [key, meta] : m_chapters) {
            (void)key;
            maxOrder = std::max(maxOrder, meta.order);
        }
        const int order = m_chapters.empty() ? 0 : maxOrder + 1;
        m_chapters[{order, fileChapter}] = {fileChapter, fileChapter, order, 0, false};
        rebuildOrder();
    }
    const std::string bucket = fileChapter;
    auto& stored = m_buckets[bucket];
    for (const StoryNode& node : parsed) {
        stored.push_back(node);
        m_nodeBucket[node.id] = bucket;
    }
    m_totalNodes += parsed.size();
    if (m_active.empty() && !m_order.empty()) m_active = m_order.front();
    // Pencere disi chapter dosyasi resident'e alinmaz; pencere ici ise
    // artimli birlesir (tam applyWindow yikimi seffaf-reload'lari silerdi).
    const auto pos = std::find(m_order.begin(), m_order.end(), m_active);
    const auto mine = std::find(m_order.begin(), m_order.end(), bucket);
    if (!m_active.empty() && pos != m_order.end() && mine != m_order.end()) {
        const auto distance =
            pos > mine ? static_cast<std::size_t>(pos - mine) : static_cast<std::size_t>(mine - pos);
        if (distance <= kChapterNeighborDistance) {
            for (const StoryNode& node : parsed) m_resident.emplace(node.id, node);
            m_residentChapters[bucket] = true;
        }
    } else if (m_active.empty()) {
        for (const StoryNode& node : parsed) m_resident.emplace(node.id, node);
        m_residentChapters[bucket] = true;
    }
    return true;
}

bool ChapterLoader::setActiveChapter(const std::string& chapterId, std::string& error) {
    const bool known = std::any_of(
        m_chapters.begin(), m_chapters.end(),
        [&](const auto& entry) { return entry.second.id == chapterId; });
    if (!known) {
        error = "unknown chapter '" + chapterId + "'; rejected";
        return false;
    }
    m_active = chapterId;
    applyWindow();
    std::string loaded;
    for (const auto& [id, _] : m_residentChapters) {
        (void)_;
        if (!loaded.empty()) loaded += ",";
        loaded += id;
    }
    recordDiagnostic("active chapter '" + chapterId + "' (resident: " + loaded + ")");
    return true;
}

bool ChapterLoader::loadChapter(const std::string& chapterId, std::string& error) {
    const bool known = std::any_of(
        m_chapters.begin(), m_chapters.end(),
        [&](const auto& entry) { return entry.second.id == chapterId; });
    if (!known) {
        error = "unknown chapter '" + chapterId + "'; rejected";
        return false;
    }
    const auto bucket = m_buckets.find(chapterId);
    if (bucket != m_buckets.end()) {
        for (const StoryNode& node : bucket->second) m_resident.emplace(node.id, node);
    }
    m_residentChapters[chapterId] = true;
    return true;
}

bool ChapterLoader::unloadChapter(const std::string& chapterId, std::string& error) {
    const bool known = std::any_of(
        m_chapters.begin(), m_chapters.end(),
        [&](const auto& entry) { return entry.second.id == chapterId; });
    if (!known) {
        error = "unknown chapter '" + chapterId + "'; rejected";
        return false;
    }
    if (chapterId == m_active) {
        error = "cannot unload the active chapter '" + chapterId + "'; rejected";
        return false;
    }
    const auto bucket = m_buckets.find(chapterId);
    if (bucket != m_buckets.end()) {
        for (const StoryNode& node : bucket->second) m_resident.erase(node.id);
    }
    m_residentChapters.erase(chapterId);
    return true;
}

const StoryNode* ChapterLoader::node(uint64_t nodeId) {
    const auto resident = m_resident.find(nodeId);
    if (resident != m_resident.end()) return &resident->second;
    std::string bucket;
    if (!bucketOf(nodeId, bucket)) {
        recordDiagnostic("unknown node " + std::to_string(nodeId) + "; rejected");
        return nullptr;
    }
    // Seffaf reload: kova resident'e alinir + tani uretilir.
    const auto stored = m_buckets.find(bucket);
    if (stored != m_buckets.end()) {
        for (const StoryNode& node : stored->second) m_resident.emplace(node.id, node);
    }
    m_residentChapters[bucket] = true;
    recordDiagnostic("transparent reload of chapter '" + bucket + "' for node " +
                     std::to_string(nodeId));
    const auto reloaded = m_resident.find(nodeId);
    return reloaded == m_resident.end() ? nullptr : &reloaded->second;
}

namespace {

const StoryNode* findNodeEverywhere(const std::unordered_map<uint64_t, StoryNode>& resident,
                                    const std::unordered_map<std::string, std::vector<StoryNode>>& buckets,
                                    uint64_t nodeId, const StoryNode*& bucketHit) {
    const auto residentIt = resident.find(nodeId);
    if (residentIt != resident.end()) {
        bucketHit = nullptr;
        return &residentIt->second;
    }
    for (const auto& [chapterId, nodes] : buckets) {
        (void)chapterId;
        for (const StoryNode& node : nodes) {
            if (node.id == nodeId) {
                bucketHit = &node;
                return &node;
            }
        }
    }
    return nullptr;
}

} // namespace

bool ChapterLoader::isChapterBoundaryNode(uint64_t nodeId) const {
    const StoryNode* bucketHit = nullptr;
    const StoryNode* found = findNodeEverywhere(m_resident, m_buckets, nodeId, bucketHit);
    if (found == nullptr) return false;
    for (const auto& [key, meta] : m_chapters) {
        (void)key;
        if (meta.hasStartNodeId && meta.startNodeId == nodeId) return true;
    }
    const std::string& chapter =
        found->chapterId.empty() ? kImplicitDefaultChapter : found->chapterId;
    for (const StoryNode::NextNode& next : found->nextNodes) {
        const StoryNode* targetHit = nullptr;
        const StoryNode* target = findNodeEverywhere(m_resident, m_buckets, next.nodeId, targetHit);
        if (target == nullptr) continue;
        const std::string targetChapter =
            target->chapterId.empty() ? kImplicitDefaultChapter : target->chapterId;
        if (targetChapter != chapter) return true;
    }
    return false;
}

std::vector<std::string> ChapterLoader::loadedChapters() const {
    std::vector<std::string> loaded;
    for (const std::string& id : m_order) {
        if (m_residentChapters.find(id) != m_residentChapters.end()) loaded.push_back(id);
    }
    return loaded;
}

std::vector<std::string> ChapterLoader::diagnostics() const {
    return {m_diagnostics.begin(), m_diagnostics.end()};
}

std::string ChapterLoader::loadedChaptersJson() const {
    nlohmann::json doc;
    doc["active"] = m_active;
    nlohmann::json loaded = nlohmann::json::array();
    nlohmann::json neighbors = nlohmann::json::array();
    nlohmann::json counts = nlohmann::json::object();
    std::unordered_map<std::string, std::size_t> residentCounts;
    for (const auto& [id, node] : m_resident) {
        (void)node;
        std::string bucket;
        if (bucketOf(id, bucket)) residentCounts[bucket]++;
    }
    for (const std::string& id : m_order) {
        if (m_residentChapters.find(id) == m_residentChapters.end()) continue;
        loaded.push_back(id);
        if (id != m_active) neighbors.push_back(id);
        counts[id] = residentCounts.count(id) ? residentCounts[id] : 0;
    }
    doc["loaded"] = std::move(loaded);
    doc["neighbors"] = std::move(neighbors);
    doc["node_counts"] = std::move(counts);
    doc["resident_nodes"] = m_resident.size();
    doc["total_nodes"] = m_totalNodes;
    doc["legacy_single_graph"] = m_legacySingleGraph;
    doc["last_diagnostic"] = m_lastDiagnostic;
    return doc.dump();
}

std::vector<uint64_t> ChapterLoader::chapterNodeIds(const std::string& chapterId) const {
    const auto bucket = m_buckets.find(chapterId);
    if (bucket == m_buckets.end()) return {};
    std::vector<uint64_t> ids;
    ids.reserve(bucket->second.size());
    for (const StoryNode& node : bucket->second) ids.push_back(node.id);
    return ids;
}

bool documentChapterBoundary(const StoryGraphDocument& document, uint64_t nodeId) {
    if (nodeId == 0 || document.chapters.empty()) return false;
    const auto it = document.nodes.find(nodeId);
    if (it == document.nodes.end()) return false;
    for (const GraphChapter& chapter : document.chapters) {
        if (chapter.hasStartNodeId && chapter.startNodeId == nodeId) return true;
    }
    const std::string& chapter =
        it->second.chapterId.empty() ? kImplicitDefaultChapter : it->second.chapterId;
    for (const StoryNode::NextNode& next : it->second.nextNodes) {
        const auto target = document.nodes.find(next.nodeId);
        if (target == document.nodes.end()) continue;
        const std::string targetChapter = target->second.chapterId.empty()
                                              ? kImplicitDefaultChapter
                                              : target->second.chapterId;
        if (targetChapter != chapter) return true;
    }
    return false;
}

} // namespace Rowl::Core
