/**
 * c_api_story.cpp
 *
 * C-API story/graph surface: scene updates, graph loads, navigation, state queries.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/vfs/vfs.hpp"
#include "fstream"
#include "filesystem"
#include "nlohmann/json.hpp"
extern "C" {
/* ── Scene / story control ───────────────────────────────────────────────── */

void RowlEngine_UpdateScene(
    RowlEngineHandle handle,
    const char* speaker,
    const char* dialogue,
    const char* background,
    float bgX,   float bgY,   float bgW,   float bgH,
    const char* character,
    float charX, float charY, float charW, float charH,
    float dlgX,  float dlgY,  float dlgW,  float dlgH)
{
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->updateActiveScene(
        speaker    ? speaker    : "",
        dialogue   ? dialogue   : "",
        background ? background : "",
        bgX, bgY, bgW, bgH,
        character  ? character  : "",
        charX, charY, charW, charH,
        dlgX,  dlgY,  dlgW,  dlgH
    ); });
}

void RowlEngine_UpdateSceneEx(
    RowlEngineHandle handle,
    const char* speaker,
    const char* dialogue,
    const char* background,
    float bgX,   float bgY,   float bgW,   float bgH,   float bgRot,
    const char* character,
    float charX, float charY, float charW, float charH, float charRot,
    float dlgX,  float dlgY,  float dlgW,  float dlgH)
{
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->updateActiveScene(
        speaker    ? speaker    : "",
        dialogue   ? dialogue   : "",
        background ? background : "",
        bgX, bgY, bgW, bgH,
        character  ? character  : "",
        charX, charY, charW, charH,
        dlgX,  dlgY,  dlgW,  dlgH,
        bgRot, charRot
    ); });
}

void RowlEngine_UpdateSceneFromJson(
    RowlEngineHandle handle,
    const char* componentsJson)
{
    if (!isLiveHandle(handle) || !componentsJson) return;
    invokeNoexcept([&] { toEngine(handle)->updateSceneFromComponents(componentsJson); });
}

void RowlEngine_LoadStoryGraph(RowlEngineHandle handle, const char* jsonPath) {
    if (!isLiveHandle(handle)) return;
    if (!jsonPath || !*jsonPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Story graph path is null or empty",
                                  "load_story_graph_path", "");
                }
            }
        });
        return;
    }
    // Engine'in path'i geçici olarak override et ve graph'i yükle
    invokeNoexcept([&] {
        if (auto* engine = toEngine(handle)) {
            engine->loadStoryGraphFromPath(jsonPath);
        }
    });
}

int RowlEngine_LoadStoryGraphFromVfs(RowlEngineHandle handle, const char* vfsPath) {
    if (!isLiveHandle(handle)) return 0;
    if (!vfsPath || !*vfsPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Story graph VFS path is null or empty",
                                  "load_story_graph_vfs", "");
                }
            }
        });
        return 0;
    }
    int loaded = 0;
    invokeNoexcept([&] {
        if (auto* engine = toEngine(handle)) {
            loaded = engine->loadStoryGraphFromVfs(vfsPath) ? 1 : 0;
        }
    });
    return loaded;
}

const char* RowlEngine_GetLastStoryGraphError(RowlEngineHandle handle) {
    static thread_local std::string buffer;
    buffer.clear();
    if (!isLiveHandle(handle)) return buffer.c_str();
    invokeNoexcept([&] { buffer = toEngine(handle)->getLastStoryGraphLoadError(); });
    return buffer.c_str();
}

