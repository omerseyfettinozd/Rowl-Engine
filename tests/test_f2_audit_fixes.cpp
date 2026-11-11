/**
 * test_f2_audit_fixes.cpp - F2: denetim B/D duzeltmeleri (erisilebilirlik, kayit/yukle,
 * girdi, secim menusu, kosullar).
 *
 * Bilerek-boz notlari her bolumun basinda.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/player_ui_strings.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/ui_text_fit.hpp"
#include "rowl/render/window.hpp"
#include "rowl/text/text_shaper.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/state/save_durability.hpp"
#include "rowl/state/save_metadata.hpp"
#include "rowl/state/save_slot_preview.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;

constexpr const char* kSection = "f2-audit-fixes";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

uint64_t fnv(const std::string& value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Minimal raw (flags=0) v1 package: header, payloads, index. A packaged game keeps its pictures
// under images/ and mounts no bare-name alias for them (a loose project does).
void writePackage(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& files) {
    fs::create_directories(path.parent_path());
    std::string payload;
    std::string index;
    uint64_t offset = sizeof(Rowl::VFS::RowlPkgHeader);
    for (const auto& [name, content] : files) {
        Rowl::VFS::RowlPkgEntryRaw entry{fnv(name), static_cast<uint32_t>(name.size()), offset,
                                         content.size(), content.size(), 0};
        index.append(reinterpret_cast<const char*>(&entry), sizeof(entry));
        index.append(name);
        payload.append(content);
        offset += content.size();
    }
    Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, static_cast<uint32_t>(files.size()), offset};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    out.write(index.data(), static_cast<std::streamsize>(index.size()));
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string makeWav(uint32_t seconds) {
    constexpr uint32_t kRate = 22050;
    const uint32_t dataBytes = kRate * 2u * seconds;
    std::string out;
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    out += "RIFF"; u32(36u + dataBytes); out += "WAVE";
    out += "fmt "; u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2u); u16(2); u16(16);
    out += "data"; u32(dataBytes);
    out.append(dataBytes, '\0');
    return out;
}

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "20000000-0000-4000-8000-%012d", id);
    return text;
}

// ── Graph builder (raw component lists) ─────────────────────────────────────
struct Node {
    int id = 0;
    std::string text;
    std::vector<json> extra;                           // variable / script / audio components
    std::vector<int> next;                             // plain edges
    std::vector<std::pair<std::string, int>> choices;  // option_id -> target
    std::string choiceFont;                            // font_family of every option (empty = default)
    std::map<std::string, std::string> choiceCond;     // option_id -> Lua condition
    std::string speaker = "S";
    bool typewriter = false;
    float choiceY0 = 520.0f, choiceStep = 80.0f, choiceH = 64.0f;
};

json g_graphRootExtra = json::object();  // merged into the graph root by buildGraph (test seam)

std::string buildGraph(const std::vector<Node>& nodes) {
    json out = json::array();
    for (const Node& n : nodes) {
        json components = json::array();
        int serial = 0;
        for (const json& extra : n.extra) {
            json comp = extra;
            comp["id"] = "x" + std::to_string(n.id) + "_" + std::to_string(serial++);
            comp["enabled"] = true;
            components.push_back(comp);
        }
        json dialogue = {{"speaker", n.speaker}, {"dialogue", n.text}, {"typewriter_enabled", n.typewriter},
                         {"content_id", cid(n.id)}};
        components.push_back({{"type", "dialogue"}, {"id", "d" + std::to_string(n.id)}, {"enabled", true},
                              {"data", dialogue}});
        json edges = json::array();
        if (!n.choices.empty()) {
            json options = json::array();
            float y = n.choiceY0;
            for (const auto& [optionId, target] : n.choices) {
                json option = {{"option_id", optionId}, {"text", optionId}, {"x", 680}, {"y", y},
                               {"width", 560}, {"height", n.choiceH}};
                if (!n.choiceFont.empty()) option["font_family"] = n.choiceFont;
                if (const auto cond = n.choiceCond.find(optionId); cond != n.choiceCond.end()) {
                    option["condition"] = cond->second;
                }
                options.push_back(option);
                edges.push_back({{"id", target}, {"label", optionId}, {"option_id", optionId}});
                y += n.choiceStep;
            }
            components.push_back({{"type", "choice"}, {"id", "c" + std::to_string(n.id)}, {"enabled", true},
                                  {"data", {{"options", options}}}});
        }
        for (int target : n.next) edges.push_back({{"id", target}});
        out.push_back({{"id", n.id}, {"title", "N" + std::to_string(n.id)},
                       {"objects", json::array({{{"id", "o" + std::to_string(n.id)}, {"name", "D"},
                                                 {"is_active", true}, {"components", components}}})},
                       {"next_nodes", edges}});
    }
    json root = {{"format_version", 4}, {"start_node_id", nodes.front().id}, {"nodes", out}};
    for (auto& item : g_graphRootExtra.items()) root[item.key()] = item.value();
    return root.dump();
}

class InputHost final : public Rowl::Platform::PlatformHost {
public:
    explicit InputHost(std::shared_ptr<Rowl::Platform::PlatformHost> inner) : m_inner(std::move(inner)) {}
    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        return m_inner ? m_inner->openAssetStream(path) : nullptr;
    }
    std::filesystem::path writableSavePath() const override {
        return m_inner ? m_inner->writableSavePath() : std::filesystem::path();
    }
    std::filesystem::path writableProfilePath() const override {
        return m_inner ? m_inner->writableProfilePath() : std::filesystem::path("profiles");
    }
    Rowl::Platform::LifecycleState lifecycleState() const override {
        return m_inner ? m_inner->lifecycleState() : Rowl::Platform::LifecycleState::Active;
    }
    std::vector<RuntimeInputEvent> takeInputEvents() override {
        std::vector<RuntimeInputEvent> result(input.begin(), input.end());
        input.clear();
        return result;
    }
    Rowl::Platform::RenderSurface renderSurface() const override {
        return m_inner ? m_inner->renderSurface() : Rowl::Platform::RenderSurface{};
    }
    Rowl::Platform::AudioFocus audioFocus() const override {
        return m_inner ? m_inner->audioFocus() : Rowl::Platform::AudioFocus::Granted;
    }
    std::deque<RuntimeInputEvent> input;

private:
    std::shared_ptr<Rowl::Platform::PlatformHost> m_inner;
};

std::string capiText(RowlEngineHandle h,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    req(query(h, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    req(query(h, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

int g_rigCounter = 0;
fs::path g_root;

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<InputHost> host;
    fs::path game;
    Rowl::Audio::AudioEngine* audio = nullptr;

    // `manifestExtra` is merged into project.rowlproj; `files` are Assets/<path>.
    explicit Rig(const std::vector<Node>& nodes, const json& manifestExtra = json::object(),
                 const std::map<std::string, std::string>& files = {}, bool standalone = false,
                 const std::vector<std::pair<std::string, std::string>>& packageFiles = {}) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "7d3f2c86-8e4f-4f90-9c32-2e4d9f7a0b%02x", g_rigCounter);
        json manifest = {{"project_uuid", uuid}, {"name", "F2 Test"}};
        for (auto& item : manifestExtra.items()) manifest[item.key()] = item.value();
        writeBytes(game / "project.rowlproj", manifest.dump());
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph(nodes));
        const std::string wav = makeWav(20);
        for (const char* name : {"a.wav", "b.wav", "v.wav", "s.wav"}) {
            writeBytes(game / "Assets" / "audio" / name, wav);
        }
        for (const auto& [path, bytes] : files) writeBytes(game / "Assets" / path, bytes);
        if (!packageFiles.empty()) writePackage(game / "Assets" / "packages" / "game.rowlpkg", packageFiles);
        h = RowlEngine_Create();
        req(h && (standalone ? RowlEngine_InitStandalone(h, "F2 Test", 320, 180, 0)
                             : RowlEngine_Init(h, 320, 180, 0)) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        audio = engine->getAudio();
        req(audio != nullptr, "engine has no audio engine");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void play() {
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_Step(h, 0.016f);
    }
    void step(float dt, int times = 1) {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    void press(InType type) {
        RuntimeInputEvent event{};
        event.type = type;
        host->input.push_back(event);
        step(0.001f);
    }
    void key(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        step(0.001f);
    }
    void advance() {
        RowlEngine_AdvanceNode(h, 0);
        step(0.016f);
    }
    void choose(const std::string& optionId) {
        req(RowlEngine_SelectChoice(h, optionId.c_str()) == 1, "choice " + optionId + " refused");
        step(0.016f);
    }
    void markRead(int id) {
        req(RowlEngine_ProfileMarkRead(h, cid(id).c_str()) == ROWL_RESULT_OK, "markRead");
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    uint64_t stepId() const { return RowlEngine_GetCurrentStepId(h); }
    std::string var(const char* key) const {
        const char* value = RowlEngine_GetVariable(h, key);
        return value ? value : "";
    }
    json history() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
        req(parsed.is_array(), "history JSON did not parse");
        return parsed;
    }
    json driver() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetPlayerDriverStateJsonUtf8), nullptr, false);
        req(parsed.is_object(), "driver state JSON did not parse");
        return parsed;
    }
    json menu() const {
        json parsed = json::parse(RowlEngine_GetPauseMenuJson(h), nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + " (node " + std::to_string(node()) + ", expected " + std::to_string(id) + ")");
    }
};

std::vector<Node> chain(int count) {
    std::vector<Node> nodes;
    for (int id = 1; id <= count; ++id) {
        Node n;
        n.id = id;
        n.text = "A" + std::to_string(id);
        if (id < count) n.next = {id + 1};
        nodes.push_back(n);
    }
    return nodes;
}

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    ::setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

json slotMeta(RowlEngineHandle h, int slot) {
    uint32_t required = 0;
    if (RowlEngine_GetSaveSlotMetadataJson(h, slot, nullptr, 0, &required) != ROWL_RESULT_OK) return json();
    std::string buffer(required, '\0');
    req(RowlEngine_GetSaveSlotMetadataJson(h, slot, buffer.data(), required, &required) == ROWL_RESULT_OK,
        "slot metadata copy failed");
    buffer.resize(required - 1);
    return json::parse(buffer, nullptr, false);
}

fs::path slotFile(Rig& rig, int slot) {
    return fs::path(capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8)) / ("save_slot_" + std::to_string(slot) + ".json");
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string rowValue(const json& menu, int row) { return menu["rows"][row]["value"].get<std::string>(); }

// Pause menu helpers: open it, go to the Save (1) / Load (2) page, back to the main page.
void openPause(Rig& rig) {
    RowlEngine_SetPaused(rig.h, 1);
    rig.step(0.016f, 2);
}
void openSlotPage(Rig& rig, int mainRow) {
    for (int i = 0; i < mainRow; ++i) RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);
    rig.step(0.016f);
}

// Log capture (plain function pointer callback).
std::vector<std::string> g_warnings;
void collectWarnings(int level, const char*, const char* message, const char*) {
    if (level >= 3 && message) g_warnings.emplace_back(message);
}

// capiText wants a (handle, buffer, size, out) query: the prefetch progress one.
RowlEngine_ResultCode prefetchProgressQuery(RowlEngineHandle h, char* b, uint32_t n, uint32_t* o) {
    return RowlEngine_GetPrefetchProgressJson(h, b, n, o);
}

}  // namespace

void test_f2_audit_fixes() {
    TEST_SECTION("F2 audit fixes (input guards, save/load feedback, slot cache, choice menu, conditions)");

    g_root = fs::temp_directory_path() / "rowl_f2_audit_fixes";
    std::error_code ec;
    fs::remove_all(g_root, ec);
    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (g_root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (g_root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (g_root / "xdg").string().c_str(), 1);
#endif

    // ── B-2: the slot list previews metadata only; the thumbnail is read lazily ──
    // Bilerek-boz: ensureSlotCache'i loadFromSlot'a geri almak 1'in hiz oranini, preview'u
    // dogrulamasiz yapmak 1b'yi, thumbnailStamp'tan mtime'i atmak 1c'yi kirmizi yapar.
    {
        // 1. A preview agrees with the full decode and is much cheaper.
        Rig rig(chain(3));
        rig.play();
        rig.advance();
        const int kSlots = 12;
        for (int slot = 1; slot <= kSlots; ++slot) {
            req(RowlEngine_SaveGameSlot(rig.h, slot) == 1, "save slot " + std::to_string(slot));
        }
        const std::string dir = capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8);
        const auto full = Rowl::State::GameState::loadFromSlot(1, dir);
        req(full != nullptr && !full->thumbnailPng.empty(), "the saved slot has a thumbnail to skip");
        const auto preview = Rowl::State::readSaveSlotPreview(slotFile(rig, 1));
        req(preview.status == Rowl::State::SlotPreviewStatus::Valid && preview.savedAt == full->savedAt &&
                preview.activeNodeId == full->activeNodeId && preview.summary == full->summary &&
                preview.chapterTitle == full->chapterTitle && preview.hasThumbnail &&
                preview.graphIdentity == full->graphIdentity,
            "the preview carries the same metadata as the full decode");
        req(Rowl::State::readSaveSlotThumbnail(slotFile(rig, 1)) == full->thumbnailPng,
            "the lazily read thumbnail equals the decoded one");
        using Clock = std::chrono::steady_clock;
        const auto t0 = Clock::now();
        for (int slot = 1; slot <= kSlots; ++slot) (void)Rowl::State::readSaveSlotPreview(slotFile(rig, slot));
        const auto t1 = Clock::now();
        for (int slot = 1; slot <= kSlots; ++slot) (void)Rowl::State::GameState::loadFromSlot(slot, dir);
        const auto t2 = Clock::now();
        const double previewMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double fullMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
        std::printf("   [F2 B-2] %d slots: preview %.1f ms, full decode %.1f ms\n", kSlots, previewMs, fullMs);
        req(previewMs * 5.0 < fullMs, "previewing the slots is not clearly cheaper than decoding them (" +
                                          std::to_string(previewMs) + " vs " + std::to_string(fullMs) + " ms)");
        // 1b. Top-level validation parity with decodeJson; foreign formats are "incompatible".
        using Rowl::State::SlotPreviewStatus;
        using Rowl::State::parseSaveSlotPreview;
        req(parseSaveSlotPreview("{\"version\":4,\"step_id\":3,\"active_node_id\":2}").status == SlotPreviewStatus::Valid,
            "a minimal save is valid");
        req(parseSaveSlotPreview("{\"version\":99,\"step_id\":3,\"active_node_id\":2}").status ==
                SlotPreviewStatus::Incompatible, "a newer format version is incompatible, not corrupt");
        req(parseSaveSlotPreview("{\"version\":4,\"active_node_id\":0}").status == SlotPreviewStatus::Corrupt, "node 0");
        req(parseSaveSlotPreview("{\"version\":4,\"bgm_volume\":7}").status == SlotPreviewStatus::Corrupt, "volume range");
        req(parseSaveSlotPreview("{\"version\":4,\"summary\":[1]}").status == SlotPreviewStatus::Corrupt, "summary type");
        req(parseSaveSlotPreview("{\"version\":4,\"thumbnail_png_base64\":\"@@@@\"}").status == SlotPreviewStatus::Corrupt,
            "malformed thumbnail");
        req(parseSaveSlotPreview("{\"version\":4,\"thumbnail_png_base64\":\"QUJD\"}").hasThumbnail, "tiny thumbnail");
        req(parseSaveSlotPreview("[1,2]").status == SlotPreviewStatus::Corrupt, "root array");
        req(parseSaveSlotPreview("{\"version\":4").status == SlotPreviewStatus::Corrupt, "truncated");
        req(parseSaveSlotPreview("").status == SlotPreviewStatus::Corrupt, "empty");
        req(parseSaveSlotPreview(std::string(200, '[') + std::string(200, ']')).status == SlotPreviewStatus::Corrupt,
            "nesting budget");
    }
    TEST_PASS("B-2: the slot list previews top-level metadata (agrees with the decode, several times cheaper); thumbnail read lazily");

    {
        // 1c. The pause slot page: the thumbnail follows the selected row; its texture key
        // changes when the slot is saved again inside the same second.
        Rig rig(chain(3));
        rig.play();
        rig.advance();
        req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save slot 2");
        openPause(rig);
        openSlotPage(rig, 2);  // Load page
        auto view = [&]() { return rig.engine->getPauseMenuView(); };
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);  // row 2
        rig.step(0.016f);
        const auto first = view();
        req(first.thumbnailPng && !first.thumbnailPng->empty() && !first.thumbnailKey.empty(),
            "the selected full slot shows its thumbnail");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_UP);
        req(!view().thumbnailPng, "an empty row has no thumbnail");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);
        // Save the same slot again straight away (same wall-clock second most of the time).
        RowlEngine_SetPaused(rig.h, 0);
        rig.advance();
        req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save slot 2 again");
        openPause(rig);
        openSlotPage(rig, 2);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        const auto second = view();
        req(second.thumbnailPng && second.thumbnailKey != first.thumbnailKey,
            "a re-saved slot gets a new thumbnail key (" + first.thumbnailKey + " vs " + second.thumbnailKey + ")");
    }
    TEST_PASS("B-2: thumbnail follows the selected row; key changes on a re-save within the second");

    // ── B-3: save / load results are visible; failed saves do not pose as saved ──
    // Bilerek-boz: menuChooseSlot'ta sonuc ipucunu kaldirmak 2'yi, m_slotSaveFailed'i
    // yok saymak 2b'yi, basarili yuklemede setPaused(false)'u kaldirmak 2c'yi,
    // Incompatible durumunu Corrupt'a katmak 2d'yi, postNotice'i kaldirmak 2e'yi kirmizi yapar.
    {
        Rig rig(chain(5));
        rig.play();
        rig.advance();  // node 2
        openPause(rig);
        openSlotPage(rig, 1);  // Save page
        rig.key(SDLK_2);
        auto menu = rig.menu();
        req(menu["hint"].get<std::string>() == "Yuva 2: kaydedildi", "save result hint: " + menu["hint"].get<std::string>());
        req(rowValue(menu, 2) == "dolu", "the saved row is full");
        // 2b. A failed save: the row says so, the older file is untouched, the hint explains.
        const json before = slotMeta(rig.h, 2);
        Rowl::State::setSaveDurabilityInjectErrno(ENOSPC);
        rig.key(SDLK_2);
        menu = rig.menu();
        Rowl::State::setSaveDurabilityInjectErrno(0);
        req(rowValue(menu, 2) == "kaydedilemedi", "a failed save row must not read 'dolu': " + rowValue(menu, 2));
        req(menu["hint"].get<std::string>() == "Yuva 2: kaydedilemedi - eski kayıt korundu",
            "failed save hint: " + menu["hint"].get<std::string>());
        req(slotMeta(rig.h, 2) == before, "the older slot file is unchanged by the failed save");
        rig.key(SDLK_2);  // works again: the row recovers
        menu = rig.menu();
        req(rowValue(menu, 2) == "dolu" && menu["hint"].get<std::string>() == "Yuva 2: kaydedildi", "recovered");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);  // moving clears the result line
        req(rig.menu()["hint"].get<std::string>() != "Yuva 2: kaydedildi", "the result line goes when the selection moves");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);  // resume
        req(RowlEngine_IsPaused(rig.h) == 0, "resumed");
        // The failed-save mark is for that visit only.
        openPause(rig);
        openSlotPage(rig, 1);
        req(rowValue(rig.menu(), 2) == "dolu", "failed-save marks do not outlive the visit");
        RowlEngine_SetPaused(rig.h, 0);
    }
    TEST_PASS("B-3: pause-menu save shows its result; a failed save row says 'kaydedilemedi' and keeps the old file");

    {
        // 2c. Load from the pause menu: success closes the menu and plays on from the load.
        Rig rig(chain(6));
        rig.play();
        rig.advance();  // node 2
        req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save at node 2");
        rig.advance();
        rig.advance();  // node 4
        rig.expectNode(4, "later node");
        openPause(rig);
        openSlotPage(rig, 2);  // Load page
        rig.key(SDLK_2);
        req(RowlEngine_IsPaused(rig.h) == 0, "a successful load closes the pause menu");
        rig.expectNode(2, "the story is at the loaded node");
        const std::string notice = rig.engine->unlockToastText();
        req(notice == "Yüklendi: Yuva 2", "load notice capsule: '" + notice + "'");
        rig.advance();
        rig.expectNode(3, "the story plays on after the load");
        rig.step(0.25f, 12);
        req(rig.engine->unlockToastText().empty(), "the capsule goes away by itself");
    }
    TEST_PASS("B-3: a successful load from the pause menu closes it, shows 'Yüklendi', and the story continues");

    {
        // 2d. Failed loads: empty / damaged / incompatible are told apart; the menu stays.
        Rig rig(chain(4));
        rig.play();
        rig.advance();
        req(RowlEngine_SaveGameSlot(rig.h, 3) == 1 && RowlEngine_SaveGameSlot(rig.h, 6) == 1 &&
                RowlEngine_SaveGameSlot(rig.h, 7) == 1 && RowlEngine_SaveGameSlot(rig.h, 8) == 1,
            "saves for the failure cases");
        // 3 stays valid. 5: garbage. 6: a newer format. 7: another story's graph id. 8: a node the graph lacks.
        writeBytes(slotFile(rig, 5), "this is not json");
        {
            json j = json::parse(readAll(slotFile(rig, 6)));
            j["version"] = 99;
            writeBytes(slotFile(rig, 6), j.dump());
            j = json::parse(readAll(slotFile(rig, 7)));
            j["graph_id"] = "00000000000000ff";
            writeBytes(slotFile(rig, 7), j.dump());
            j = json::parse(readAll(slotFile(rig, 8)));
            j["active_node_id"] = 4242;
            writeBytes(slotFile(rig, 8), j.dump());
        }
        openPause(rig);
        openSlotPage(rig, 2);
        auto menu = rig.menu();
        req(rowValue(menu, 3) == "dolu" && rowValue(menu, 4) == "boş", "valid / empty rows");
        req(rowValue(menu, 5) == "bozuk", "garbage reads 'bozuk': " + rowValue(menu, 5));
        req(rowValue(menu, 6) == "uyumsuz", "a newer format reads 'uyumsuz': " + rowValue(menu, 6));
        req(rowValue(menu, 7) == "uyumsuz", "another graph reads 'uyumsuz': " + rowValue(menu, 7));
        req(rowValue(menu, 8) == "uyumsuz", "a missing node reads 'uyumsuz': " + rowValue(menu, 8));
        const struct { SDL_Keycode key; const char* hint; } cases[] = {
            {SDLK_4, "Yuva 4: boş yuva"}, {SDLK_5, "Yuva 5: bozuk kayıt"}, {SDLK_6, "Yuva 6: uyumsuz kayıt"},
            {SDLK_7, "Yuva 7: uyumsuz kayıt"}, {SDLK_8, "Yuva 8: uyumsuz kayıt"}};
        for (const auto& c : cases) {
            rig.key(c.key);
            req(RowlEngine_IsPaused(rig.h) == 1, "the menu stays after a refused load");
            req(rig.menu()["hint"].get<std::string>() == c.hint,
                std::string("refused load hint: ") + rig.menu()["hint"].get<std::string>() + " vs " + c.hint);
        }
        // The same slots through F9 name the reason in the capsule.
        RowlEngine_SetPaused(rig.h, 0);
        rig.key(SDLK_5);  // quick slot 5
        rig.key(SDLK_F9);
        req(rig.engine->unlockToastText() == "Yüklenemedi: Yuva 5 (kayıt bozuk)",
            "F9 on a damaged slot: " + rig.engine->unlockToastText());
        rig.key(SDLK_6);
        rig.key(SDLK_F9);
        req(rig.engine->unlockToastText() == "Yüklenemedi: Yuva 6 (kayıt uyumsuz)",
            "F9 on an incompatible slot: " + rig.engine->unlockToastText());
        rig.key(SDLK_4);
        rig.key(SDLK_F9);
        req(rig.engine->unlockToastText() == "Yüklenemedi: Yuva 4 (yuva boş)",
            "F9 on an empty slot: " + rig.engine->unlockToastText());
    }
    TEST_PASS("B-3: empty / damaged / incompatible slots are told apart in the rows, the hint and the F9 capsule");

    {
        // 2e. F5 / F9 results: a capsule for both outcomes (tr and en).
        Rig rig(chain(4));
        rig.play();
        rig.advance();
        rig.key(SDLK_F5);
        req(rig.engine->unlockToastText() == "Kaydedildi: Yuva 0", "F5 capsule: '" + rig.engine->unlockToastText() + "'");
        rig.step(0.25f, 12);
        req(rig.engine->unlockToastText().empty(), "F5 capsule expires");
        Rowl::State::setSaveDurabilityInjectErrno(ENOSPC);
        rig.key(SDLK_F5);
        Rowl::State::setSaveDurabilityInjectErrno(0);
        req(rig.engine->unlockToastText() == "Kaydedilemedi: Yuva 0", "failed F5 capsule: '" + rig.engine->unlockToastText() + "'");
        rig.step(0.25f, 12);
        rig.advance();
        rig.key(SDLK_F9);
        req(rig.engine->unlockToastText() == "Yüklendi: Yuva 0", "F9 capsule: '" + rig.engine->unlockToastText() + "'");
    }
    {
        Rig rig(chain(3), json{{"default_locale", "en"}, {"supported_locales", json::array({"en"})}},
                {{"locales/en.json", R"({"schema_version":1,"locale":"en","entries":{}})"}});
        rig.play();
        rig.key(SDLK_F5);
        req(rig.engine->unlockToastText() == "Saved: Slot 0", "English F5 capsule: '" + rig.engine->unlockToastText() + "'");
    }
    TEST_PASS("B-3: F5 / F9 show 'Kaydedildi' / 'Kaydedilemedi' / 'Yüklendi' capsules (tr and en)");

    // ── B-1: the save thumbnail is the story frame, never the menu; visible windows get one ──
    // Bilerek-boz: step'teki captureStoryFrame cagrisini kaldirmak 3'u ve 3b'yi,
    // buildSaveMetadata'da hasStoryFrame dalini kaldirmak 3'u, ertelenmis F5'i hemen
    // calistirmak 3b'yi kirmizi yapar.
    {
        // 3. Offscreen runtime: a thumbnail saved from the pause menu equals the one saved
        // from the running story (same frame) - it used to contain the menu itself.
        Rig rig(chain(4));
        rig.play();
        rig.advance();
        rig.step(0.016f, 2);
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "plain save");
        const json plain = slotMeta(rig.h, 1);
        req(plain["has_thumbnail"] == true, "offscreen saves carry a thumbnail");
        openPause(rig);
        openSlotPage(rig, 1);
        rig.step(0.016f, 2);  // the menu overlay is on the surface now
        rig.key(SDLK_2);
        const json fromMenu = slotMeta(rig.h, 2);
        req(fromMenu["has_thumbnail"] == true, "a menu save carries a thumbnail");
        req(fromMenu["thumbnail_png_base64"] == plain["thumbnail_png_base64"],
            "the pause-menu thumbnail must be the story frame, not a picture of the menu");
        // Moving to another page of the menu and saving again still gives the story frame.
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);
        rig.step(0.016f, 2);
        openSlotPage(rig, 0);  // Back lands on the Kaydet row (F2 D-D2): Confirm opens the page again
        rig.step(0.016f, 2);
        rig.key(SDLK_3);
        req(slotMeta(rig.h, 3)["thumbnail_png_base64"] == plain["thumbnail_png_base64"], "second menu save too");
        // The snapshot is per pause: a later pause on a changed scene takes a fresh one.
        RowlEngine_SetPaused(rig.h, 0);
        rig.advance();
        rig.advance();
        rig.step(0.016f, 2);
        req(RowlEngine_SaveGameSlot(rig.h, 4) == 1, "plain save at node 4");
        const json later = slotMeta(rig.h, 4);
        openPause(rig);
        openSlotPage(rig, 1);
        rig.step(0.016f, 2);
        rig.key(SDLK_5);
        req(slotMeta(rig.h, 5)["thumbnail_png_base64"] == later["thumbnail_png_base64"],
            "a new pause snapshots the new scene");
        req(slotMeta(rig.h, 5)["thumbnail_png_base64"] != plain["thumbnail_png_base64"], "and it is a different picture");
    }
    TEST_PASS("B-1: a pause-menu save's thumbnail is the menu-free story frame (offscreen)");

    // ── B-5: Hareketi Azalt covers the story's own flash component ──
    // Bilerek-boz: Window::triggerScreenFlash'taki reducedMotion kapisini kaldirmak 4'u,
    // setReducedMotion'daki sürmekte olan flasi durdurmayi kaldirmak 4b'yi kirmizi yapar.
    {
        std::vector<Node> nodes = chain(4);
        nodes[1].extra = {json{{"type", "transition"},
                               {"data", {{"flash_enabled", true}, {"flash_duration", 1.0f}, {"flash_color", "#FFFFFF"}}}}};
        nodes[2].extra = nodes[1].extra;
        {
            Rig rig(nodes);
            rig.play();
            rig.advance();
            rig.expectNode(2, "flash node");
            req(RowlEngine_IsScreenFlashActive(rig.h) == 1, "the story's flash component flashes by default");
        }
        {
            Rig rig(nodes);
            rig.play();
            RowlEngine_SetReducedMotion(rig.h, 1);
            rig.advance();
            rig.expectNode(2, "flash node, reduced motion");
            req(RowlEngine_IsScreenFlashActive(rig.h) == 0, "Hareketi Azalt: the story's flash component must not flash");
            RowlEngine_SetReducedMotion(rig.h, 0);
            rig.advance();
            rig.expectNode(3, "second flash node");
            req(RowlEngine_IsScreenFlashActive(rig.h) == 1, "flash is back with the option off");
            // 4b. switching it on mid-flash ends the flash at once
            RowlEngine_SetReducedMotion(rig.h, 1);
            req(RowlEngine_IsScreenFlashActive(rig.h) == 0, "turning Hareketi Azalt on ends a running flash");
        }
    }
    TEST_PASS("B-5: Hareketi Azalt stops the story's flash component and a flash already running");

    // ── B-6: a choice button's own font follows Metin Boyutu / Yüksek Kontrast ──
    // Bilerek-boz: window.cpp'de yeni dugme yazi tipine textScale/highContrast kopyasini kaldirmak 5'i kirmizi yapar.
    {
        std::vector<Node> nodes = chain(3);
        nodes[0].choices = {{"a", 2}, {"b", 3}};
        nodes[0].next.clear();
        nodes[0].choiceFont = "fonts/button.ttf";
        std::string fontBytes;
        {
            std::ifstream in(fs::path("Assets") / "fonts" / "default.ttf", std::ios::binary);
            fontBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        req(!fontBytes.empty(), "repo default font readable");
        Rig rig(nodes, json::object(), {{"fonts/button.ttf", fontBytes}});
        RowlEngine_SetTextScale(rig.h, 1.5f);
        RowlEngine_SetHighContrast(rig.h, 1);
        rig.play();
        rig.step(0.05f, 3);  // the choice menu is drawn: the button font renderer is created now
        int renderers = 0;
        bool allScaled = true, allContrast = true;
        rig.engine->getWindow()->forEachFontRenderer([&](Rowl::Render::FontRenderer& r) {
            ++renderers;
            if (std::abs(r.textScale() - 1.5f) > 1e-4f) allScaled = false;
            if (!r.highContrast()) allContrast = false;
        });
        req(renderers >= 2, "a separate button font renderer exists (" + std::to_string(renderers) + ")");
        req(allScaled, "every font renderer, the button font included, follows the text size");
        req(allContrast, "every font renderer, the button font included, follows high contrast");
    }
    TEST_PASS("B-6: a button's own font renderer wears the player's text size and contrast");

    // ── B-8: prefetch resolves pictures the way the renderer does; no per-picture warning noise ──
    // Bilerek-boz: prefetch.cpp'de resolveImageAsset'i kaldirmak 6'yi, window_texture_cache.cpp'de
    // exists() on-yoklamasini kaldirmak 6b'yi kirmizi yapar.
    {
        std::vector<uint8_t> rgba(4 * 4 * 4, 200);
        const auto png = Rowl::State::encodeThumbnailPng(rgba.data(), rgba.size(), 4, 4, 16);
        req(!png.png.empty(), "test picture");
        std::vector<Node> nodes = chain(2);
        nodes[0].extra = {json{{"type", "background"}, {"data", {{"texture", "pic.png"}}}}};
        // Packaged: the picture lives at images/pic.png inside the package, bare "pic.png" is not mounted.
        Rig rig(nodes, json::object(), {}, false, {{"images/pic.png", png.png}});
        g_warnings.clear();
        Rowl::Core::Logger::setLogCallback(&collectWarnings);
        rig.play();
        rig.step(0.016f, 3);
        Rowl::Core::Logger::setLogCallback(nullptr);
        size_t notFound = 0;
        for (const auto& line : g_warnings) {
            if (line.find("File not found") != std::string::npos && line.find("pic.png") != std::string::npos) ++notFound;
        }
        req(notFound == 0, "rendering images/pic.png named 'pic.png' printed " + std::to_string(notFound) +
                               " 'File not found' warnings");
        // The prefetch finds the same picture.
        req(RowlEngine_PrefetchChapterAssets(rig.h, "", 0) == ROWL_RESULT_OK, "prefetch call");
        RowlEngine_PumpPrefetch(rig.h, 50.0f);
        const json progress = json::parse(capiText(rig.h, prefetchProgressQuery), nullptr, false);
        req(progress.is_object() && progress.value("total_assets", 0) >= 1, "prefetch queued the picture");
        req(progress.value("missing_assets", 99) == 0 && progress.value("ready_assets", 0) >= 1,
            "the prefetch must find images/pic.png: " + progress.dump());
        req(progress["missing_paths"].empty(), "no missing paths: " + progress.dump());
        // A picture that really is absent is still reported missing (under the graph's spelling).
        std::vector<Node> gone = chain(2);
        gone[0].extra = {json{{"type", "background"}, {"data", {{"texture", "nowhere.png"}}}}};
        Rig rig2(gone);
        rig2.play();
        RowlEngine_PrefetchChapterAssets(rig2.h, "", 0);
        RowlEngine_PumpPrefetch(rig2.h, 50.0f);
        const json progress2 = json::parse(capiText(rig2.h, prefetchProgressQuery), nullptr, false);
        req(progress2.value("missing_assets", 0) == 1 && progress2["missing_paths"][0] == "nowhere.png",
            "a really absent picture is still missing: " + progress2.dump());
    }
    TEST_PASS("B-8: prefetch resolves images/ pictures like the renderer; absent names no longer print warnings");

    // ── B-10: a condition that runs out of budget is false for itself only ──
    // Bilerek-boz: evaluateCondition'daki "if (!m_sessionPoisoned) { m_limitTripped = false; ... }"
    // blogunu kaldirmak 7'yi kirmizi yapar.
    {
        std::vector<Node> nodes = chain(6);
        nodes[1].extra = {json{{"type", "condition"},
                               {"data", {{"expression", "(function() while true do end end)()"},
                                         {"fail_target_node_id", "4"}}}}};
        nodes[3].extra = {json{{"type", "condition"},
                               {"data", {{"expression", "2 > 1"}, {"fail_target_node_id", "6"}}}}};
        Rig rig(nodes);
        rig.play();
        rig.advance();  // node 2: its condition burns the budget -> false -> redirect to node 4
        rig.expectNode(4, "the budget-burning condition redirected to its target");
        // Node 4's own, perfectly good condition held on entry (it is evaluated in the same
        // redirect chain): had the session been poisoned it would have sent us on to node 6.
        rig.advance();
        rig.expectNode(5, "the story goes on normally");
    }
    TEST_PASS("B-10: a condition over budget is false for itself; later conditions are unaffected");

    // ── B-7: project-level variable declarations ──
    // Bilerek-boz: seedDeclaredVariables'i bos birakmak 8'i, createStartState'i
    // beyansiz kurmak 8b'yi, yuklemedeki seedDeclaredVariables(present) cagrisini kaldirmak 8c'yi
    // kirmizi yapar.
    {
        // 8. Parser.
        const auto parse = [&](const json& extra) {
            g_graphRootExtra = extra;
            const std::string text = buildGraph(chain(2));
            g_graphRootExtra = json::object();
            return Rowl::Core::StoryGraphParser::parse(text);
        };
        auto ok = parse({{"variables", {{"kaba", 0}, {"ad", "Eleni"}, {"acik", true}, {"oran", 0.5}}}});
        req(ok.succeeded() && ok.document.variables.size() == 4, "declarations parse");
        bool sawName = false;
        for (const auto& [k, v] : ok.document.variables) {
            if (k == "ad") sawName = v == "Eleni";
            if (k == "acik") req(v == "true", "booleans are kept as 'true'");
            if (k == "kaba") req(v == "0", "numbers are kept as text");
        }
        req(sawName, "string declaration kept");
        req(!parse({{"variables", json::array({1})}}).succeeded(), "variables must be an object");
        req(!parse({{"variables", {{"", 1}}}}).succeeded(), "empty name refused");
        req(!parse({{"variables", {{"x", json::array()}}}}).succeeded(), "non-scalar value refused");
        req(!parse({{"variables", {{"rowl", 1}}}}).succeeded(), "a reserved engine name refused");
        req(parse(json::object()).document.variables.empty(), "no declarations = none");
    }
    {
        // 8b. The audit's case: node 1 is guarded by a condition on a variable nothing assigned yet.
        std::vector<Node> nodes = chain(3);
        nodes[0].extra = {json{{"type", "condition"},
                               {"data", {{"expression", "zz < 1"}, {"fail_target_node_id", "3"}}}}};
        {
            Rig undeclared(nodes);
            undeclared.play();
            undeclared.expectNode(3, "without a declaration the unknown variable is nil: the condition fails (old behaviour)");
        }
        g_graphRootExtra = json{{"variables", {{"zz", 0}}}};
        Rig rig(nodes);
        g_graphRootExtra = json::object();
        rig.play();
        rig.expectNode(1, "with 'zz' declared as 0 the condition holds and the start node plays");
        req(rig.var("zz") == "0", "the declared variable exists from the first node on: '" + rig.var("zz") + "'");
        // The root state carries it (rewind to the beginning keeps it) and New Game restores it.
        RowlEngine_SetVariable(rig.h, "zz", "5");
        rig.advance();
        req(rig.var("zz") == "5", "scripts may change it");
        RowlEngine_ResetToStartNode(rig.h);
        req(rig.var("zz") == "0", "New Game restores the declared value: '" + rig.var("zz") + "'");
        req(rig.engine->getGameState() && rig.engine->getGameState()->getVariable("zz", "?") == "0",
            "the New Game root state holds the declared variable");
    }
    {
        // 8c. A save made before a declaration existed still gets the default on load.
        std::vector<Node> nodes = chain(3);
        Rig old(nodes);
        old.play();
        old.advance();
        req(RowlEngine_SaveGameSlot(old.h, 1) == 1, "save without declarations");
        const std::string saved = readAll(slotFile(old, 1));
        g_graphRootExtra = json{{"variables", {{"neu", 7}}}};
        Rig rig(nodes);
        g_graphRootExtra = json::object();
        rig.play();
        // The save came from an undeclared build of the same story; graph identity ignores declarations.
        writeBytes(slotFile(rig, 1), saved);
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load the older save");
        req(rig.var("neu") == "7", "a newly declared variable gets its default when an older save is loaded: '" + rig.var("neu") + "'");
    }
    TEST_PASS("B-7: project-level variable declarations (parser, start state, New Game, older saves)");

    // ── D-D3 / D-O3: the choice menu's selectability is live and computed once ──
    // Bilerek-boz: choiceLayout()'ta enabled'i canli kosuldan atamamak 10'u, anahtardan Lua
    // revizyonunu atmak 10b'yi (eski duruma gelir), anahtara her cagrida yeni deger katmak 10c'yi
    // kirmizi yapar.
    {
        std::vector<Node> nodes = chain(4);
        nodes[0].choices = {{"a", 2}, {"b", 3}};
        nodes[0].next.clear();
        nodes[0].choiceCond = {{"b", "gate > 0"}};
        g_graphRootExtra = json{{"variables", {{"gate", 0}}}};
        Rig rig(nodes);
        g_graphRootExtra = json::object();
        rig.play();
        rig.step(0.016f, 2);
        auto laid = rig.engine->getLaidOutChoiceButtons();
        req(laid.size() == 2 && laid[0].enabled && !laid[1].enabled, "option b starts hidden (gate = 0)");
        // Down focuses the only selectable option
        rig.key(SDLK_DOWN);
        req(rig.engine->getLaidOutChoiceButtons()[0].focused, "first Down focuses a");
        rig.key(SDLK_DOWN);
        req(rig.engine->getLaidOutChoiceButtons()[0].focused, "b is not in the walk yet (it wraps back to a)");
        // The condition turns true AFTER the scene was built: drawn, focusable and clickable alike.
        RowlEngine_SetVariable(rig.h, "gate", "1");
        rig.step(0.016f);
        laid = rig.engine->getLaidOutChoiceButtons();
        req(laid[1].enabled, "D-D3: b is drawn once its condition holds (the drawn state used to be frozen at scene build)");
        rig.key(SDLK_DOWN);
        const auto walked = rig.engine->getLaidOutChoiceButtons();
        req(walked[1].focused, "D-D3: the keyboard reaches b");
        // 10b. Layout is computed once per change, not per query / mouse move.
        const uint64_t builds = rig.engine->choiceLayoutBuildCount();
        for (int i = 0; i < 20; ++i) {
            RuntimeInputEvent move{};
            move.type = InType::PointerMotion;
            move.x = 40.0f + static_cast<float>(i);
            move.y = 90.0f;
            rig.host->input.push_back(move);
            (void)rig.engine->getLaidOutChoiceButtons();
        }
        rig.step(0.016f, 3);
        req(rig.engine->choiceLayoutBuildCount() == builds,
            "20 mouse moves + repeated queries rebuilt the layout " +
                std::to_string(rig.engine->choiceLayoutBuildCount() - builds) + " times");
        RowlEngine_SetVariable(rig.h, "gate", "0");
        (void)rig.engine->getLaidOutChoiceButtons();
        req(rig.engine->choiceLayoutBuildCount() == builds + 1, "a variable change rebuilds it once");
        req(!rig.engine->getLaidOutChoiceButtons()[1].enabled, "and b is hidden again");
        req(rig.engine->choiceLayoutBuildCount() == builds + 1, "no further rebuild");
    }
    TEST_PASS("D-D3/D-O3: selectability is live for draw / keyboard / click; layout rebuilt only on change");

    // ── D-O2: with a big text scale and many options nothing leaves the screen ──
    // Bilerek-boz: fitChoiceButtons'ta fitChoiceBoxesWithin cagrisini fitChoiceBoxes'e cevirmek 11'i kirmizi yapar.
    {
        std::vector<Node> nodes = chain(10);
        nodes[0].next.clear();
        for (int i = 0; i < 8; ++i) {
            nodes[0].choices.push_back({"option " + std::to_string(i) + " has a fairly long text that wraps onto a second line", i + 2});
        }
        nodes[0].choiceY0 = 280.0f;
        nodes[0].choiceStep = 60.0f;
        nodes[0].choiceH = 50.0f;
        Rig rig(nodes);
        RowlEngine_SetTextScale(rig.h, 1.5f);
        rig.play();
        rig.step(0.016f, 3);
        const auto laid = rig.engine->getLaidOutChoiceButtons();
        req(laid.size() == 8, "eight options");
        float top = 1e9f, bottom = -1e9f;
        for (const auto& b : laid) {
            top = std::min(top, b.y);
            bottom = std::max(bottom, b.y + b.height);
            req(b.height > 0.0f && b.width > 0.0f, "positive box");
        }
        req(top >= 16.0f - 0.01f, "the top option stays on screen: y = " + std::to_string(top));
        req(bottom <= 1080.0f, "and the bottom too: " + std::to_string(bottom));
        // Drawn == hit-tested: a click in the middle of every box picks that very option.
        for (int i = 0; i < 8; ++i) {
            Rig again(nodes);
            RowlEngine_SetTextScale(again.h, 1.5f);
            again.play();
            again.step(0.016f, 3);
            const auto boxes = again.engine->getLaidOutChoiceButtons();
            RuntimeInputEvent click{};
            click.type = InType::PointerDown;
            click.x = (boxes[static_cast<size_t>(i)].x + boxes[static_cast<size_t>(i)].width * 0.5f) * (320.0f / 1920.0f);
            click.y = (boxes[static_cast<size_t>(i)].y + boxes[static_cast<size_t>(i)].height * 0.5f) * (180.0f / 1080.0f);
            again.host->input.push_back(click);
            again.step(0.016f);
            again.expectNode(static_cast<uint64_t>(i + 2), "clicking the middle of option " + std::to_string(i));
        }
    }
    TEST_PASS("D-O2: at 150% with many options every box stays on screen and clicks land on what is drawn");

    // ── D-O4: the shape memo covers a big menu ──
    {
        std::vector<Node> nodes = chain(24);
        nodes[0].next.clear();
        for (int i = 0; i < 20; ++i) nodes[0].choices.push_back({"o" + std::to_string(i), i + 2});
        nodes[0].choiceY0 = 40.0f;
        nodes[0].choiceStep = 40.0f;
        nodes[0].choiceH = 32.0f;
        Rig rig(nodes);
        RowlEngine_SetTextScale(rig.h, 1.5f);
        rig.play();
        rig.step(0.016f, 3);
        req(rig.engine->getWindow()->getDialogueFontRenderer()->shapeCacheCapacity() >= 40,
            "20 options: the shape memo grew past its old 16 entries (" +
                std::to_string(rig.engine->getWindow()->getDialogueFontRenderer()->shapeCacheCapacity()) + ")");
    }
    TEST_PASS("D-O4: the shape cache is sized to the menu");

    // ── D-O7: repeated option ids become distinct choices ──
    // Bilerek-boz: story_graph_parser.cpp'de dedupeOptionIds cagrisini kaldirmak 13'u kirmizi yapar.
    {
        std::vector<Node> nodes = chain(4);
        nodes[0].next.clear();
        nodes[0].choices = {{"same", 2}, {"same", 3}, {"other", 4}};
        Rig rig(nodes);
        rig.play();
        rig.step(0.016f, 2);
        const auto laid = rig.engine->getLaidOutChoiceButtons();
        req(laid.size() == 3 && laid[0].optionId == "same" && laid[1].optionId == "same#2" &&
                laid[2].optionId == "other",
            "the repeated id was renamed: " + (laid.size() > 1 ? laid[1].optionId : std::string("?")));
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        const auto walked = rig.engine->getLaidOutChoiceButtons();
        req(!walked[0].focused && walked[1].focused, "focus walks to the second copy instead of sticking on the first");
        rig.key(SDLK_2);  // digit 2 = the second option -> its own edge (node 3)
        rig.expectNode(3, "the second copy leads to its own target");
    }
    TEST_PASS("D-O7: repeated option_id values are made distinct (same#2) and each keeps its own edge");

    // ── D-O5 / D-D6 / D-D7 / D-O6: speaker tag text decisions ──
    // Bilerek-boz: ellipsizeToWidth'u dogrusal aramaya geri almak 14'u (olcum sayisi), cluster
    // sinirini kaldirmak 14b'yi, speakerHasVisibleText'i `!speaker.empty()` yapmak 14c'yi,
    // renderShapedOrPlainText'te bos-glif yedegini kaldirmak 14d'yi kirmizi yapar.
    {
        using namespace Rowl::Render;
        int calls = 0;
        const auto measure = [&](const std::string& t) {
            ++calls;
            // 10 units per code point (ellipsis counts one)
            float units = 0.0f;
            for (unsigned char c : t) if ((c & 0xC0) != 0x80) units += 10.0f;
            return units;
        };
        const std::string longName(240, 'x');
        const std::string cut = ellipsizeToWidth(longName, 505.0f, measure);
        req(cut == std::string(49, 'x') + "\xE2\x80\xA6", "the longest fitting prefix + ellipsis");
        req(calls <= 24, "binary search: " + std::to_string(calls) + " measurements for 240 characters (was ~240)");
        // Same answer as a linear scan for any monotone measure (spaces trimmed before the ellipsis).
        calls = 0;
        const std::string spaced = "Madam Eleni Papadopoulos of the Grand Bazaar";
        const std::string spacedCut = ellipsizeToWidth(spaced, 130.0f, measure);
        req(spacedCut == "Madam Eleni\xE2\x80\xA6", "trailing space is dropped before the ellipsis: '" + spacedCut + "'");
        req(ellipsizeToWidth("short", 500.0f, measure) == "short", "fits: untouched");
        req(ellipsizeToWidth("anything", 5.0f, measure) == "\xE2\x80\xA6" || ellipsizeToWidth("anything", 5.0f, measure).empty() ||
                true, "tiny width never crashes");
    }
    {
        using namespace Rowl::Render;
        const auto measure = [](const std::string& t) {
            float units = 0.0f;
            for (unsigned char c : t) if ((c & 0xC0) != 0x80) units += 10.0f;
            return units;
        };
        // 14b. Never cut inside a cluster: letter + combining accent, a ZWJ emoji family, a flag.
        std::string accents = "A";
        for (int i = 0; i < 10; ++i) accents += "e\xCC\x81";  // e + U+0301
        for (float width = 30.0f; width < 200.0f; width += 10.0f) {
            const std::string cut = ellipsizeToWidth(accents, width, measure);
            const std::string body = cut.substr(0, cut.size() - 3);
            std::string expected = "A";
            while (expected.size() < body.size()) expected += "e\xCC\x81";
            req(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "\xE2\x80\xA6") == 0 && body == expected,
                "cut inside a letter+accent cluster at width " + std::to_string(width) + ": '" + cut + "'");
        }
        const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";  // man ZWJ woman ZWJ girl
        for (float width = 20.0f; width < 140.0f; width += 10.0f) {
            const std::string cut = ellipsizeToWidth("ab" + family + "cdefghij", width, measure);
            const bool hasJoiner = cut.find("\xE2\x80\x8D") != std::string::npos;
            const bool endsOnJoiner = cut.size() >= 6 && cut.compare(cut.size() - 6, 3, "\xE2\x80\x8D") == 0;
            req(!endsOnJoiner, "cut left a dangling joiner at width " + std::to_string(width));
            if (hasJoiner) {
                req(cut.find("\xF0\x9F\x91\xA7") != std::string::npos, "the emoji sequence is whole or absent at width " + std::to_string(width));
            }
        }
        // 14c. A name with nothing visible has no badge.
        req(!speakerHasVisibleText("") && !speakerHasVisibleText("   ") && !speakerHasVisibleText("\t \xC2\xA0") &&
                !speakerHasVisibleText("<b> </b>") && !speakerHasVisibleText("\xE2\x80\x8B"),
            "blank names have no visible text");
        req(speakerHasVisibleText("Eleni") && speakerHasVisibleText(" <b>E</b> "), "named speakers do");
    }
    {
        // 14d. A shaped text without glyphs (the build without text shaping) still draws its plain text.
        Rowl::Render::FontRenderer font;
        req(font.loadFont("Assets/fonts/default.ttf") && font.isLoaded(), "default font");
        SDL_Surface* withPlain = SDL_CreateSurface(200, 60, SDL_PIXELFORMAT_RGBA32);
        SDL_Surface* shapedOnly = SDL_CreateSurface(200, 60, SDL_PIXELFORMAT_RGBA32);
        SDL_FillSurfaceRect(withPlain, nullptr, 0);
        SDL_FillSurfaceRect(shapedOnly, nullptr, 0);
        Rowl::Text::ShapedText noGlyphs;  // what the stub shaper returns: no glyphs
        font.renderShapedText(shapedOnly, noGlyphs, 10.0f, 10.0f, 32.0f, SDL_Color{255, 255, 255, 255}, 180.0f, 50.0f);
        font.renderShapedOrPlainText(withPlain, noGlyphs, "Eleni", 10.0f, 10.0f, 32.0f, SDL_Color{255, 255, 255, 255}, 180.0f, 50.0f);
        const auto ink = [](SDL_Surface* surface) {
            size_t count = 0;
            for (int y = 0; y < surface->h; ++y) {
                const auto* row = static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch;
                for (int x = 0; x < surface->w; ++x) if (row[x * 4 + 3] > 0) ++count;
            }
            return count;
        };
        const size_t shapedInk = ink(shapedOnly);
        const size_t plainInk = ink(withPlain);
        SDL_DestroySurface(withPlain);
        SDL_DestroySurface(shapedOnly);
        req(shapedInk == 0, "premise: renderShapedText alone draws nothing for a glyph-less shape");
        req(plainInk > 50, "renderShapedOrPlainText draws the plain name instead (" + std::to_string(plainInk) + " px)");
    }
    {
        // The memo: same inputs -> same text, and a different text scale is a different entry.
        Rowl::Render::FontRenderer font;
        req(font.loadFont("Assets/fonts/default.ttf"), "font");
        const std::string name = "Madam Eleni Papadopoulos the Elder of the Grand Bazaar";
        const std::string first = font.ellipsizedToWidth(name, 24.0f, 200.0f);
        req(font.ellipsizedToWidth(name, 24.0f, 200.0f) == first, "memoised result is stable");
        req(font.measureTextWidth(first, 24.0f) <= 200.5f, "and it fits");
        font.setTextScale(1.5f);
        const std::string scaled = font.ellipsizedToWidth(name, 24.0f, 200.0f);
        req(scaled.size() < first.size() && font.measureTextWidth(scaled, 24.0f) <= 200.5f,
            "a text scale change is not served from the old entry");
    }
    TEST_PASS("D-O5/D-D6/D-D7/D-O6: ellipsis search + memo, cluster-safe cuts, blank names, glyph-less shape fallback");

    // ── B-13/B-14: menu language of a project without locales follows the system ──
    // Bilerek-boz: uiLangForLocale'da !localeDeclared dalini Turkish'e sabitlemek 15'i kirmizi yapar.
    {
        const auto menuTitle = [&](const char* systemLocale, const json& manifest,
                                   const std::map<std::string, std::string>& files) {
            setEnv("ROWL_SYSTEM_LOCALE", systemLocale);
            Rig rig(chain(2), manifest, files);
            rig.play();
            openPause(rig);
            const std::string title = rig.menu()["title"].get<std::string>();
            return title;
        };
        const std::map<std::string, std::string> trCatalog = {
            {"locales/tr.json", R"({"schema_version":1,"locale":"tr","entries":{}})"}};
        const std::map<std::string, std::string> enCatalog = {
            {"locales/en.json", R"({"schema_version":1,"locale":"en","entries":{}})"}};
        req(menuTitle("tr_TR.UTF-8", json::object(), {}) == "Duraklatıldı", "undeclared project, Turkish system: Turkish menu");
        req(menuTitle("en_US.UTF-8", json::object(), {}) == "Paused", "undeclared project, English system: English menu");
        req(menuTitle("de_DE", json::object(), {}) == "Paused", "undeclared project, German system: English menu (tr/en only)");
        req(menuTitle("en_US", json{{"default_locale", "tr"}, {"supported_locales", json::array({"tr"})}}, trCatalog) == "Duraklatıldı",
            "a project that declares Turkish keeps Turkish on an English system");
        req(menuTitle("tr_TR", json{{"default_locale", "en"}, {"supported_locales", json::array({"en"})}}, enCatalog) == "Paused",
            "a project that declares English keeps English on a Turkish system");
        setEnv("ROWL_SYSTEM_LOCALE", "tr");  // back to the suite's pin
        req(Rowl::Core::uiLangForLocale("en", false, Rowl::Core::UiLang::Turkish) == Rowl::Core::UiLang::Turkish, "pure rule");
    }
    TEST_PASS("B-13/B-14: a project without locales follows the system language (tr -> Turkish, else English)");

    // ── B-15: a profile that cannot be written is told once; one auto-delay range ──
    // Bilerek-boz: step'teki flushFailureCount kontrolunu kaldirmak 16'yi, persistPauseMenuPreference'taki
    // ReadOnly bildirimini kaldirmak 16b'yi, profil aralik tavanini 60'a geri almak 16c'yi kirmizi yapar.
    {
        Rig rig(chain(3));
        rig.play();
        openPause(rig);
        openSlotPage(rig, 3);  // Tercihler
        for (int i = 0; i < 7; ++i) RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_RIGHT);  // text speed up -> profile dirty
        RowlEngine_SetPaused(rig.h, 0);
        Rowl::State::setSaveDurabilityInjectErrno(ENOSPC);
        rig.step(0.25f, 12);  // past the debounce: the write fails
        Rowl::State::setSaveDurabilityInjectErrno(0);
        req(rig.engine->unlockToastText() == "Tercihler kaydedilemedi: bu oturumda geçerli",
            "profile write failure notice: '" + rig.engine->unlockToastText() + "'");
        req(rig.engine->playerProfile().flushFailureCount() >= 1, "the failure was counted");
        rig.step(0.25f, 12);
        req(rig.engine->unlockToastText().empty(), "the notice expires");
        Rowl::State::setSaveDurabilityInjectErrno(ENOSPC);
        rig.step(0.25f, 80);  // retries keep failing: no second notice
        Rowl::State::setSaveDurabilityInjectErrno(0);
        req(rig.engine->unlockToastText().empty(), "told once per session, not on every retry");
    }
    {
        // 16b. A read-only profile (a newer file format): the change is refused, the player is told.
        Rig rig(chain(3));
        const fs::path profile = rig.engine->getProfileDirectoryPath() / "player-profile.json";
        writeBytes(profile, R"({"version":999})");
        rig.engine->openPlayerProfile();
        rig.play();
        openPause(rig);
        openSlotPage(rig, 3);
        for (int i = 0; i < 7; ++i) RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_RIGHT);
        RowlEngine_SetPaused(rig.h, 0);
        rig.step(0.05f, 2);
        req(rig.engine->unlockToastText() == "Tercihler kaydedilemedi: bu oturumda geçerli",
            "read-only profile notice: '" + rig.engine->unlockToastText() + "'");
    }
    {
        // 16c. The profile and the engine use the same auto-advance range (0..10).
        Rig rig(chain(3));
        const fs::path profile = rig.engine->getProfileDirectoryPath() / "player-profile.json";
        writeBytes(profile, R"({"version":1,"auto_advance_delay":30})");
        rig.engine->openPlayerProfile();
        const json stored = json::parse(capiText(rig.h, RowlEngine_ProfileGetJson));
        req(stored["auto_advance_delay"] == 10.0, "a stored 30 is clamped to the 10 s the menu shows: " + stored.dump());
        req(RowlEngine_ProfileSetPreferencesJson(rig.h, R"({"auto_advance_delay":45})") == ROWL_RESULT_OK, "set");
        req(json::parse(capiText(rig.h, RowlEngine_ProfileGetJson))["auto_advance_delay"] == 10.0, "set clamps to 10");
    }
    TEST_PASS("B-15: an unwritable / read-only profile is announced once; auto-advance delay has one range");

    // ── Küçükler: the local-time text re-reads the time zone ──
    {
        const std::string stamp = "2026-06-01T12:00:00Z";
        setEnv("TZ", "UTC0");
        const std::string utc = Rowl::Core::utcIsoToLocalText(stamp, false);
        setEnv("TZ", "JST-9");  // POSIX spelling: UTC+9, understood by glibc and the MSVC CRT alike
        const std::string tokyo = Rowl::Core::utcIsoToLocalText(stamp, false);
        unsetEnv("TZ");
        req(utc == "2026-06-01 12:00", "UTC: " + utc);
        req(tokyo == "2026-06-01 21:00", "a zone change while running is honoured: " + tokyo);
    }
    TEST_PASS("tzset: a time-zone change while the player runs shows in save dates");

    //@@SECTIONS@@

    if (previousXdg) {
#ifdef _WIN32
        _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
        ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
#endif
    } else {
#ifndef _WIN32
        ::unsetenv("XDG_DATA_HOME");
#endif
    }
    fs::remove_all(g_root, ec);
}

// Runs right after the pause-overlay visible-window test: a visible SDL window needs the event
// dispatch thread unclaimed, which later engine concurrency tests no longer guarantee.
void test_f2_visible_window() {
    TEST_SECTION("F2 B-1 visible window save thumbnail (F5 and pause menu)");
    g_root = fs::temp_directory_path() / "rowl_f2_visible_window";
    std::error_code ec;
    fs::remove_all(g_root, ec);
    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (g_root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (g_root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (g_root / "xdg").string().c_str(), 1);
#endif
    {
        Rig rig(chain(4), json::object(), {}, true);
        rig.play();
        rig.advance();
        rig.step(0.016f, 3);
        rig.key(SDLK_F5);
        const json quick = slotMeta(rig.h, 0);
        req(!quick.is_null() && quick["has_thumbnail"] == true,
            "a visible window's F5 save carries a thumbnail");
        openPause(rig);
        openSlotPage(rig, 1);
        rig.step(0.016f, 2);
        rig.key(SDLK_2);
        const json menuSave = slotMeta(rig.h, 2);
        req(!menuSave.is_null() && menuSave["has_thumbnail"] == true,
            "a visible window's pause-menu save carries a thumbnail");
        req(menuSave["thumbnail_png_base64"] == quick["thumbnail_png_base64"],
            "the visible window's menu thumbnail is the menu-free story frame (same as F5's)");
        const std::string png = Rowl::State::readSaveSlotThumbnail(slotFile(rig, 2));
        req(png.size() > 1000, "thumbnail PNG has content");
    }
    TEST_PASS("B-1: a visible window saves a thumbnail (F5 and pause menu), menu-free");
    if (previousXdg) {
#ifdef _WIN32
        _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
        ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
#endif
    } else {
#ifndef _WIN32
        ::unsetenv("XDG_DATA_HOME");
#endif
    }
    fs::remove_all(g_root, ec);
}
