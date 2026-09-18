/**
 * c_api_story.cpp
 *
 * C-API story/graph surface: scene updates, graph loads, navigation, state queries.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/i18n/localization_manager.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/vfs/vfs.hpp"
#include "algorithm"
#include "cstring"
#include "fstream"
#include "filesystem"
#include "nlohmann/json.hpp"
#include "vector"
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
    // A2a-tur2: explicit string — const char* is convertible to both the
    // string and the JSON overloads (ambiguous otherwise).
    invokeNoexcept([&] { toEngine(handle)->updateSceneFromComponents(std::string(componentsJson)); });
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

const char* RowlEngine_GetLastStoryGraphErrorWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastStoryGraphError(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

/* ── Graph vNext chapter queries ─────────────────────────────────────── */

// MSVC, extern "C" blogu icindeki C++ donuslu helper'a izin vermez (C2526);
// helper C++ linkage ile acikca isaretlenir (GCC/Clang'de zaten oyleydi).
extern "C++" {
namespace {

std::vector<const Rowl::Core::GraphChapter*> orderedChapters(const Rowl::Core::Engine* engine) {
    std::vector<const Rowl::Core::GraphChapter*> ordered;
    if (engine == nullptr) return ordered;
    for (const auto& chapter : engine->getStoryGraphDocument().chapters) {
        ordered.push_back(&chapter);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Rowl::Core::GraphChapter* left, const Rowl::Core::GraphChapter* right) {
                  if (left->order != right->order) return left->order < right->order;
                  return left->id < right->id;
              });
    return ordered;
}

} // namespace
} // extern "C++"

RowlEngine_ResultCode RowlEngine_GetCurrentChapterIdUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getCurrentChapterId(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetChapterCount(
    RowlEngineHandle handle, uint32_t* outCount) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    if (!outCount) return ROWL_RESULT_INVALID_ARGUMENT;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto chapters = engine->getStoryGraphDocument().chapters.size();
        *outCount = static_cast<uint32_t>(chapters);
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetChapterIdAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto ordered = orderedChapters(engine);
        if (index >= ordered.size()) return ROWL_RESULT_INVALID_ARGUMENT;
        return copyUtf8ToCaller(ordered[index]->id, buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

uint32_t RowlEngine_GetChoiceCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint32_t>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return static_cast<uint32_t>(0);
        return static_cast<uint32_t>(engine->getActiveChoiceButtons().size());
    }, 0);
}

RowlEngine_ResultCode RowlEngine_GetChoiceLabelAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto& buttons = engine->getActiveChoiceButtons();
        if (index >= buttons.size()) return ROWL_RESULT_INVALID_ARGUMENT;
        return copyUtf8ToCaller(buttons[index].text, buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetChoiceOptionIdAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto& buttons = engine->getActiveChoiceButtons();
        if (index >= buttons.size()) return ROWL_RESULT_INVALID_ARGUMENT;
        return copyUtf8ToCaller(buttons[index].optionId, buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetActiveDialogueContentIdsJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        nlohmann::json ids = nlohmann::json::array();
        for (const auto& dialogue : engine->getActiveDialogues()) {
            ids.push_back(dialogue.contentId);
        }
        return copyUtf8ToCaller(ids.dump(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

void RowlEngine_SetProjectDirectory(RowlEngineHandle handle, const char* projectRoot) {
    if (!isLiveHandle(handle) || !projectRoot || !*projectRoot) return;
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        // Save slots belong to the selected game/project. This prevents an
        // embedded editor preview or another standalone game from sharing the
        // process-relative default "saves" directory.
        const auto projectPath = Rowl::Platform::pathFromUtf8(projectRoot);
        const auto savePath = projectPath / "saves";
        engine->setSaveDirectory(Rowl::Platform::pathToUtf8(savePath));
        // Project-owned defaults are read at the mount boundary so player and
        // embedded editor preview resolve the same component contract.
        std::string transition = "instant";
        float transitionDuration = 1.0f;
        const auto manifestPath = projectPath / "project.rowlproj";
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
        // Faz 3 Dilim 1: manifest locales + catalogs load on project mount.
        // Legacy projects without locale keys keep the "en" fallback.
        Rowl::I18n::applyProjectLocalesToEngine(*engine, projectRoot);
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
            // A2b: UTF-8 in, wide path out — the narrow path ctor would
            // reinterpret a non-ASCII project root in the ANSI codepage on
            // Windows (mojibake miss), and the throwing probes turn a
            // hostile root into an escaped exception instead of a skip.
            const auto rootWide = Rowl::Platform::pathFromUtf8(projectRoot);
            const std::filesystem::path candidates[] = {
                rootWide / "Assets" / "json" / "full_story_graph.json",
                rootWide / "Assets" / "full_story_graph.json",
                rootWide / "full_story_graph.json",
            };
            std::error_code probeError;
            for (const auto& candidate : candidates) {
                probeError.clear();
                if (std::filesystem::is_regular_file(candidate, probeError) && !probeError) {
                    engine->loadStoryGraphFromPath(
                        Rowl::Platform::pathToUtf8(candidate));
                    break;
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

const char* RowlEngine_GetSpeakerWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetSpeaker(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

const char* RowlEngine_GetDialogueWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetDialogue(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
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