void RowlEngine_SetProjectDirectory(RowlEngineHandle handle, const char* projectRoot) {
    if (!isLiveHandle(handle) || !projectRoot || !*projectRoot) return;
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        // Save slots belong to the selected game/project. This prevents an
        // embedded editor preview or another standalone game from sharing the
        // process-relative default "saves" directory.
        engine->setSaveDirectory((std::filesystem::path(projectRoot) / "saves").string());
        // Project-owned defaults are read at the mount boundary so player and
        // embedded editor preview resolve the same component contract.
        std::string transition = "instant";
        float transitionDuration = 1.0f;
        const auto manifestPath = std::filesystem::path(projectRoot) / "project.rowlproj";
        std::error_code manifestError;
        if (std::filesystem::is_regular_file(manifestPath, manifestError) && !manifestError &&
            std::filesystem::file_size(manifestPath, manifestError) <= 1024 * 1024 && !manifestError) {
            try {
                std::ifstream manifest(manifestPath);
                const auto json = nlohmann::json::parse(manifest);
                transition = json.value("default_bgm_transition", transition);
                transitionDuration = json.value("default_bgm_transition_duration_seconds", transitionDuration);
            } catch (...) { }
        }
        engine->setBgmTransitionDefaults(transition, transitionDuration);
        if (engine->getVfs()) {
            engine->getVfs()->remountProject(projectRoot);
        }
        auto* win = engine->getWindow();
        if (win) {
            win->reloadFonts();
        }
        // Try loading story graph via VFS first (project Assets is now mounted)
        engine->loadStoryGraphFile();
        if (engine->getCurrentNodeId() == 0) {
            std::string graphPath = std::string(projectRoot) + "/Assets/json/full_story_graph.json";
            if (std::filesystem::exists(graphPath)) {
                engine->loadStoryGraphFromPath(graphPath);
            } else {
                std::string altGraphPath = std::string(projectRoot) + "/full_story_graph.json";
                if (std::filesystem::exists(altGraphPath)) {
                    engine->loadStoryGraphFromPath(altGraphPath);
                }
            }
        }
    });
}

void RowlEngine_SetBgmTransitionDefaults(RowlEngineHandle handle, const char* transition, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setBgmTransitionDefaults(transition ? transition : "instant", durationSeconds); });
}

void RowlEngine_AdvanceNode(RowlEngineHandle handle, uint32_t choiceIndex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->advanceToNextNode(choiceIndex); });
}

int RowlEngine_SelectChoice(RowlEngineHandle handle, const char* optionId) {
    if (!isLiveHandle(handle) || !optionId || !*optionId) return 0;
    return invokeNoexcept<int>([&] { return toEngine(handle)->advanceToChoice(optionId) ? 1 : 0; }, 0);
}

int RowlEngine_PointerDown(RowlEngineHandle handle, float x, float y) {
    if (!isLiveHandle(handle)) return 0;
    // Editor supplies 1920x1080 virtual coordinates; the engine's offscreen
    // surface is the same size, so the shared hit-test path remains canonical.
    return invokeNoexcept<int>([&] { return toEngine(handle)->handlePointerDown(x, y) ? 1 : 0; }, 0);
}

/* ── State queries ───────────────────────────────────────────────────────── */

const char* RowlEngine_GetSpeaker(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    // Returned pointer is valid until next step/update — owned by engine
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveSpeaker();
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetDialogue(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveDialogue();
        return buf.c_str();
    }, "");
}

uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] { return toEngine(handle)->getCurrentNodeId(); }, 0);
}

float RowlEngine_GetBackgroundRotation(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { return toEngine(handle)->getActiveBackgroundRotation(); }, 0.0f);
}

void RowlEngine_SetBackgroundParallax(RowlEngineHandle handle, float parallaxX, float parallaxY) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setBackgroundParallax(parallaxX, parallaxY); });
}

float RowlEngine_GetBackgroundParallaxX(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1.0f;
    return invokeNoexcept<float>([&] { return toEngine(handle)->getActiveBackgroundParallaxX(); }, 1.0f);
}

float RowlEngine_GetBackgroundParallaxY(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1.0f;
    return invokeNoexcept<float>([&] { return toEngine(handle)->getActiveBackgroundParallaxY(); }, 1.0f);
}

float RowlEngine_GetBackgroundOpacity(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1.0f;
    return invokeNoexcept<float>([&] { return toEngine(handle)->getActiveBackgroundOpacity(); }, 1.0f);
}

float RowlEngine_GetCharacterRotation(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { return toEngine(handle)->getActiveCharacterRotation(); }, 0.0f);
}

} // extern "C"
