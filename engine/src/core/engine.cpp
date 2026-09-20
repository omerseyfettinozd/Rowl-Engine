#include "rowl/core/engine.hpp"
#include "rowl/state/save_metadata.hpp"
#include "rowl/state/save_slots.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/scene/scene.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/render/frame_composition.hpp"
#include "rowl/scene/character_layers.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include <chrono>
#include <cstdio>
#include <thread>
#include <array>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Rowl::Core {

constexpr uint32_t kMaxVirtualCanvasDimension = 16'384;
constexpr std::size_t kMaxCharactersPerScene = 128;
constexpr std::size_t kMaxDialoguesPerScene = 128;
constexpr std::size_t kMaxChoiceButtonsPerScene = 256;
constexpr std::size_t kMaxScriptsPerScene = 32;
constexpr std::size_t kMaxAudioComponentsPerScene = 64;
constexpr std::size_t kMaxComponentStringBytes = 64 * 1024;
constexpr std::size_t kMaxNestedComponentValues = 4'096;
constexpr std::size_t kMaxComponentDataDepth = 32;
constexpr double kMaxComponentNumericMagnitude = 1'000'000.0;

// The quick-save / pause-menu slot window must stay inside the canonical
// save-slot range (rowl/state/save_slots.hpp: 0..99).
static_assert(kPauseMenuQuickSlotMin >= Rowl::State::kMinSaveSlot &&
              kPauseMenuQuickSlotMax <= Rowl::State::kMaxSaveSlot,
              "Quick-save slots must stay within the canonical 0..99 range");

namespace {

bool isPunctuationOrWhitespace(uint32_t cp) {
    if (cp <= 32) return true; // Whitespace & control characters
    switch (cp) {
        case '.': case ',': case '!': case '?': case ';': case ':':
        case '-': case '_': case '"': case '\'': case '`': case '~':
        case '(': case ')': case '[': case ']': case '{': case '}':
        case '<': case '>': case '/': case '\\': case '|': case '@':
        case '#': case '$': case '%': case '^': case '&': case '*':
        case '+': case '=':
        case 0x2026: // …
        case 0x2014: // —
        case 0x2013: // –
        case 0x201C: // “
        case 0x201D: // ”
        case 0x2018: // ‘
        case 0x2019: // ’
            return true;
        default:
            return false;
    }
}

std::shared_ptr<const Rowl::Text::ShapedText> shapeDialogue(
    const Rowl::Render::FontRenderer* renderer,
    const Rowl::Render::DialogueRenderData& dialogue) {
    if (renderer) return renderer->shapeTextShared(dialogue.dialogue, dialogue.fontSize);
    Rowl::Text::TextShaper fallback;
    Rowl::Text::ShapeOptions options;
    options.fontSize = dialogue.fontSize;
    return std::make_shared<Rowl::Text::ShapedText>(
        fallback.shapeMarkup(dialogue.dialogue, options));
}

bool isSafeComponentData(const nlohmann::json& value, std::size_t depth = 0) {
    if (depth > kMaxComponentDataDepth) return false;
    if (value.is_string()) return value.get_ref<const std::string&>().size() <= kMaxComponentStringBytes;
    if (value.is_number()) {
        const double number = value.get<double>();
        return std::isfinite(number) && std::abs(number) <= kMaxComponentNumericMagnitude;
    }
    if (value.is_array() || value.is_object()) {
        if (value.size() > kMaxNestedComponentValues) return false;
        for (const auto& entry : value) {
            if (!isSafeComponentData(entry, depth + 1)) return false;
        }
    }
    return true;
}

} // namespace

void Engine::setBgmTransitionDefaults(std::string kind, float durationSeconds) {
    if (kind != "instant" && kind != "fade" && kind != "crossfade") kind = "instant";
    m_defaultBgmTransition = std::move(kind);
    m_defaultBgmTransitionDurationSeconds = std::isfinite(durationSeconds)
        ? std::clamp(durationSeconds, 0.0f, 60.0f) : 1.0f;
}

Engine::Engine(std::shared_ptr<RuntimeContext> context)
    : m_context(context ? std::move(context) : std::make_shared<RuntimeContext>()) {}

Rowl::VFS::VFSManager* Engine::getVfs() const {
    return m_context ? m_context->getVfs().get() : nullptr;
}

std::shared_ptr<Rowl::Platform::PlatformHost> Engine::getPlatformHost() const {
    return m_context ? m_context->getPlatformHost() : nullptr;
}

std::string Engine::getSaveDirectory() const {
    return Rowl::Platform::pathToUtf8(getSaveDirectoryPath());
}

void Engine::setSaveDirectory(const std::string& saveDir) {
    m_saveDirectoryOverride = Rowl::Platform::pathFromUtf8(saveDir);
}

std::filesystem::path Engine::getSaveDirectoryPath() const {
    if (!m_saveDirectoryOverride.empty()) return m_saveDirectoryOverride;
    if (const auto host = getPlatformHost()) {
        const auto path = host->writableSavePath();
        if (!path.empty()) return path;
    }
    return "saves";
}

std::string Engine::getProfileDirectory() const {
    return Rowl::Platform::pathToUtf8(getProfileDirectoryPath());
}

std::filesystem::path Engine::getProfileDirectoryPath() const {
    if (const auto host = getPlatformHost()) {
        const auto path = host->writableProfilePath();
        if (!path.empty()) return path;
    }
    return "profiles";
}

Rowl::State::SessionPersistence& Engine::sessionPersistence() const {
    m_sessionPersistence.setSaveDirectory(getSaveDirectoryPath());
    return m_sessionPersistence;
}

void Engine::handleRuntimeInput(const Rowl::Platform::RuntimeInputEvent& event) {
    using Type = Rowl::Platform::RuntimeInputEvent::Type;
    switch (event.type) {
        case Type::PauseToggle:
            togglePause();
            break;
        case Type::MenuUp:
            if (m_paused) pauseMenuCommand(PauseMenuCommand::Up);
            break;
        case Type::MenuDown:
            if (m_paused) pauseMenuCommand(PauseMenuCommand::Down);
            break;
        case Type::MenuLeft:
            if (m_paused) pauseMenuCommand(PauseMenuCommand::Left);
            break;
        case Type::MenuRight:
            if (m_paused) pauseMenuCommand(PauseMenuCommand::Right);
            break;
        case Type::MenuBack:
            if (m_paused) pauseMenuCommand(PauseMenuCommand::Back);
            break;
        case Type::SelectSlot:
            if (m_paused) {
                if (m_pauseMode != PauseMenuMode::Main) menuChooseSlot(event.slot);
            } else {
                setQuickSaveSlot(event.slot);
            }
            break;
        case Type::Advance:
            // MS-6: Space/Enter confirm the menu selection while paused and
            // advance the story otherwise — one key, pause-routed.
            if (m_paused) menuActivateSelected();
            else advanceToNextNode();
            break;
        case Type::QuickSave:
            if (!m_paused) quickSave();
            break;
        case Type::QuickLoad:
            if (!m_paused) quickLoad();
            break;
        case Type::Rewind:
            if (!m_paused) rewind(1);
            break;
        case Type::PointerDown:
            if (!handlePointerDown(event.x, event.y)) advanceToNextNode();
            break;
        case Type::SwipeForward:
            if (m_paused) menuActivateSelected();
            else advanceToNextNode();
            break;
        case Type::SwipeBack:
            if (!m_paused) rewind(1);
            break;
        case Type::PointerUp:
        case Type::KeyUp:
        case Type::PointerMotion:
        case Type::Scroll:
        case Type::TextInput:
            // #16: consciously consumed at the Window boundary (the input
            // handler fires observably) but carrying no story action:
            // releases, hover/drag positions, scroll deltas and committed
            // text must never advance dialogue, toggle pause, rewind, or
            // touch save slots.
            break;
    }
}

void Engine::applyAudioSuspension(
    const std::shared_ptr<Rowl::Platform::PlatformHost>& host) {
    if (!m_audio) return;
    const bool hostSuspended = host &&
        (host->lifecycleState() != Rowl::Platform::LifecycleState::Active ||
         host->audioFocus() != Rowl::Platform::AudioFocus::Granted);
    m_audio->setOutputSuspended(m_windowAudioSuspended || hostSuspended);
}

Engine::~Engine() {
    if (m_initialized) {
        shutdown();
    }
}

void Engine::setExternalWindowHandle(void* nativeHandle, uint32_t w, uint32_t h) {
    m_externalWindowHandle = nativeHandle;
    m_externalWindowWidth  = w;
    m_externalWindowHeight = h;
    ROWL_LOG_INFO("External window handle set (" + std::to_string(w) + "x" + std::to_string(h) + ")");
}

bool Engine::initialize(const EngineConfig& config) {
    if (m_initialized) {
        // B1a (#103/#125): double-init sessiz-başarıydı (1) ve boyut
        // validasyonunu atlıyordu. Yüksek sesle reddet; shutdown→re-init
        // akışı etkilenmez (shutdown m_initialized'ı düşürür).
        ROWL_LOG_WARN("Engine is already initialized.");
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Engine is already initialized; shut down before re-initializing",
                                "init", "");
        }
        return false;
    }

    if (config.virtualWidth == 0 || config.virtualHeight == 0 ||
        config.virtualWidth > kMaxVirtualCanvasDimension ||
        config.virtualHeight > kMaxVirtualCanvasDimension) {
        ROWL_LOG_ERROR("Invalid virtual canvas dimensions: " +
                       std::to_string(config.virtualWidth) + "x" + std::to_string(config.virtualHeight));
        // B1a (#104): init-başarısızlığı last-error yazmadan 0 dönüyordu
        // (stale OK). Her ret kanala işlenir.
        if (m_context) {
            m_context->setError(RuntimeErrorCode::ValidationError,
                                "Invalid virtual canvas dimensions: " +
                                    std::to_string(config.virtualWidth) + "x" +
                                    std::to_string(config.virtualHeight),
                                "init", "");
        }
        return false;
    }

    m_config = config;
    m_windowAudioSuspended = false;
    Logger::init();

    ROWL_LOG_INFO("==================================================");
    ROWL_LOG_INFO("Initializing Rowl Engine v1.0.0 (Embedded Library Mode)");
    ROWL_LOG_INFO("App Name: " + m_config.appName);
    ROWL_LOG_INFO("Target Virtual Canvas: " + std::to_string(m_config.virtualWidth) + "x" + std::to_string(m_config.virtualHeight));
    const auto platformHost = getPlatformHost();
    const auto hostSurface = platformHost
        ? platformHost->renderSurface()
        : Rowl::Platform::RenderSurface{};
    const bool hasNativeSurface = m_externalWindowHandle ||
        (hostSurface.kind == Rowl::Platform::RenderSurfaceKind::Native && hostSurface.nativeHandle);
    ROWL_LOG_INFO("Mode: " + std::string(hasNativeSurface ? "EMBEDDED (Single-Window)" : "STANDALONE"));
    ROWL_LOG_INFO("==================================================");

    // Initialize VFS Manager
    if (getVfs()) {
        getVfs()->initialize();
    }

    // Initialize Render Window
    m_window = std::make_unique<Rowl::Render::Window>(getVfs());

    bool windowOk = false;
    if (hasNativeSurface) {
        // ── Legacy embedded mode: render into host native surface ──
        void* nativeHandle = m_externalWindowHandle ? m_externalWindowHandle : hostSurface.nativeHandle;
        const uint32_t surfaceWidth = m_externalWindowHandle
            ? m_externalWindowWidth : hostSurface.width;
        const uint32_t surfaceHeight = m_externalWindowHandle
            ? m_externalWindowHeight : hostSurface.height;
        windowOk = m_window->initializeEmbedded(
            nativeHandle,
            surfaceWidth  > 0 ? surfaceWidth  : m_config.virtualWidth,
            surfaceHeight > 0 ? surfaceHeight : m_config.virtualHeight,
            m_config.vsync
        );
    } else if (hostSurface.kind == Rowl::Platform::RenderSurfaceKind::Offscreen) {
        windowOk = m_window->initializeOffscreen(
            hostSurface.width > 0 ? hostSurface.width : m_config.virtualWidth,
            hostSurface.height > 0 ? hostSurface.height : m_config.virtualHeight
        );
    } else if (m_config.standaloneWindow) {
        // ── Standalone window mode: top-level SDL3 desktop window ──
        windowOk = m_window->initialize(
            m_config.appName,
            m_config.virtualWidth,
            m_config.virtualHeight,
            m_config.vsync
        );
    } else {
        // ── Offscreen Framebuffer mode (Texture Sharing / Zero-Copy) ──
        windowOk = m_window->initializeOffscreen(
            m_config.virtualWidth,
            m_config.virtualHeight
        );
    }

    if (!windowOk) {
        ROWL_LOG_ERROR("Failed to initialize render window!");
        // B1a (#104/#126-kısmi): yarı-pencereyi tutma (yeniden-init
        // overwrite eder, shutdown geri alamazdı) + kanala işle.
        m_window.reset();
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Failed to initialize render window", "init", "");
        }
        return false;
    }

    m_window->setInputHandler([this](const Rowl::Platform::RuntimeInputEvent& event) {
        handleRuntimeInput(event);
    });

    // Initialize Entity-Component Scene Manager
    m_scene = std::make_unique<Rowl::Scene::Scene>();

    // Initialize Audio Engine Subsystem
    m_audio = std::make_unique<Rowl::Audio::AudioEngine>(getVfs());
    // B1a (#105): dönüş çöpe atılıyor, Init yine 1 dönüyordu. Başarısız
    // alt-sistem init'i yüksek sesle kapatır (bugün audio hep true döner —
    // sözleşme-pini; lua OOM'da false dönebilir).
    if (!m_audio->initialize()) {
        ROWL_LOG_ERROR("Failed to initialize audio engine!");
        m_audio.reset();
        m_scene.reset();
        m_window.reset();
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Failed to initialize audio engine", "init", "");
        }
        return false;
    }

    // Initialize Sandboxed Lua Scripting Environment
    m_luaSandbox = std::make_unique<Rowl::Scripting::LuaSandbox>();
    if (!m_luaSandbox->initialize()) {
        ROWL_LOG_ERROR("Failed to initialize sandboxed Lua environment!");
        m_luaSandbox.reset();
        m_audio.reset();
        m_scene.reset();
        m_window.reset();
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Failed to initialize sandboxed Lua environment", "init", "");
        }
        return false;
    }

    // Initialize GameState Subsystem (Root Step #1)
    m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.currentNodeId());

    // Load story graph from disk
    loadStoryGraphFile();

    m_initialized = true;
    m_isRunning   = true;
    // D1 (#108): init, önceki last-result'u geçersiz kılar. Pre-init
    // load/save girişimi artık guard'lı (aşağıda) ama savunma-derinliği:
    // init-sonrası kanal her zaman init'e aittir, bayat load-OK kalamaz.
    if (m_context) {
        m_context->setSuccess("init", "");
    }
    return true;
}

void Engine::setPlayState(bool isPlaying) {
    m_isPlaying = isPlaying;
    m_autoAdvanceElapsed = 0.0f;
    m_activeDialogueData.isPlaying = isPlaying;
    if (isPlaying) {
        m_activeDialogueData.elapsedTypewriterTime = 0.0f;
        m_activeDialogueData.lastBlipCodepointIndex = 0;
        for (auto& dlg : m_activeDialogues) {
            dlg.elapsedTypewriterTime = 0.0f;
            dlg.lastBlipCodepointIndex = 0;
        }
    } else {
        m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
        m_activeDialogueData.lastBlipCodepointIndex = 99999;
        for (auto& dlg : m_activeDialogues) {
            dlg.elapsedTypewriterTime = 9999.0f;
            dlg.lastBlipCodepointIndex = 99999;
        }
    }
    ROWL_LOG_INFO("Engine Play State set to: " + std::string(isPlaying ? "PLAYING" : "STOPPED"));
}

void Engine::resetToStartNode() {
    if (!m_storyRuntime.resetToStart()) return;

    m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.startNodeId());
    m_lastRecordedDialogueNodeId = 0;
    m_lastSfxPlaybackNodeId = 0;
    if (const StoryNode* activeNode = m_storyRuntime.currentNode()) {
        const auto& startNode = *activeNode;
        if (!startNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : startNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson);
        } else {
            updateActiveScene(
                startNode.speaker, startNode.dialogue,
                startNode.background,
                startNode.backgroundX,  startNode.backgroundY,
                startNode.backgroundWidth, startNode.backgroundHeight,
                startNode.character,
                startNode.characterX,   startNode.characterY,
                startNode.characterWidth, startNode.characterHeight,
                startNode.dialogueBoxX, startNode.dialogueBoxY,
                startNode.dialogueBoxWidth, startNode.dialogueBoxHeight
            );
        }
        // MS-6: presenting the start node arms its typewriter regardless of
        // play state. Paused hosts still render full text (the renderer keys
        // visibility off the per-line playing flag) and report static frames.
        for (auto& dlg : m_activeDialogues) {
            dlg.elapsedTypewriterTime = 0.0f;
            dlg.lastBlipCodepointIndex = 0;
        }
        m_activeDialogueData.elapsedTypewriterTime = 0.0f;
        m_activeDialogueData.lastBlipCodepointIndex = 0;
        ROWL_LOG_INFO("Engine Reset to Start Node #" + std::to_string(m_storyRuntime.currentNodeId()));
    }
}

void Engine::endSession() {
    setPlayState(false);
    if (m_luaSandbox) m_luaSandbox->clearVariables();
    // No scene re-hydration here by design: resetToStartNode() re-presents
    // the start node with replay on, and audio/variable entry effects bump
    // stepId back above 1 (mid-session gate trips on step alone). The mount
    // boundary needs pristine markers, visuals stay as-is (the host pushes
    // right after the mount anyway).
    if (!m_storyRuntime.resetToStart()) return;
    m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.startNodeId());
    m_lastRecordedDialogueNodeId = 0;
    m_lastSfxPlaybackNodeId = 0;
}

const uint8_t* Engine::getPixelBuffer(uint32_t* outW, uint32_t* outH) const {
    return getPixelBuffer(outW, outH, nullptr);
}

const uint8_t* Engine::getPixelBuffer(uint32_t* outW, uint32_t* outH, uint32_t* outPitch) const {
    if (outW) *outW = m_window ? m_window->getWidth() : 0;
    if (outH) *outH = m_window ? m_window->getHeight() : 0;
    if (outPitch) *outPitch = m_window ? m_window->getPixelPitch() : 0;
    return m_window ? m_window->getPixelBuffer() : nullptr;
}

bool Engine::completeTypewriterIfTyping() {
    // MS-6: click-to-complete is a property of the presented line, not of the
    // play state. A typing line completes on the first advance request from
    // ANY input (keyboard, pointer, swipe, choice) in both player and
    // preview; only a settled line advances the story.
    // D6 #148: the snap targets each line's OWN totalSeconds, never a magic
    // constant. Pause-heavy lines can exceed 9999 s of reveal time; an
    // under-shooting snap leaves the line "typing", so the next advance is
    // swallowed here again (soft-lock). Lines that are not typing keep their
    // state untouched; the blip index snaps to revealUnits.size(), which is
    // provably in range (evaluateReveal never reports more visible units).
    // The +10 ms margin is load-bearing: elapsed is float while totalSeconds
    // is double, and a bare cast can land one ulp below the cursor, reading
    // back incomplete on the very next query.
    constexpr double kSnapMarginSeconds = 0.01;
    const auto* fontRenderer = m_window ? m_window->getFontRenderer() : nullptr;
    bool anyTyping = false;
    for (auto& dlg : m_activeDialogues) {
        if (dlg.typewriterEnabled && dlg.textSpeed > 0) {
            const auto shaped = shapeDialogue(fontRenderer, dlg);
            const auto reveal = Rowl::Text::evaluateReveal(
                *shaped, dlg.elapsedTypewriterTime, dlg.textSpeed);
            if (!reveal.complete) {
                anyTyping = true;
                // reveal.totalSeconds is the PARTIAL cursor (evaluateReveal
                // breaks at `elapsed`), so it must not be the snap target —
                // it grows with every snap and `complete` never arrives.
                // Re-evaluate at +inf for the true line total; the margin
                // covers the float(double) round-trip on re-query.
                const double fullTotal = Rowl::Text::evaluateReveal(
                    *shaped, std::numeric_limits<double>::infinity(),
                    dlg.textSpeed).totalSeconds;
                dlg.elapsedTypewriterTime = static_cast<float>(
                    std::max<double>(dlg.elapsedTypewriterTime,
                                     fullTotal + kSnapMarginSeconds));
                dlg.lastBlipCodepointIndex = shaped->revealUnits.size();
            }
        }
    }
    if (m_activeDialogueData.typewriterEnabled && m_activeDialogueData.textSpeed > 0) {
        const auto shaped = shapeDialogue(fontRenderer, m_activeDialogueData);
        const auto reveal = Rowl::Text::evaluateReveal(
            *shaped, m_activeDialogueData.elapsedTypewriterTime,
            m_activeDialogueData.textSpeed);
        if (!reveal.complete) {
            anyTyping = true;
            const double fullTotal = Rowl::Text::evaluateReveal(
                *shaped, std::numeric_limits<double>::infinity(),
                m_activeDialogueData.textSpeed).totalSeconds;
            m_activeDialogueData.elapsedTypewriterTime = static_cast<float>(
                std::max<double>(m_activeDialogueData.elapsedTypewriterTime,
                                 fullTotal + kSnapMarginSeconds));
            m_activeDialogueData.lastBlipCodepointIndex = shaped->revealUnits.size();
        }
    }
    return anyTyping;
}

void Engine::advanceToNextNode(uint32_t choiceIndex) {
    if (m_storyRuntime.empty()) return;
    // MS-6: the pause menu is modal — story advance is suspended until resume.
    // (Paused Advance/Pointer inputs are rerouted to menu actions before
    // reaching here; this guard seals direct API calls too.)
    if (m_paused) return;
    m_autoAdvanceElapsed = 0.0f;

    // If typewriter is still typing out any line, the request reveals the
    // full text immediately instead of advancing (MS-6 unified contract).
    if (completeTypewriterIfTyping()) return;

    const auto advanceResult = m_storyRuntime.advance(choiceIndex);
    if (advanceResult == StoryRuntime::AdvanceResult::CurrentNodeMissing) {
        // A2a-tur2: the reset used to fire with no log line (unlike the
        // ChoiceUnavailable INFO below). A missing cursor is always a bug
        // upstream — say so.
        ROWL_LOG_WARN("Advance from missing node #" +
                      std::to_string(m_storyRuntime.currentNodeId()) + "; resetting to start node");
        resetToStartNode();
        return;
    }
    if (advanceResult == StoryRuntime::AdvanceResult::ChoiceUnavailable) {
        // End of story chain: stay on last frame (do not loop back)
        ROWL_LOG_INFO("End of story chain reached on Node #" +
                      std::to_string(m_storyRuntime.currentNodeId()));
        return;
    }
    if (advanceResult != StoryRuntime::AdvanceResult::Advanced) return;

    if (m_gameState) {
        m_gameState = Rowl::State::GameState::createNextState(
            m_gameState, m_storyRuntime.currentNodeId());
    }

    if (const StoryNode* activeNode = m_storyRuntime.currentNode()) {
        const auto& nextNode = *activeNode;
        if (!nextNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : nextNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson);
        } else {
            updateActiveScene(
                nextNode.speaker, nextNode.dialogue,
                nextNode.background,
                nextNode.backgroundX, nextNode.backgroundY,
                nextNode.backgroundWidth, nextNode.backgroundHeight,
                nextNode.character,
                nextNode.characterX, nextNode.characterY,
                nextNode.characterWidth, nextNode.characterHeight,
                nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
            );
        }
        // MS-6: presenting the next node arms its typewriter regardless of
        // play state (same contract as resetToStartNode above).
        for (auto& dlg : m_activeDialogues) {
            dlg.elapsedTypewriterTime = 0.0f;
            dlg.lastBlipCodepointIndex = 0;
        }
        m_activeDialogueData.elapsedTypewriterTime = 0.0f;
        m_activeDialogueData.lastBlipCodepointIndex = 0;
        ROWL_LOG_INFO("▶ Active Node #" + std::to_string(m_storyRuntime.currentNodeId()) +
                      " (" + nextNode.speaker + "): " + nextNode.dialogue);
    }
}

bool Engine::advanceToChoice(const std::string& optionId) {
    if (optionId.empty()) return false;
    if (!m_storyRuntime.currentNode()) return false;
    const auto choice = m_storyRuntime.resolveChoice(optionId);
    if (!choice) {
        ROWL_LOG_WARN("Choice option not found on Node #" +
                      std::to_string(m_storyRuntime.currentNodeId()) + ": " + optionId);
        return false;
    }

    if (m_hasActiveScript && m_luaSandbox) {
        for (const auto& moduleId : m_activeScriptModuleIds) {
            m_luaSandbox->callOptionalModuleFunction(moduleId, "on_choice");
        }
    }

    advanceToNextNode(choice->index);
    return m_storyRuntime.currentNodeId() == choice->targetNodeId;
}

bool Engine::handlePointerDown(float physicalX, float physicalY) {
    // B6 (#9): NaN x/y fails every comparison in containsPhysicalPoint, so
    // the tap is misclassified as bezelTap — and with no choice buttons the
    // bezel path returns false (advance). A non-finite tap is not a tap:
    // consume it as a no-op before it can advance the story.
    if (!std::isfinite(physicalX) || !std::isfinite(physicalY)) {
        ROWL_LOG_WARN("Non-finite pointer coordinates consumed without advancing");
        return true;
    }
    if (!m_window) return false;
    float x = 0.0f, y = 0.0f;
    bool bezelTap = false;
    if (!m_window->mapPhysicalToVirtual(physicalX, physicalY, 1920, 1080, x, y, bezelTap)) {
        return false;
    }
    // MS-6: the pause menu is modal — every canvas tap feeds it, so composed
    // hosts (click = PointerDown, else AdvanceNode) can never advance the
    // story behind the menu.
    if (m_paused) {
        if (!bezelTap) pauseMenuClick(x, y);
        return true;
    }
    if (m_activeChoiceButtons.empty()) return false;
    // Letterbox/pillarbox margins are not story canvas. Consume input there so
    // the caller does not turn a bezel tap into an accidental advance.
    if (bezelTap) {
        return true;
    }
    for (auto it = m_activeChoiceButtons.rbegin(); it != m_activeChoiceButtons.rend(); ++it) {
        const auto& button = *it;
        if (!button.enabled) continue;
        if (x >= button.x && x <= button.x + button.width &&
            y >= button.y && y <= button.y + button.height) {
            advanceToChoice(button.optionId);
            return true;
        }
    }
    return false;
}

void Engine::updateActiveScene(
    const std::string& speaker,
    const std::string& dialogue,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::string& character,
    float charX, float charY, float charW, float charH,
    float dlgX, float dlgY, float dlgW, float dlgH,
    float bgRot, float charRot
) {
    // B6 (#6): the 14 legacy geometry floats flowed unsanitized into member
    // state and then into SDL_FRect production (NaN drops the frame, Inf
    // overflows, negative w/h draws invisible/mirrored). Non-finite inputs
    // keep last-valid, finite ones are magnitude-capped; w/h stay positive.
    // One InvalidArgument record per call (fail-loud); valid inputs identical.
    auto fixSceneFloat = [&](float& v, float keep, float lo, float hi) {
        if (!std::isfinite(v)) {
            v = keep;
        } else {
            v = std::clamp(v, lo, hi);
        }
    };
    const float mag = static_cast<float>(kMaxComponentNumericMagnitude);
    const float oldBgW = m_activeBackgroundWidth, oldBgH = m_activeBackgroundHeight;
    const float oldCharW = m_activeCharacterWidth, oldCharH = m_activeCharacterHeight;
    const float oldDlgW = m_activeDialogueBoxWidth, oldDlgH = m_activeDialogueBoxHeight;
    bool sceneArgsValid =
        std::isfinite(bgX) && std::isfinite(bgY) && std::isfinite(bgW) && std::isfinite(bgH) &&
        std::isfinite(charX) && std::isfinite(charY) && std::isfinite(charW) && std::isfinite(charH) &&
        std::isfinite(dlgX) && std::isfinite(dlgY) && std::isfinite(dlgW) && std::isfinite(dlgH) &&
        std::isfinite(bgRot) && std::isfinite(charRot);
    fixSceneFloat(bgX, m_activeBackgroundX, -mag, mag);
    fixSceneFloat(bgY, m_activeBackgroundY, -mag, mag);
    fixSceneFloat(bgW, oldBgW > 0.0f ? oldBgW : 1920.0f, 1.0f, mag);
    fixSceneFloat(bgH, oldBgH > 0.0f ? oldBgH : 1080.0f, 1.0f, mag);
    fixSceneFloat(charX, m_activeCharacterX, -mag, mag);
    fixSceneFloat(charY, m_activeCharacterY, -mag, mag);
    fixSceneFloat(charW, oldCharW > 0.0f ? oldCharW : 360.0f, 1.0f, mag);
    fixSceneFloat(charH, oldCharH > 0.0f ? oldCharH : 540.0f, 1.0f, mag);
    fixSceneFloat(dlgX, m_activeDialogueBoxX, -mag, mag);
    fixSceneFloat(dlgY, m_activeDialogueBoxY, -mag, mag);
    fixSceneFloat(dlgW, oldDlgW > 0.0f ? oldDlgW : 1760.0f, 1.0f, mag);
    fixSceneFloat(dlgH, oldDlgH > 0.0f ? oldDlgH : 180.0f, 1.0f, mag);
    fixSceneFloat(bgRot, m_activeBackgroundRotation, -mag, mag);
    fixSceneFloat(charRot, m_activeCharacterRotation, -mag, mag);
    if (!sceneArgsValid) {
        ROWL_LOG_WARN("Legacy UpdateScene rejected a non-finite argument, kept last-valid geometry");
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Legacy scene geometry must be finite", "update_active_scene", "");
    }
    m_activeChoiceButtons.clear();
    m_hasBackground = !background.empty();
    m_hasDialogueBox = !dialogue.empty() || !speaker.empty();

    // Empty values are meaningful: switching to a silent/blank frame must not
    // leak text or assets from the previously active node.
    m_activeSpeaker    = speaker;
    m_activeDialogue   = dialogue;
    m_activeBackground = background;
    m_activeBackgroundX      = bgX;
    m_activeBackgroundY      = bgY;
    m_activeBackgroundWidth  = bgW;
    m_activeBackgroundHeight = bgH;
    m_activeBackgroundRotation = bgRot;

    m_activeCharacters.clear();
    if (!character.empty()) {
        m_activeCharacter       = character;
        m_activeCharacterX       = charX;
        m_activeCharacterY       = charY;
        m_activeCharacterWidth   = charW;
        m_activeCharacterHeight  = charH;
        m_activeCharacterRotation = charRot;
        m_activeCharacters.push_back({character, charX, charY, charW, charH, charRot, 1.0f, 1.0f});
    } else {
        m_activeCharacter = "";
        m_activeCharacterRotation = 0.0f;
    }

    m_activeDialogueBoxX     = dlgX;
    m_activeDialogueBoxY     = dlgY;
    m_activeDialogueBoxWidth = dlgW;
    m_activeDialogueBoxHeight = dlgH;

    m_activeDialogueData.hasDialogueBox = m_hasDialogueBox;
    m_activeDialogueData.speaker = m_activeSpeaker;
    m_activeDialogueData.dialogue = m_activeDialogue;
    m_activeDialogueData.x = dlgX;
    m_activeDialogueData.y = dlgY;
    m_activeDialogueData.width = dlgW;
    m_activeDialogueData.height = dlgH;
    m_activeDialogueData.typewriterEnabled = false;

    m_activeDialogues.clear();
    if (m_hasDialogueBox) {
        m_activeDialogues.push_back(m_activeDialogueData);
    }

    ROWL_LOG_INFO("Scene Updated (Legacy) → Speaker: '" + m_activeSpeaker + "', Dialogue: '" +
                  m_activeDialogue + "', BG: '" + m_activeBackground + "'");
    recordActiveDialogueHistory();
}

void Engine::updateSceneFromComponents(const std::string& componentsJson,
                                       bool replayEntryEffects) {
    if (componentsJson.size() > kMaxStoryJsonBytes) {
        ROWL_LOG_ERROR("Component JSON exceeds the maximum accepted size");
        return;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(componentsJson);
    } catch (const std::exception& e) {
        // A2a-tur2: no scene state was touched yet — nothing to restore.
        ROWL_LOG_ERROR("Failed to parse components JSON: " + std::string(e.what()));
        return;
    }
    updateSceneFromComponents(root, replayEntryEffects);
}

// A2a-tur2: JSON overload — owns the snapshot/restore contract formerly
// inline in the string version (behavior unchanged, parse step skipped).
void Engine::updateSceneFromComponents(const nlohmann::json& root,
                                       bool replayEntryEffects) {
    const auto previousCharacters = m_activeCharacters;
    const auto previousDialogues = m_activeDialogues;
    const auto previousChoices = m_activeChoiceButtons;
    const auto previousSpeaker = m_activeSpeaker;
    const auto previousDialogue = m_activeDialogue;
    const auto previousBackground = m_activeBackground;
    const auto previousCharacter = m_activeCharacter;
    const auto previousBackgroundX = m_activeBackgroundX;
    const auto previousBackgroundY = m_activeBackgroundY;
    const auto previousBackgroundWidth = m_activeBackgroundWidth;
    const auto previousBackgroundHeight = m_activeBackgroundHeight;
    const auto previousBackgroundRotation = m_activeBackgroundRotation;
    const auto previousCharacterX = m_activeCharacterX;
    const auto previousCharacterY = m_activeCharacterY;
    const auto previousCharacterWidth = m_activeCharacterWidth;
    const auto previousCharacterHeight = m_activeCharacterHeight;
    const auto previousCharacterRotation = m_activeCharacterRotation;
    const auto previousDialogueBoxX = m_activeDialogueBoxX;
    const auto previousDialogueBoxY = m_activeDialogueBoxY;
    const auto previousDialogueBoxWidth = m_activeDialogueBoxWidth;
    const auto previousDialogueBoxHeight = m_activeDialogueBoxHeight;
    const auto previousDialogueData = m_activeDialogueData;
    const auto previousHasBackground = m_hasBackground;
    const auto previousHasDialogueBox = m_hasDialogueBox;
    const auto previousGameState = m_gameState;
    const auto previousLuaVariables = m_luaSandbox ? m_luaSandbox->getAllVariables() : std::unordered_map<std::string, std::string>{};
    // #86: camera + live-mixer snapshot. Component zinciri kamera/mikseri
    // TRY içinde mutasyona uğratır (camera bile replayEntryEffects=false iken;
    // ses ertelenmiş blokta); throw'da görseller kadar ses+kamera da geri
    // alınır, yoksa yeni karede bayat mikser dururdu.
    const auto previousAudio = captureAudioSnapshot();
    const auto previousCamera = captureCameraSnapshot();
    const auto restorePreviousState = [&] {
        m_activeCharacters = previousCharacters;
        m_activeDialogues = previousDialogues;
        m_activeChoiceButtons = previousChoices;
        m_activeSpeaker = previousSpeaker;
        m_activeDialogue = previousDialogue;
        m_activeBackground = previousBackground;
        m_activeCharacter = previousCharacter;
        m_activeBackgroundX = previousBackgroundX;
        m_activeBackgroundY = previousBackgroundY;
        m_activeBackgroundWidth = previousBackgroundWidth;
        m_activeBackgroundHeight = previousBackgroundHeight;
        m_activeBackgroundRotation = previousBackgroundRotation;
        m_activeCharacterX = previousCharacterX;
        m_activeCharacterY = previousCharacterY;
        m_activeCharacterWidth = previousCharacterWidth;
        m_activeCharacterHeight = previousCharacterHeight;
        m_activeCharacterRotation = previousCharacterRotation;
        m_activeDialogueBoxX = previousDialogueBoxX;
        m_activeDialogueBoxY = previousDialogueBoxY;
        m_activeDialogueBoxWidth = previousDialogueBoxWidth;
        m_activeDialogueBoxHeight = previousDialogueBoxHeight;
        m_activeDialogueData = previousDialogueData;
        m_hasBackground = previousHasBackground;
        m_hasDialogueBox = previousHasDialogueBox;
        m_gameState = previousGameState;
        if (m_luaSandbox) {
            m_luaSandbox->clearVariables();
            for (const auto& [key, value] : previousLuaVariables) m_luaSandbox->setVariable(key, value);
        }
        // #86: önce kamera, sonra ses (apply* best-effort + WARN, dışarı
        // fırlatmaz; catch'in kendisi throw-safe kalır).
        applyCameraSnapshot(previousCamera);
        applyAudioSnapshot(previousAudio);
    };
    try {
        nlohmann::json comps = root;
        if (root.is_object() && root.contains("components") && root["components"].is_array()) {
            comps = root["components"];
        }
        if (!comps.is_array()) return;
        if (comps.size() > kMaxComponentsPerScene) {
            ROWL_LOG_ERROR("Component JSON exceeds the maximum component count");
            return;
        }

        // Validate the complete external payload before clearing the active
        // scene. A schema error must not leave the renderer half-updated.
        std::size_t dialogueCount = 0;
        std::size_t characterCount = 0;
        std::size_t choiceButtonCount = 0;
        std::size_t scriptCount = 0;
        std::size_t audioComponentCount = 0;
        for (const auto& comp : comps) {
            if (!comp.is_object() || !comp.contains("type") || !comp.contains("data") ||
                !comp["type"].is_string() || !comp["data"].is_object() ||
                (comp.contains("enabled") && !comp["enabled"].is_boolean()) ||
                !isSafeComponentData(comp["data"])) {
                ROWL_LOG_ERROR("Component JSON contains an invalid component schema");
                return;
            }
            if (!comp.value("enabled", true)) continue;

            const auto type = comp["type"].get<std::string>();
            if (type == "dialogue" && ++dialogueCount > kMaxDialoguesPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum dialogue count");
                return;
            }
            if (type == "character" && ++characterCount > kMaxCharactersPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum character count");
                return;
            }
            if (type == "script" && ++scriptCount > kMaxScriptsPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum script count");
                return;
            }
            if (type == "audio" && ++audioComponentCount > kMaxAudioComponentsPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum audio component count");
                return;
            }
            if (type == "choice") {
                const auto& options = comp["data"].value("options", nlohmann::json::array());
                if (options.is_array()) {
                    choiceButtonCount += options.size();
                    if (choiceButtonCount > kMaxChoiceButtonsPerScene) {
                        ROWL_LOG_ERROR("Component JSON exceeds the maximum choice button count");
                        return;
                    }
                }
            }
        }

        m_activeCharacters.clear();
        m_activeDialogues.clear();
        m_activeChoiceButtons.clear();
        m_activeSpeaker.clear();
        m_activeDialogue.clear();
        m_activeBackground.clear();
        m_activeCharacter.clear();
        m_activeBackgroundRotation = 0.0f;
        m_activeCharacterRotation = 0.0f;
        m_activeDialogueData = {};
        m_hasBackground = false;
        m_hasDialogueBox = false;
        std::vector<nlohmann::json> pendingAudioComponents;
        std::vector<nlohmann::json> pendingScripts;

        // The payload has passed schema validation, so the active script can
        // be notified without a malformed update leaving the scene half-live.
        m_scriptRuntimeStatuses.clear();
        deactivateScripts(replayEntryEffects);

        for (const auto& comp : comps) {
            if (!comp.contains("type") || !comp.contains("data")) continue;
            bool enabled = comp.value("enabled", true);
            if (!enabled) continue;

            std::string type = comp["type"].get<std::string>();
            const auto& data = comp["data"];

            if (type == "dialogue") {
                DialogueRenderData dlgData;
                dlgData.hasDialogueBox = true;
                dlgData.speaker = data.value("speaker", "Evelyn");
                dlgData.dialogue = data.value("dialogue", "");
                dlgData.contentId = data.value("content_id", "");
                dlgData.x = data.value("x", 80.0f);
                dlgData.y = data.value("y", 860.0f);
                dlgData.width = data.value("width", 1760.0f);
                dlgData.height = data.value("height", 180.0f);
                dlgData.scale = data.value("scale", 1.0f);
                dlgData.typewriterEnabled = data.value("typewriter_enabled", true);
                // B6 (#1/#12): raw text_speed hydration used to flow into
                // evaluateReveal cursor math unclamped — a huge value made
                // the reveal cursor unreachable (permanent lock), a negative
                // value silently disabled the typewriter. Clamp to
                // [1,1000] ms/char, fallback to default 30 + WARN.
                dlgData.textSpeed = 30;
                if (data.contains("text_speed")) {
                    const int rawSpeed = data.value("text_speed", 30);
                    if (rawSpeed < 1 || rawSpeed > 1000) {
                        ROWL_LOG_WARN("Dialogue text_speed out of range, clamped to [1,1000]");
                    }
                    dlgData.textSpeed = std::clamp(rawSpeed, 1, 1000);
                }
                dlgData.autoAdvance = data.value("auto_advance", false);
                dlgData.autoAdvanceDelay = std::clamp(data.value("auto_advance_delay", 2.0f), 0.0f, 60.0f);
                dlgData.fontSize = data.value("font_size", 24.0f);
                dlgData.speakerFontSize = data.value("speaker_font_size", 20.0f);
                dlgData.textColor = data.value("text_color", "#F1F5F9");
                dlgData.speakerColor = data.value("speaker_color", "#38BDF8");
                dlgData.textAlignment = data.value("text_alignment", "Left");
                dlgData.boxOpacity = data.value("box_opacity", 0.88f);
                dlgData.boxColor = data.value("box_color", "#0F0F1A");
                dlgData.borderColor = data.value("border_color", "#00F0FF");
                dlgData.borderThickness = data.value("border_thickness", 2.0f);
                dlgData.cornerRadius = data.value("corner_radius", 8.0f);
                dlgData.customBoxTexture = data.value("custom_box_texture", "");
                dlgData.typewriterSound = data.value("typewriter_sound", data.value("voice_blip_sound", ""));
                dlgData.voiceBlipPitch = data.value("voice_blip_pitch", 1.0f);
                dlgData.voiceBlipPitchVariance = data.value("voice_blip_variance", 0.08f);
                dlgData.voiceBlipCadence = std::max(1, data.value("voice_blip_cadence", 1));
                dlgData.voiceBlipSkipPunctuation = data.value("voice_blip_skip_punctuation", true);
                std::string blipChanStr = data.value("voice_blip_channel_name", "");
                if (blipChanStr == "Sfx" || blipChanStr == "sfx") {
                    dlgData.voiceBlipChannel = 2;
                } else {
                    dlgData.voiceBlipChannel = data.value("voice_blip_channel", 1);
                }
                dlgData.voiceBlipVolume = data.value("voice_blip_volume", 0.85f);
                dlgData.isPlaying = m_isPlaying;
                // MS-6: hydrating a line arms its typewriter regardless of play
                // state. Paused hosts still render full text (visibility keys
                // off isPlaying above) and report static frames.
                dlgData.elapsedTypewriterTime = 0.0f;
                dlgData.lastBlipCodepointIndex = 0;

                m_activeDialogues.push_back(dlgData);
                m_hasDialogueBox = true;

                // Sync first dialogue component with legacy single-dialogue state
                if (m_activeDialogues.size() == 1) {
                    m_activeSpeaker = dlgData.speaker;
                    m_activeDialogue = dlgData.dialogue;
                    m_activeDialogueBoxX = dlgData.x;
                    m_activeDialogueBoxY = dlgData.y;
                    m_activeDialogueBoxWidth = dlgData.width;
                    m_activeDialogueBoxHeight = dlgData.height;
                    m_activeDialogueData = dlgData;
                }
            } else if (type == "speaker") {
                m_activeSpeaker = data.value("speaker", m_activeSpeaker);
                m_activeDialogue = data.value("dialogue", m_activeDialogue);
                m_activeDialogueData.speaker = m_activeSpeaker;
                m_activeDialogueData.dialogue = m_activeDialogue;
                if (!m_activeDialogues.empty()) {
                    m_activeDialogues[0].speaker = m_activeSpeaker;
                    m_activeDialogues[0].dialogue = m_activeDialogue;
                }
            } else if (type == "background") {
                m_activeBackground = data.value("texture", "");
                m_activeBackgroundX = data.value("x", 0.0f);
                m_activeBackgroundY = data.value("y", 0.0f);
                m_activeBackgroundWidth = data.value("width", 1920.0f);
                m_activeBackgroundHeight = data.value("height", 1080.0f);
                m_activeBackgroundRotation = data.value("rotation", 0.0f);
                // B6 (#8): raw parallax hydration flowed into
                // transformRectParallax unclamped — NaN/Inf silently killed
                // the background. Reject non-finite (keep default 1.0),
                // clamp finite to [-8,8]; same contract as the setter.
                m_activeBackgroundParallaxX = 1.0f;
                m_activeBackgroundParallaxY = 1.0f;
                const float rawPx = data.value("parallax_x", 1.0f);
                const float rawPy = data.value("parallax_y", 1.0f);
                if (std::isfinite(rawPx) && std::isfinite(rawPy)) {
                    m_activeBackgroundParallaxX = std::clamp(rawPx, -8.0f, 8.0f);
                    m_activeBackgroundParallaxY = std::clamp(rawPy, -8.0f, 8.0f);
                } else {
                    ROWL_LOG_WARN("Non-finite background parallax rejected, kept default");
                }
                m_activeBackgroundOpacity = std::clamp(data.value("opacity", 1.0f), 0.0f, 1.0f);
                m_hasBackground = !m_activeBackground.empty();
            } else if (type == "character") {
                CharacterRenderData cd;
                cd.sprite = data.value("sprite", "");
                cd.x = data.value("x", 1440.0f);
                cd.y = data.value("y", 340.0f);
                cd.width = data.value("width", 360.0f);
                cd.height = data.value("height", 540.0f);
                cd.rotation = data.value("rotation", 0.0f);
                cd.scaleX = data.value("scale_x", data.value("scale", 1.0f));
                cd.scaleY = data.value("scale_y", data.value("scale", 1.0f));
                cd.voiceBlipSound = data.value("voice_blip_sound", data.value("typewriter_sound", ""));
                cd.voiceBlipPitch = data.value("voice_blip_pitch", 1.0f);
                cd.voiceBlipPitchVariance = data.value("voice_blip_variance", 0.08f);
                cd.voiceBlipCadence = std::max(1, data.value("voice_blip_cadence", 1));
                // Faz 5 Dilim 3 fix: `layers` varsa parse+compose yolu cizilir,
                // yoksa legacy sprite yolu aynen calisir. Parse basarisizsa
                // legacy sprite'a dusulur (fail-closed).
                bool layeredHandled = false;
                const bool hadCharacters = !m_activeCharacters.empty();
                if (data.contains("layers")) {
                    Rowl::Scene::CharacterLayers staged;
                    std::string layersError;
                    if (Rowl::Scene::CharacterLayers::parseComponentData(data, staged, layersError)) {
                        layeredHandled = true;
                        for (const auto& draw : staged.toSpriteDraws(cd.x, cd.y, cd.width, cd.height)) {
                            CharacterRenderData layered = cd;
                            layered.sprite = draw.asset;
                            layered.opacity = draw.opacity;
                            m_activeCharacters.push_back(layered);
                        }
                    } else {
                        ROWL_LOG_WARN("character layers ignored, legacy sprite kept (" + layersError + ")");
                    }
                }
                if (!layeredHandled && !cd.sprite.empty()) {
                    m_activeCharacters.push_back(cd);
                }

                // Set legacy single-character fallback to first character
                if (!hadCharacters && !m_activeCharacters.empty()) {
                    const auto& first = m_activeCharacters.front();
                    m_activeCharacter = first.sprite;
                    m_activeCharacterX = first.x;
                    m_activeCharacterY = first.y;
                    m_activeCharacterWidth = first.width;
                    m_activeCharacterHeight = first.height;
                    m_activeCharacterRotation = first.rotation;
                }
            } else if (type == "dialogue_box") {
                m_activeDialogueBoxX = data.value("x", m_activeDialogueBoxX);
                m_activeDialogueBoxY = data.value("y", m_activeDialogueBoxY);
                m_activeDialogueBoxWidth = data.value("width", m_activeDialogueBoxWidth);
                m_activeDialogueBoxHeight = data.value("height", m_activeDialogueBoxHeight);
                m_hasDialogueBox = true;
                m_activeDialogueData.x = m_activeDialogueBoxX;
                m_activeDialogueData.y = m_activeDialogueBoxY;
                m_activeDialogueData.width = m_activeDialogueBoxWidth;
                m_activeDialogueData.height = m_activeDialogueBoxHeight;
                m_activeDialogueData.hasDialogueBox = true;
                if (!m_activeDialogues.empty()) {
                    m_activeDialogues[0].x = m_activeDialogueBoxX;
                    m_activeDialogues[0].y = m_activeDialogueBoxY;
                    m_activeDialogues[0].width = m_activeDialogueBoxWidth;
                    m_activeDialogues[0].height = m_activeDialogueBoxHeight;
                    m_activeDialogues[0].hasDialogueBox = true;
                }
            } else if (type == "audio") {
                // Defer device side effects until every component has been
                // converted successfully; a later malformed field must not
                // leave audio changed while the scene is rolled back.
                pendingAudioComponents.push_back(data);
            } else if (type == "choice" && data.contains("options") && data["options"].is_array()) {
                size_t index = 0;
                for (const auto& option : data["options"]) {
                    ChoiceButtonRenderData button;
                    button.optionId = option.value("option_id", "");
                    button.text = option.value("text", "Choice");
                    button.enabled = option.value("enabled", true);

                    // Lua condition check (fail-closed: without a sandbox the
                    // gated choice stays disabled rather than opening).
                    std::string condition = option.value("condition", "");
                    if (!condition.empty() && condition != "true" && condition != "1") {
                        if (!m_luaSandbox || !m_luaSandbox->evaluateCondition(condition)) {
                            button.enabled = false;
                        }
                    }

                    button.x = option.value("x", 680.0f);
                    button.y = option.value("y", 520.0f + static_cast<float>(index) * 80.0f);
                    button.width = option.value("width", 560.0f);
                    button.height = option.value("height", 64.0f);
                    button.fontSize = option.value("font_size", 22.0f);
                    button.opacity = option.value("opacity", 1.0f);
                    button.borderThickness = option.value("border_thickness", 2.0f);
                    button.cornerRadius = option.value("corner_radius", 8.0f);
                    button.textColor = option.value("text_color", "#FFFFFF");
                    button.backgroundColor = option.value("background_color", "#1E293B");
                    button.hoverColor = option.value("hover_color", "#0EA5E9");
                    button.borderColor = option.value("border_color", "#38BDF8");
                    button.textAlignment = option.value("text_alignment", "Center");
                    button.fontFamily = option.value("font_family", "Default");
                    button.backgroundImage = option.value("normal_image", option.value("background_image", ""));
                    if (!button.optionId.empty()) m_activeChoiceButtons.push_back(std::move(button));
                    ++index;
                }
            } else if (type == "variable") {
                // Restore paths already carry the materialized variables in
                // GameState. Replaying set/add components would apply node
                // entry side effects a second time.
                if (!replayEntryEffects) continue;
                std::string varKey = data.value("key", "");
                std::string varVal = data.value("value", "");
                std::string op = data.value("operation", "set");
                if (!varKey.empty()) {
                    if (op == "add" && m_luaSandbox) {
                        double cur = m_luaSandbox->getGlobalNumber(varKey, 0.0);
                        const double delta = std::stod(varVal);
                        double res = cur + delta;
                        if (!std::isfinite(cur) || !std::isfinite(delta) || !std::isfinite(res)) {
                            throw std::invalid_argument("Variable add requires finite numeric values");
                        }
                        m_luaSandbox->setGlobalNumber(varKey, res);
                        if (m_gameState) {
                            m_gameState = Rowl::State::GameState::createNextState(
                                m_gameState, m_storyRuntime.currentNodeId(), varKey,
                                std::to_string(res));
                        }
                    } else {
                        setScriptVariable(varKey, varVal);
                    }
                }
            } else if (type == "camera") {
                if (m_window && m_window->getCamera()) {
                    float zoom = data.value("zoom", 1.0f);
                    float x = data.value("x", 960.0f);
                    float y = data.value("y", 540.0f);
                    float rot = data.value("rotation", 0.0f);
                    float panDuration = data.value("pan_duration", 0.0f);
                    float zoomDuration = data.value("zoom_duration", 0.0f);
                    std::string easingStr = data.value("easing", "ease_in_out");

                    Rowl::Render::CameraEasing easing = Rowl::Render::CameraEasing::EaseInOutCubic;
                    if (easingStr == "linear") easing = Rowl::Render::CameraEasing::Linear;
                    else if (easingStr == "ease_in") easing = Rowl::Render::CameraEasing::EaseInQuad;
                    else if (easingStr == "ease_out") easing = Rowl::Render::CameraEasing::EaseOutQuad;
                    else if (easingStr == "smooth_step") easing = Rowl::Render::CameraEasing::SmoothStep;

                    if (panDuration > 0.0f) {
                        m_window->getCamera()->panTo(x, y, panDuration, easing);
                    } else {
                        m_window->getCamera()->setPosition(x, y);
                    }

                    if (zoomDuration > 0.0f) {
                        m_window->getCamera()->zoomTo(zoom, zoomDuration, easing);
                    } else {
                        m_window->getCamera()->setZoom(zoom);
                    }

                    m_window->getCamera()->setRotation(rot);
                    if (replayEntryEffects && data.contains("shake_preset") && !data["shake_preset"].get<std::string>().empty() && data["shake_preset"].get<std::string>() != "none") {
                        std::string preset = data["shake_preset"].get<std::string>();
                        float mult = data.value("shake_intensity_multiplier", 1.0f);
                        float durOverride = data.value("shake_duration_override", 0.0f);
                        if (data.contains("shake_intensity") && data.value("shake_intensity", 0.0f) > 0.0f) {
                            float intensity = data.value("shake_intensity", 0.0f);
                            float duration = data.value("shake_duration", 0.5f);
                            float freq = data.value("shake_frequency", 25.0f);
                            float damping = data.value("shake_damping", 1.0f);
                            float dirX = data.value("shake_dir_x", 1.0f);
                            float dirY = data.value("shake_dir_y", 1.0f);
                            Rowl::Render::CameraShakePreset presetEnum = Rowl::Render::CameraShakePreset::Custom;
                            if (preset == "subtle") presetEnum = Rowl::Render::CameraShakePreset::Subtle;
                            else if (preset == "earthquake") presetEnum = Rowl::Render::CameraShakePreset::Earthquake;
                            else if (preset == "explosion") presetEnum = Rowl::Render::CameraShakePreset::Explosion;
                            else if (preset == "heartbeat" || preset == "pulse") presetEnum = Rowl::Render::CameraShakePreset::Heartbeat;
                            m_window->getCamera()->shakeWithProfile(presetEnum, intensity, duration, freq, damping, dirX, dirY);
                        } else {
                            m_window->getCamera()->shakePreset(preset, mult, durOverride);
                        }
                    } else if (replayEntryEffects && data.contains("shake_intensity") && data.contains("shake_duration")) {
                        float intensity = data.value("shake_intensity", 0.0f);
                        float duration = data.value("shake_duration", 0.0f);
                        float freq = data.value("shake_frequency", 25.0f);
                        float damping = data.value("shake_damping", 2.0f);
                        float dirX = data.value("shake_dir_x", 1.0f);
                        float dirY = data.value("shake_dir_y", 1.0f);
                        m_window->getCamera()->shakeWithProfile(Rowl::Render::CameraShakePreset::Custom, intensity, duration, freq, damping, dirX, dirY);
                    }
                }
            } else if (type == "transition" || type == "screen_fx" || type == "visual_fx") {
                if (!replayEntryEffects) continue;
                if (data.contains("kind")) {
                    std::string kind = data.value("kind", "crossfade");
                    float duration = data.value("duration", 1.0f);
                    std::string colorHex = data.value("color", "#000000");
                    if (m_window && kind != "none") {
                        m_window->startTransition(kind, duration, colorHex);
                    }
                }
                if (m_window) {
                    if (data.value("flash_enabled", false) || (data.contains("flash_duration") && data.value("flash_duration", 0.0f) > 0.0f)) {
                        std::string flashColor = data.value("flash_color", "#FFFFFF");
                        float flashDuration = data.value("flash_duration", 0.5f);
                        float flashIntensity = data.value("flash_intensity", 1.0f);
                        m_window->triggerScreenFlashHex(flashColor, flashDuration, flashIntensity);
                    }
                    if (data.value("tint_enabled", false) || data.contains("tint_color") || data.contains("tint_opacity")) {
                        std::string tintColor = data.value("tint_color", "#000000");
                        float tintOpacity = data.value("tint_opacity", 0.0f);
                        if (tintOpacity > 0.001f) {
                            m_window->setScreenTintHex(tintColor, tintOpacity);
                        } else {
                            m_window->clearScreenTint();
                        }
                    }
                    if (data.value("vignette_enabled", false) || data.contains("vignette_intensity")) {
                        float vIntensity = data.value("vignette_intensity", 0.0f);
                        float vRadius = data.value("vignette_radius", 0.75f);
                        std::string vColor = data.value("vignette_color", "#000000");
                        m_window->setVignette(vIntensity, vRadius, vColor);
                    }
                }
            } else if (type == "script") {
                pendingScripts.push_back(data);
            }
        }

        if (m_activeCharacters.empty()) {
            m_activeCharacter = "";
        }

        if (m_activeDialogues.empty() && m_hasDialogueBox) {
            m_activeDialogues.push_back(m_activeDialogueData);
        }

        for (const auto& data : pendingAudioComponents) {
            if (!replayEntryEffects) continue;
            std::string dsp = data.value("dsp_filter", "Normal");
            std::string bgm = data.value("bgm_track", "");
            std::string sfx = data.value("sfx_track", "");
            float vol = data.value("volume", 1.0f);
            ROWL_LOG_INFO("[Audio] Applied DSP Filter from Component: " + dsp);
            if (!m_audio) continue;
            if (dsp == "Cave" || dsp == "CaveReverb") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::CaveReverb);
            } else if (dsp == "Telephone") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);
            } else if (dsp == "Underwater" || dsp == "UnderwaterLowPass") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::UnderwaterLowPass);
            } else {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
            }
            m_audio->setBgmVolume(vol);
            if (!bgm.empty() && bgm != m_audio->getCurrentBgmPath()) {
                std::string transition = data.value("bgm_transition", "project_default");
                if (transition == "project_default") transition = m_defaultBgmTransition;
                float duration = data.value("bgm_transition_duration_seconds", 0.0f);
                if (!std::isfinite(duration) || duration < 0.0f || duration > 60.0f) duration = 0.0f;
                if (duration == 0.0f) duration = m_defaultBgmTransitionDurationSeconds;
                const auto kind = transition == "crossfade" ? Rowl::Audio::BgmTransitionKind::Crossfade :
                                  transition == "fade" ? Rowl::Audio::BgmTransitionKind::Fade :
                                                         Rowl::Audio::BgmTransitionKind::Instant;
                m_audio->playBgm(bgm, kind, duration);
            }
            // SFX are node-entry events. Component updates also happen for
            // editor preview refreshes, so playing here unconditionally made
            // a property edit repeat the sound. A node may still play several
            // SFX components, but only once for each entry.
            if (!sfx.empty() && m_isPlaying &&
                m_lastSfxPlaybackNodeId != m_storyRuntime.currentNodeId())
                m_audio->playAudio(sfx, Rowl::Audio::AudioChannelType::Sfx);
        }

        if (m_isPlaying && !pendingAudioComponents.empty())
            m_lastSfxPlaybackNodeId = m_storyRuntime.currentNodeId();

        activateScripts(pendingScripts, replayEntryEffects);

        if (replayEntryEffects && m_audio && (!pendingAudioComponents.empty()) && m_gameState) {
            std::string filter = "Normal";
            switch (m_audio->getActiveFilter()) {
                case Rowl::Audio::DSPFilterType::CaveReverb: filter = "CaveReverb"; break;
                case Rowl::Audio::DSPFilterType::Telephone: filter = "Telephone"; break;
                case Rowl::Audio::DSPFilterType::UnderwaterLowPass: filter = "UnderwaterLowPass"; break;
                case Rowl::Audio::DSPFilterType::Normal: break;
            }
            m_gameState = Rowl::State::GameState::createNextStateWithAudio(
                m_gameState, m_storyRuntime.currentNodeId(), m_activeBackground,
                m_audio->getCurrentBgmPath(), m_audio->getBgmVolume(),
                m_audio->isBgmPlaying(), filter,
                // #86: sahne-ses commit'i tam mikseri damgalar (bgm dışı
                // kazançlar başka türlü state'e hiç inmezdi).
                m_audio->getMasterVolume(), m_audio->getSfxVolume(),
                m_audio->getVoiceVolume());
        }

        // Sync voice blip default settings from character if dialogue sound is empty or uses defaults
        for (auto& dlg : m_activeDialogues) {
            for (const auto& ch : m_activeCharacters) {
                // Match if single character in scene or speaker matches character sprite
                bool speakerMatch = (m_activeCharacters.size() == 1) ||
                    (!dlg.speaker.empty() && !ch.sprite.empty() &&
                     (ch.sprite.find(dlg.speaker) != std::string::npos ||
                      dlg.speaker.find(ch.sprite) != std::string::npos));

                if (speakerMatch) {
                    if (dlg.typewriterSound.empty() && !ch.voiceBlipSound.empty()) {
                        dlg.typewriterSound = ch.voiceBlipSound;
                    }
                    if (dlg.voiceBlipPitch == 1.0f && ch.voiceBlipPitch != 1.0f) {
                        dlg.voiceBlipPitch = ch.voiceBlipPitch;
                    }
                    if (dlg.voiceBlipPitchVariance == 0.08f && ch.voiceBlipPitchVariance != 0.08f) {
                        dlg.voiceBlipPitchVariance = ch.voiceBlipPitchVariance;
                    }
                    if (dlg.voiceBlipCadence == 1 && ch.voiceBlipCadence != 1) {
                        dlg.voiceBlipCadence = ch.voiceBlipCadence;
                    }
                    break;
                }
            }
        }
        if (!m_activeDialogues.empty()) {
            m_activeDialogueData = m_activeDialogues[0];
        }

        if (replayEntryEffects) recordActiveDialogueHistory();

        ROWL_LOG_INFO("Scene Updated (Components) → " + std::to_string(comps.size()) +
                      " comps, " + std::to_string(m_activeCharacters.size()) + " chars, " +
                      std::to_string(m_activeDialogues.size()) + " dlgs, HasBg: " +
                      (m_hasBackground ? "true" : "false") + ", HasDlg: " +
                      (m_hasDialogueBox ? "true" : "false"));
    } catch (const std::exception& e) {
        restorePreviousState();
        ROWL_LOG_ERROR("Failed to parse components JSON: " + std::string(e.what()));
    }
}

bool Engine::parseStoryGraphJson(const std::string& jsonContent) {
    auto parseResult = StoryGraphParser::parse(jsonContent);
    if (!parseResult.succeeded()) {
        m_storyRuntime.recordLoadFailure(std::move(parseResult.message));
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        return false;
    }

    if (!parseResult.document.nodes.contains(parseResult.document.startNodeId)) {
        m_storyRuntime.recordLoadFailure(
            "Story graph parser returned an invalid start node; the active graph was preserved.");
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        return false;
    }

    // Parsing is deliberately transactional. Only a complete, validated
    // document can replace the currently playable graph.
    if (!m_storyRuntime.commit(std::move(parseResult.document))) {
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        return false;
    }

    // Loading a graph is a new story session. Keeping a previous graph's
    // history here could make Save/Load or rewind jump into another graph.
    m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.currentNodeId());
    m_lastSfxPlaybackNodeId = 0;
    if (m_luaSandbox) m_luaSandbox->clearVariables();

    const auto& startNode = *m_storyRuntime.currentNode();
    if (!startNode.components.empty()) {
        nlohmann::json componentsJson = nlohmann::json::array();
        for (const auto& component : startNode.components) {
            componentsJson.push_back({
                {"type", component.type},
                {"id", component.id},
                {"enabled", component.enabled},
                {"data", component.data}
            });
        }
        updateSceneFromComponents(componentsJson);
    } else {
        // B7 (#38): the legacy branch skipped the script teardown the
        // component branch performs (status-clear + deactivate above) — a
        // lingering entity script kept running headless against the new
        // graph's globals. Mirror the teardown before painting the scene.
        m_scriptRuntimeStatuses.clear();
        deactivateScripts();
        updateActiveScene(
            startNode.speaker, startNode.dialogue, startNode.background,
            startNode.backgroundX, startNode.backgroundY,
            startNode.backgroundWidth, startNode.backgroundHeight,
            startNode.character, startNode.characterX, startNode.characterY,
            startNode.characterWidth, startNode.characterHeight,
            startNode.dialogueBoxX, startNode.dialogueBoxY,
            startNode.dialogueBoxWidth, startNode.dialogueBoxHeight);
    }

    ROWL_LOG_INFO("Story graph loaded: " + std::to_string(m_storyRuntime.size()) +
                  " nodes. Start node #" + std::to_string(m_storyRuntime.currentNodeId()));
    return true;
}

bool Engine::loadStoryGraphFromPath(const std::string& jsonPath) {
    m_storyRuntime.clearLoadError();
    std::error_code fileError;
    // Tur-12: jsonPath is UTF-8 (C ABI contract). The implicit
    // path-from-string ctor would reinterpret it in the ANSI codepage on
    // Windows, so a graph under a non-ASCII directory silently missed
    // (CI-11: CJK fixture unloaded -> metadata fixture empty).
    const std::filesystem::path graphPath = Rowl::Platform::pathFromUtf8(jsonPath);
    if (!std::filesystem::exists(graphPath, fileError) || !std::filesystem::is_regular_file(graphPath, fileError) || fileError) {
        m_storyRuntime.recordLoadFailure(
            "Story graph is missing or not a regular file: " + jsonPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::FileNotFound,
                            m_storyRuntime.lastLoadError(), "load_story_graph_path", jsonPath);
        return false;
    }
    if (std::filesystem::file_size(graphPath, fileError) > kMaxStoryJsonBytes || fileError) {
        m_storyRuntime.recordLoadFailure(
            "Story graph exceeds the maximum size limit: " + jsonPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::FileTooLarge,
                            m_storyRuntime.lastLoadError(), "load_story_graph_path", jsonPath);
        return false;
    }
    // Tur-12: open via the wide path, never the narrow bytes (ANSI
    // fopen cannot name non-ASCII directories on Windows).
    std::ifstream f(graphPath);
    if (!f.is_open()) {
        m_storyRuntime.recordLoadFailure("Cannot open story graph: " + jsonPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::IoError,
                            m_storyRuntime.lastLoadError(), "load_story_graph_path", jsonPath);
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    if (!parseStoryGraphJson(content)) {
        if (m_storyRuntime.lastLoadError().empty()) {
            m_storyRuntime.recordLoadFailure(
                "Story graph JSON was rejected; the active graph was preserved.");
        }
        RuntimeErrorCode errCode =
            (m_storyRuntime.lastLoadError().find("parse error") != std::string::npos)
            ? RuntimeErrorCode::ParseError
            : RuntimeErrorCode::ValidationError;
        m_context->setError(errCode, m_storyRuntime.lastLoadError(),
                            "load_story_graph_path", jsonPath);
        return false;
    }
    m_context->setSuccess("load_story_graph_path", jsonPath);
    return true;
}

bool Engine::loadStoryGraphFromVfs(const std::string& vfsPath) {
    m_storyRuntime.clearLoadError();
    if (vfsPath.empty()) {
        m_storyRuntime.recordLoadFailure("Story graph VFS path is empty");
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::FileNotFound,
                            m_storyRuntime.lastLoadError(), "load_story_graph_vfs", vfsPath);
        return false;
    }

    const auto host = getPlatformHost();
    if (!host) {
        m_storyRuntime.recordLoadFailure("Runtime platform host is unavailable");
        m_context->setError(RuntimeErrorCode::StateError,
                            m_storyRuntime.lastLoadError(), "load_story_graph_vfs", vfsPath);
        return false;
    }
    auto stream = host->openAssetStream(vfsPath);
    if (!stream) {
        m_storyRuntime.recordLoadFailure("Story graph is missing from VFS: " + vfsPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::FileNotFound,
                            m_storyRuntime.lastLoadError(), "load_story_graph_vfs", vfsPath);
        return false;
    }

    return loadStoryGraphFromAssetStream(vfsPath, std::move(stream));
}

bool Engine::loadStoryGraphFromAssetStream(
    const std::string& assetPath,
    std::unique_ptr<std::istream> stream) {
    std::string content;
    content.reserve(64 * 1024);
    std::array<char, 8192> buffer{};
    while (*stream) {
        stream->read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto bytesRead = stream->gcount();
        if (bytesRead <= 0) break;
        content.append(buffer.data(), static_cast<std::size_t>(bytesRead));
        if (content.size() > kMaxStoryJsonBytes) {
            m_storyRuntime.recordLoadFailure(
                "Story graph VFS content exceeds the size limit: " + assetPath);
            ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
            m_context->setError(RuntimeErrorCode::FileTooLarge,
                                m_storyRuntime.lastLoadError(),
                                "load_story_graph_vfs", assetPath);
            return false;
        }
    }
    if (stream->bad()) {
        m_storyRuntime.recordLoadFailure(
            "Story graph VFS stream could not be read: " + assetPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::IoError,
                            m_storyRuntime.lastLoadError(),
                            "load_story_graph_vfs", assetPath);
        return false;
    }
    if (content.empty()) {
        m_storyRuntime.recordLoadFailure(
            "Story graph VFS content is empty: " + assetPath);
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        m_context->setError(RuntimeErrorCode::ParseError,
                            m_storyRuntime.lastLoadError(),
                            "load_story_graph_vfs", assetPath);
        return false;
    }

    if (!parseStoryGraphJson(content)) {
        if (m_storyRuntime.lastLoadError().empty()) {
            m_storyRuntime.recordLoadFailure(
                "Story graph JSON from VFS was rejected; the active graph was preserved.");
        }
        RuntimeErrorCode errCode =
            (m_storyRuntime.lastLoadError().find("parse error") != std::string::npos)
            ? RuntimeErrorCode::ParseError
            : RuntimeErrorCode::ValidationError;
        m_context->setError(errCode, m_storyRuntime.lastLoadError(),
                            "load_story_graph_vfs", assetPath);
        return false;
    }
    m_context->setSuccess("load_story_graph_vfs", assetPath);
    return true;
}

bool Engine::loadStoryGraphFile() {
    // 1. Try VFS resolution first (isolated project mounts, packages, or loose assets)
    if (const auto host = getPlatformHost()) {
        const std::vector<std::string> vfsCandidates = {
            "json/full_story_graph.json",
            "full_story_graph.json",
            "Assets/json/full_story_graph.json",
            "Assets/full_story_graph.json"
        };
        for (const auto& candidate : vfsCandidates) {
            auto stream = host->openAssetStream(candidate);
            if (stream && loadStoryGraphFromAssetStream(candidate, std::move(stream))) {
                return true;
            }
        }
    }

    // 2. Physical search path fallback (explicit asset root only; no parent traversal).
    // Editor and player resolve the project via RowlEngine_SetProjectDirectory/VFS.
    std::vector<std::string> searchPaths = {
        "Assets/json/full_story_graph.json",
        "Assets/full_story_graph.json"
    };
    for (const auto& p : searchPaths) {
        // A2a-tur2: ec overload + wide path — the throwing narrow overload
        // dies on non-ASCII roots on Windows instead of answering false.
        std::error_code probeError;
        const std::filesystem::path probePath = Rowl::Platform::pathFromUtf8(p);
        if (std::filesystem::exists(probePath, probeError) && !probeError) {
            // #122: surface the attempt's verdict instead of swallowing it —
            // loadStoryGraphFromPath already records parse/IO failures.
            return loadStoryGraphFromPath(p);
        }
    }
    if (loadActiveStoryFile()) {
        return true;
    }
    // #122: total miss — no candidate produced a graph. Fail loud through
    // the story/context error channels instead of booting storyless in
    // silence. Engine::init keeps its true contract (bare-init editor/test
    // flows are unaffected); the diagnosis is observable, not fatal.
    recordStoryGraphMiss(
        "No story graph found: probed VFS candidates "
        "(json/full_story_graph.json, full_story_graph.json, "
        "Assets/json/full_story_graph.json, Assets/full_story_graph.json), "
        "the physical fallbacks (Assets/json/full_story_graph.json, "
        "Assets/full_story_graph.json), and the active-story overlay. "
        "Set a project directory containing a story graph.");
    return false;
}

/// #122: records a SPECIFIC story-boot diagnosis, always overwriting the
/// previous channel content (chronological verdicts; see header).
void Engine::recordStoryGraphCause(const std::string& detail) {
    m_storyRuntime.recordLoadFailure(detail);
    ROWL_LOG_ERROR(detail);
    m_context->setError(RuntimeErrorCode::FileNotFound, detail,
                        "load_story_graph", "");
}

/// #122: records a story-boot miss into the story + context error channels.
/// Preserves a more specific error already present (remount root cause,
/// parse rejection); a later successful commit() clears the channel.
void Engine::recordStoryGraphMiss(const std::string& detail) {
    // Preserve the root cause: a remount failure (or a parse rejection)
    // already in the channel is more specific than this total-miss note.
    // A later successful commit() clears the channel.
    if (!m_storyRuntime.lastLoadError().empty()) {
        ROWL_LOG_ERROR(m_storyRuntime.lastLoadError());
        return;
    }
    m_storyRuntime.recordLoadFailure(detail);
    ROWL_LOG_ERROR(detail);
    m_context->setError(RuntimeErrorCode::FileNotFound, detail,
                        "load_story_graph", "");
}

bool Engine::loadActiveStoryFile() {
    // 1. Try VFS resolution first
    auto* vfsPtr = getVfs();
    if (vfsPtr) {
        const std::vector<std::string> vfsCandidates = {
            "json/active_story.json",
            "active_story.json",
            "Assets/json/active_story.json",
            "Assets/active_story.json"
        };
        for (const auto& candidate : vfsCandidates) {
            if (vfsPtr->exists(candidate)) {
                const std::string content = vfsPtr->readString(candidate);
            if (!content.empty() && content.size() <= kMaxStoryJsonBytes) {
                try {
                    nlohmann::json data = nlohmann::json::parse(content);
                    uint64_t nodeId = data.value("node_id", static_cast<uint64_t>(0));
                    // A2a-tur2: a dangling id used to move the cursor in total
                    // silence (scene sync just skipped). Audible now.
                    if (nodeId != 0 && !m_storyRuntime.setCurrentNodeId(nodeId)) {
                        ROWL_LOG_WARN("Active story from VFS " + candidate + " points at node #" +
                                      std::to_string(nodeId) + " absent from the loaded graph");
                    }

                    if (data.contains("components") && data["components"].is_array()) {
                        updateSceneFromComponents(data["components"]);
                    } else {
                        updateActiveScene(
                            data.value("speaker",          std::string{}),
                            data.value("dialogue",         std::string{}),
                            data.value("background",       std::string{}),
                            data.value("background_x",     0.0f),
                            data.value("background_y",     0.0f),
                            data.value("background_width",  1920.0f),
                            data.value("background_height", 1080.0f),
                            data.value("character",        std::string{}),
                            data.value("character_x",      1440.0f),
                            data.value("character_y",      340.0f),
                            data.value("character_width",  360.0f),
                            data.value("character_height", 540.0f),
                            data.value("dialogue_box_x",   80.0f),
                            data.value("dialogue_box_y",   860.0f),
                            data.value("dialogue_box_width",1760.0f),
                            data.value("dialogue_box_height",180.0f)
                        );
                    }
                    ROWL_LOG_INFO("Loaded active story from VFS: " + candidate);
                    m_context->setSuccess("load_active_story_vfs", candidate);
                    return true;
                } catch (const std::exception& e) {
                    ROWL_LOG_ERROR("Active story load error from VFS " + candidate + ": " + e.what());
                }
            }
        }
    }
}

    // Explicit asset root only; no parent traversal (see loadStoryGraphFile).
    std::vector<std::string> searchPaths = {
        "Assets/json/active_story.json",
        "Assets/active_story.json"
    };

    for (const auto& path : searchPaths) {
        // A2a-tur2: wide path + ec overloads + wide open — the implicit
        // narrow conversion dies on non-ASCII roots on Windows (same class
        // as loadStoryGraphFromPath's Tur-12 hardening).
        const std::filesystem::path widePath = Rowl::Platform::pathFromUtf8(path);
        std::error_code fileError;
        if (std::filesystem::is_regular_file(widePath, fileError) && !fileError &&
            std::filesystem::file_size(widePath, fileError) <= kMaxStoryJsonBytes && !fileError) {
            std::ifstream f(widePath);
            if (f.is_open()) {
                try {
                    nlohmann::json data = nlohmann::json::parse(f);
                    uint64_t nodeId = data.value("node_id", static_cast<uint64_t>(0));
                    // A2a-tur2: same dangle-audibility as the VFS path above.
                    if (nodeId != 0 && !m_storyRuntime.setCurrentNodeId(nodeId)) {
                        ROWL_LOG_WARN("Active story file " + path + " points at node #" +
                                      std::to_string(nodeId) + " absent from the loaded graph");
                    }

                    if (data.contains("components") && data["components"].is_array()) {
                        updateSceneFromComponents(data["components"]);
                    } else {
                        updateActiveScene(
                            data.value("speaker",          std::string{}),
                            data.value("dialogue",         std::string{}),
                            data.value("background",       std::string{}),
                            data.value("background_x",     0.0f),
                            data.value("background_y",     0.0f),
                            data.value("background_width",  1920.0f),
                            data.value("background_height", 1080.0f),
                            data.value("character",        std::string{}),
                            data.value("character_x",      1440.0f),
                            data.value("character_y",      340.0f),
                            data.value("character_width",  360.0f),
                            data.value("character_height", 540.0f),
                            data.value("dialogue_box_x",   80.0f),
                            data.value("dialogue_box_y",   860.0f),
                            data.value("dialogue_box_width",1760.0f),
                            data.value("dialogue_box_height",180.0f)
                        );
                    }
                    ROWL_LOG_INFO("Loaded active story node #" +
                                  std::to_string(m_storyRuntime.currentNodeId()) + " from: " + path);
                    return true;
                } catch (const std::exception& e) {
                    ROWL_LOG_ERROR("Active story load error in " + path + ": " + e.what());
                }
            }
        }
    }
    // No overlay applied — not a failure; bare sessions keep the cursor.
    return false;
}

void Engine::step(float deltaTime) {
    if (!m_window) return;

    // C API hosts can pass arbitrary frame durations. Keep time-dependent
    // systems deterministic and avoid poisoning typewriter state with NaN.
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    } else if (deltaTime > 0.25f) {
        deltaTime = 0.25f;
    }

    const auto platformHost = getPlatformHost();
    if (platformHost && platformHost->lifecycleState() == Rowl::Platform::LifecycleState::Stopping) {
        applyAudioSuspension(platformHost);
        m_isRunning = false;
        return;
    }
    applyAudioSuspension(platformHost);
    if (platformHost && platformHost->lifecycleState() == Rowl::Platform::LifecycleState::Suspended) {
        return;
    }
    // Faz 2 Dilim 4 total playtime: only live, unpaused play counts.
    if (m_isPlaying && !m_paused) {
        m_playtimeSeconds += deltaTime;
    }
    if (platformHost) {
        for (const auto& event : platformHost->takeInputEvents()) {
            handleRuntimeInput(event);
        }
    }

    bool shouldQuit = false;
    m_window->pollEvents(shouldQuit);
    if (shouldQuit) {
        m_isRunning = false;
        return;
    }

    // Process-wide events the per-window dispatcher collected: audio-device
    // hotplug rebuilds output streams, minimize suspends output, restore
    // resumes it. Playback intent is preserved in all three cases.
    if (m_audio) {
        // #75: offscreen runtimes register no window, so pollEvents above
        // early-returns and the dispatch pin stays unclaimed — takeGlobal-
        // Events below would no-op without touching SDL_PollEvent and global
        // audio-device events would sit dead in the SDL queue. Claim-and-
        // pump first; the take's own internal pump then drains nothing new.
        // Visible/embedded order (takeEvents-pump, takeGlobalEvents-pump)
        // is untouched.
        if (m_window->isOffscreen()) {
            Rowl::Platform::SdlEventDispatcher::pumpOnly();
        }
        for (const SDL_Event& event : Rowl::Platform::SdlEventDispatcher::takeGlobalEvents()) {
            switch (event.type) {
                case SDL_EVENT_AUDIO_DEVICE_ADDED:
                case SDL_EVENT_AUDIO_DEVICE_REMOVED:
                case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
                    m_audio->handleDeviceEvent(event.type);
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                    m_windowAudioSuspended = true;
                    break;
                case SDL_EVENT_WINDOW_MAXIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                    m_windowAudioSuspended = false;
                    break;
                default:
                    break;
            }
        }
        applyAudioSuspension(platformHost);
    }

    m_window->update(deltaTime);

    // MS-6: pause freezes story simulation (typewriter, auto-advance,
    // scripts, entities). Rendering, audio upkeep, and the menu overlay below
    // keep running so the pause screen stays alive.
    if (!m_paused) {
    const auto* fontRenderer = m_window->getFontRenderer();
    for (auto& dlg : m_activeDialogues) {
        dlg.isPlaying = m_isPlaying;
        // MS-6: typewriter progression follows presentation + real dt, not the
        // play state, so preview stepping animates elapsed too. Audible blips
        // stay play-gated so paused previews remain silent.
        if (dlg.typewriterEnabled && dlg.textSpeed > 0) {
            dlg.elapsedTypewriterTime += deltaTime * m_textSpeedMultiplier;

            const auto shaped = shapeDialogue(fontRenderer, dlg);
            const size_t currentVisible = Rowl::Text::evaluateReveal(
                *shaped, dlg.elapsedTypewriterTime, dlg.textSpeed).visibleUnits;

            if (currentVisible > dlg.lastBlipCodepointIndex && m_audio) {
                size_t startChar = dlg.lastBlipCodepointIndex;
                size_t endChar = currentVisible;
                dlg.lastBlipCodepointIndex = currentVisible;

                for (size_t charIdx = startChar; charIdx < endChar; ++charIdx) {
                    if (dlg.voiceBlipCadence > 1 && (charIdx % dlg.voiceBlipCadence) != 0) {
                        continue;
                    }

                    const uint32_t cp = shaped->revealUnits[charIdx].representativeCodepoint;

                    if (dlg.voiceBlipSkipPunctuation && isPunctuationOrWhitespace(cp)) {
                        continue;
                    }

                    float hash = static_cast<float>(((charIdx * 2654435761u) ^ (cp * 2246822519u)) % 1000) / 1000.0f;
                    float pitchMod = dlg.voiceBlipPitch + (hash * 2.0f - 1.0f) * dlg.voiceBlipPitchVariance;
                    pitchMod = std::clamp(pitchMod, 0.25f, 4.0f);

                    Rowl::Audio::AudioChannelType ch = (dlg.voiceBlipChannel == 2)
                        ? Rowl::Audio::AudioChannelType::Sfx
                        : Rowl::Audio::AudioChannelType::Voice;

                    // MS-6: blip bookkeeping advances in every mode, but paused
                    // previews stay silent.
                    if (m_isPlaying) m_audio->playVoiceBlip(dlg.typewriterSound, pitchMod, dlg.voiceBlipVolume, ch);
                    break;
                }
            }
        }
    }
    m_activeDialogueData.isPlaying = m_isPlaying;
    // MS-6: legacy single-dialogue progression follows the same decoupled rule.
    if (m_activeDialogueData.typewriterEnabled && m_activeDialogueData.textSpeed > 0) {
        if (!m_activeDialogues.empty()) {
            m_activeDialogueData.elapsedTypewriterTime = m_activeDialogues[0].elapsedTypewriterTime;
            m_activeDialogueData.lastBlipCodepointIndex = m_activeDialogues[0].lastBlipCodepointIndex;
        } else {
            m_activeDialogueData.elapsedTypewriterTime += deltaTime * m_textSpeedMultiplier;
            if (m_audio) {
                const auto shaped = shapeDialogue(fontRenderer, m_activeDialogueData);
                const size_t currentVisible = Rowl::Text::evaluateReveal(
                    *shaped, m_activeDialogueData.elapsedTypewriterTime,
                    m_activeDialogueData.textSpeed).visibleUnits;

                if (currentVisible > m_activeDialogueData.lastBlipCodepointIndex) {
                    size_t startChar = m_activeDialogueData.lastBlipCodepointIndex;
                    size_t endChar = currentVisible;
                    m_activeDialogueData.lastBlipCodepointIndex = currentVisible;

                    for (size_t charIdx = startChar; charIdx < endChar; ++charIdx) {
                        if (m_activeDialogueData.voiceBlipCadence > 1 && (charIdx % m_activeDialogueData.voiceBlipCadence) != 0) {
                            continue;
                        }
                        const uint32_t cp = shaped->revealUnits[charIdx].representativeCodepoint;
                        if (m_activeDialogueData.voiceBlipSkipPunctuation && isPunctuationOrWhitespace(cp)) {
                            continue;
                        }
                        float hash = static_cast<float>(((charIdx * 2654435761u) ^ (cp * 2246822519u)) % 1000) / 1000.0f;
                        float pitchMod = m_activeDialogueData.voiceBlipPitch + (hash * 2.0f - 1.0f) * m_activeDialogueData.voiceBlipPitchVariance;
                        pitchMod = std::clamp(pitchMod, 0.25f, 4.0f);
                        Rowl::Audio::AudioChannelType ch = (m_activeDialogueData.voiceBlipChannel == 2)
                            ? Rowl::Audio::AudioChannelType::Sfx
                            : Rowl::Audio::AudioChannelType::Voice;
                        // MS-6: paused previews stay silent (see dialogues loop).
                        if (m_isPlaying) m_audio->playVoiceBlip(m_activeDialogueData.typewriterSound, pitchMod, m_activeDialogueData.voiceBlipVolume, ch);
                        break;
                    }
                }
            }
        }
    }

    bool autoAdvanceEnabled = false;
    float autoAdvanceDelay = 0.0f;
    for (const auto& dialogue : m_activeDialogues) {
        if (dialogue.autoAdvance) {
            autoAdvanceEnabled = true;
            // B6 (#4/#7): two individually [0,60]-clamped delays summed
            // unclamped (60+60=120s, double the documented ceiling). The sum
            // is clamped to the same ceiling; valid sub-ceiling sums unchanged.
            autoAdvanceDelay = std::clamp(
                std::max(autoAdvanceDelay, dialogue.autoAdvanceDelay + m_autoAdvanceDelayOffset),
                0.0f, 60.0f);
        }
    }
    const StoryNode* activeNode = m_storyRuntime.currentNode();
    if (m_isPlaying && autoAdvanceEnabled && m_activeChoiceButtons.empty() &&
        activeNode && !activeNode->nextNodes.empty() &&
        areActiveDialoguesComplete() && (!m_window || !m_window->isTransitionActive())) {
        m_autoAdvanceElapsed += deltaTime;
        if (m_autoAdvanceElapsed >= autoAdvanceDelay) {
            m_autoAdvanceElapsed = 0.0f;
            advanceToNextNode();
        }
    } else {
        m_autoAdvanceElapsed = 0.0f;
    }
    } // end MS-6 pause freeze of story simulation

    Rowl::Render::ComposedFrame frame;
    frame.hasBackground = m_hasBackground;
    frame.background = m_activeBackground;
    frame.backgroundX = m_activeBackgroundX;
    frame.backgroundY = m_activeBackgroundY;
    frame.backgroundWidth = m_activeBackgroundWidth;
    frame.backgroundHeight = m_activeBackgroundHeight;
    frame.characters = m_activeCharacters;
    frame.dialogues = m_activeDialogues;
    frame.choices = m_activeChoiceButtons;
    frame.backgroundRotation = m_activeBackgroundRotation;
    frame.backgroundParallaxX = m_activeBackgroundParallaxX;
    frame.backgroundParallaxY = m_activeBackgroundParallaxY;
    frame.backgroundOpacity = m_activeBackgroundOpacity;
    m_window->renderComposedFrame(frame);

    // Update & Render Entity-Component Scene (frozen while paused)
    if (m_scene) {
        if (!m_paused) m_scene->update(deltaTime);
        m_scene->render(m_window.get());
    }

    if (m_audio) {
        m_audio->update(deltaTime);
    }
    if (!m_paused && m_hasActiveScript && m_luaSandbox) {
        for (const auto& moduleId : m_activeScriptModuleIds) {
            if (!m_luaSandbox->callOptionalModuleFunction(moduleId, "on_update", deltaTime)) {
                markScriptStatus(moduleId, {}, "failed", m_luaSandbox->getLastError());
            }
        }
    }

    // MS-6: pause-menu overlay draws last so it sits above story + entities.
    if (m_paused) {
        m_window->renderPauseMenuOverlay(getPauseMenuView());
    }

    m_window->endFrame();
}

void Engine::setTextSpeedMultiplier(float multiplier) {
    // B6 (#2): bare std::clamp(NaN, 0.25, 4.0) returns NaN — the poisoned
    // multiplier then NaNs elapsedTypewriterTime every step and hits the
    // float-to-size_t UB cast in areActiveDialoguesComplete. Keep last-good.
    if (!std::isfinite(multiplier)) {
        ROWL_LOG_WARN("Non-finite text-speed multiplier rejected, keeping last value");
        return;
    }
    m_textSpeedMultiplier = std::clamp(multiplier, 0.25f, 4.0f);
}

void Engine::setAutoAdvanceDelayOffset(float seconds) {
    m_autoAdvanceDelayOffset = std::clamp(seconds, 0.0f, 60.0f);
}

void Engine::startTransition(const std::string& kind, float durationSeconds, const std::string& colorHex) {
    if (m_window) {
        m_window->startTransition(kind, durationSeconds, colorHex);
    }
}

void Engine::triggerCameraShakePreset(const std::string& preset, float intensityMultiplier, float durationOverride) {
    if (m_window && m_window->getCamera()) {
        m_window->getCamera()->shakePreset(preset, intensityMultiplier, durationOverride);
    }
}

void Engine::triggerCameraShakeProfile(float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY) {
    if (m_window && m_window->getCamera()) {
        m_window->getCamera()->shakeWithProfile(Rowl::Render::CameraShakePreset::Custom, intensity, durationSeconds, frequency, damping, dirX, dirY);
    }
}

float Engine::getCameraShakeOffsetX() const {
    return (m_window && m_window->getCamera()) ? m_window->getCamera()->getShakeOffsetX() : 0.0f;
}

float Engine::getCameraShakeOffsetY() const {
    return (m_window && m_window->getCamera()) ? m_window->getCamera()->getShakeOffsetY() : 0.0f;
}

void Engine::triggerScreenFlash(uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity) {
    if (m_window) {
        m_window->triggerScreenFlash(r, g, b, durationSeconds, intensity);
    }
}

void Engine::triggerScreenFlashHex(const std::string& colorHex, float durationSeconds, float intensity) {
    if (m_window) {
        m_window->triggerScreenFlashHex(colorHex, durationSeconds, intensity);
    }
}

bool Engine::isScreenFlashActive() const {
    return m_window ? m_window->isScreenFlashActive() : false;
}

void Engine::setScreenTint(uint8_t r, uint8_t g, uint8_t b, float opacity) {
    if (m_window) {
        m_window->setScreenTint(r, g, b, opacity);
    }
}

void Engine::setScreenTintHex(const std::string& colorHex, float opacity) {
    if (m_window) {
        m_window->setScreenTintHex(colorHex, opacity);
    }
}

void Engine::clearScreenTint() {
    if (m_window) {
        m_window->clearScreenTint();
    }
}

float Engine::getScreenTintOpacity() const {
    return m_window ? m_window->getScreenTintOpacity() : 0.0f;
}

void Engine::setVignette(float intensity, float radius, const std::string& colorHex) {
    if (m_window) {
        m_window->setVignette(intensity, radius, colorHex);
    }
}

float Engine::getVignetteIntensity() const {
    return m_window ? m_window->getVignetteIntensity() : 0.0f;
}

void Engine::run() {
    if (!m_initialized) {
        ROWL_LOG_ERROR("Engine run() called without initialization!");
        return;
    }

    // Bulgu #14 (HIGH): offscreen handle'da run() kesilemeyen sonsuz
    // donguye girerdi — initializeOffscreen m_eventWindowId setlemez,
    // pollEvents erken doner, SDL_EVENT_QUIT hicbir kayitli pencereye
    // yonlendirilmediginden m_isRunning false yazilamazdi. Fail-closed:
    // quit uretemeyen handle'da donguye girmeden don; shutdown cagrilmaz
    // (handle Step ile surulebilir kalir), kanala StateError islenir.
    // Embedded/standalone pencereler kayitli oldugu icin etkilenmez.
    if (!m_window || m_window->isOffscreen()) {
        ROWL_LOG_ERROR("Engine run() rejected: offscreen handle cannot produce "
                       "a quit event; drive it with step() instead.");
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Run requires a quit-capable window; offscreen "
                                "handles cannot produce quit — drive them with Step",
                                "run", "");
        }
        return;
    }

    ROWL_LOG_INFO("Entering standalone render loop...");
    setPlayState(true);

    auto lastTime = std::chrono::high_resolution_clock::now();
    while (m_isRunning) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0f)  dt = 0.0f;

        // step() internally calls pollEvents() and sets m_isRunning = false on quit
        step(dt);
    }

    ROWL_LOG_INFO("Engine render loop finished.");
    shutdown();
}

void Engine::deactivateScripts(bool callOnExit) {
    if (m_hasActiveScript && m_luaSandbox) {
        for (auto it = m_activeScriptModuleIds.rbegin(); it != m_activeScriptModuleIds.rend(); ++it) {
            if (callOnExit && !m_luaSandbox->callOptionalModuleFunction(*it, "on_exit")) {
                markScriptStatus(*it, {}, "failed", m_luaSandbox->getLastError());
            }
            m_luaSandbox->unloadModule(*it);
        }
    }
    m_activeScriptModuleIds.clear();
    m_hasActiveScript = false;
}

void Engine::activateScripts(const std::vector<nlohmann::json>& scripts,
                             bool callOnEnter) {
    if (!m_luaSandbox) return;
    for (std::size_t scriptIndex = 0; scriptIndex < scripts.size(); ++scriptIndex) {
        const auto& script = scripts[scriptIndex];
        std::string source = script.value("code", "");
        const std::string path = script.value("path", "");
        if (source.empty() && !path.empty()) {
            auto* vfsPtr = getVfs();
            if (!vfsPtr) {
                markScriptStatus((path + "#" + std::to_string(scriptIndex)), path, "failed",
                                 "Runtime VFS is unavailable");
                continue;
            }
            source = vfsPtr->readString(path);
            if (source.empty()) {
                ROWL_LOG_ERROR("Lua script asset could not be read: " + path);
                markScriptStatus((path + "#" + std::to_string(scriptIndex)), path, "failed",
                                 "Lua script asset could not be read");
                continue;
            }
        }
        if (source.empty()) {
            markScriptStatus("inline#" + std::to_string(scriptIndex), path, "failed",
                             "Lua script has no source code or asset path");
            continue;
        }
        const std::string moduleId = (path.empty() ? "inline" : path) + "#" + std::to_string(scriptIndex);
        if (!m_luaSandbox->loadModule(moduleId, source)) {
            ROWL_LOG_ERROR("Lua script activation failed" + (path.empty() ? std::string{} : ": " + path));
            markScriptStatus(moduleId, path, "failed", m_luaSandbox->getLastError());
            continue;
        }
        m_activeScriptModuleIds.push_back(moduleId);
        m_hasActiveScript = true;
        if (callOnEnter && !m_luaSandbox->callOptionalModuleFunction(moduleId, "on_enter")) {
            ROWL_LOG_ERROR("Lua on_enter callback failed" + (path.empty() ? std::string{} : ": " + path));
            markScriptStatus(moduleId, path, "failed", m_luaSandbox->getLastError());
        } else {
            markScriptStatus(moduleId, path, "running");
        }
    }
}

void Engine::markScriptStatus(const std::string& moduleId, const std::string& sourcePath,
                              const std::string& state, const std::string& error) {
    const auto existing = std::find_if(m_scriptRuntimeStatuses.begin(), m_scriptRuntimeStatuses.end(),
        [&](const ScriptRuntimeStatus& status) { return status.moduleId == moduleId; });
    if (existing != m_scriptRuntimeStatuses.end()) {
        if (!sourcePath.empty()) existing->sourcePath = sourcePath;
        existing->state = state;
        existing->lastError = error;
        return;
    }
    m_scriptRuntimeStatuses.push_back({moduleId, sourcePath, state, error});
}

const std::vector<Rowl::State::DialogueHistoryEntry>& Engine::getDialogueHistory() const {
    static const std::vector<Rowl::State::DialogueHistoryEntry> empty;
    return (m_gameState && m_gameState->dialogueHistory) ? *m_gameState->dialogueHistory : empty;
}

void Engine::recordActiveDialogueHistory() {
    if (!m_isPlaying || !m_gameState || m_storyRuntime.currentNodeId() == 0 ||
        m_lastRecordedDialogueNodeId == m_storyRuntime.currentNodeId() ||
        m_activeDialogues.empty()) {
        return;
    }
    std::vector<Rowl::State::DialogueHistoryEntry> entries;
    entries.reserve(m_activeDialogues.size());
    for (const auto& dialogue : m_activeDialogues) {
        if (!dialogue.dialogue.empty()) {
            entries.push_back(
                {m_storyRuntime.currentNodeId(), dialogue.speaker, dialogue.dialogue, true,
                 dialogue.contentId});
        }
    }
    if (!entries.empty()) {
        m_gameState = Rowl::State::GameState::withDialogueHistory(m_gameState, entries);
        m_lastRecordedDialogueNodeId = m_storyRuntime.currentNodeId();
    }
}

bool Engine::areActiveDialoguesComplete() const {
    // MS-6: a paused line renders full text (visibility keys off the per-line
    // playing flag) with frozen elapsed time, so for frame-staticity purposes
    // it counts as complete. This preserves the MS-4 dirty-frame gate for
    // paused previews. The advance path uses raw elapsed time instead, so
    // click-to-complete still applies to armed lines (see
    // completeTypewriterIfTyping).
    if (!m_isPlaying) return true;
    for (const auto& dialogue : m_activeDialogues) {
        if (!dialogue.typewriterEnabled || dialogue.textSpeed <= 0) continue;
        const auto total = Rowl::Render::FontRenderer::countCodepoints(dialogue.dialogue);
        const auto visible = static_cast<std::size_t>(
            (dialogue.elapsedTypewriterTime * 1000.0f) / static_cast<float>(dialogue.textSpeed));
        if (visible < total) return false;
    }
    return true;
}

bool Engine::isPreviewFrameStatic() const {
    // Conservative: without a window there is no frame to reason about, so
    // report activity and let the host attempt the (harmless no-op) copy.
    if (!m_window) return false;
    if (m_window->isTransitionActive()) return false;
    if (m_window->isScreenFlashActive()) return false;
    if (const auto* camera = m_window->getCamera()) {
        if (camera->isMoving()) return false;
    }
    if (!areActiveDialoguesComplete()) return false;
    // Entity scripts and component on_update callbacks can mutate visuals at
    // any tick; only a script/scene-free runtime is provably still.
    if (m_hasActiveScript) return false;
    if (m_scene && m_scene->getObjectCount() > 0) return false;
    return true;
}

void Engine::resetSessionProfile() {
    // D2 (#136/#137): shutdown->re-init aynı handle'da önceki oturumun
    // profilini yeni oturuma sızdırıyordu (oynatılmamışken playtime,
    // 4.0x hız, eski bgm-varsayılanı). Fabrika değerlerine döndür.
    m_isPlaying   = false;
    m_paused      = false;
    m_pauseConfirmQuit = false;
    m_pauseMode   = PauseMenuMode::Main;
    m_pauseSelected = 0;
    m_activeQuickSlot = 0;
    m_pauseSlotCacheValid = false;
    m_textSpeedMultiplier = 1.0f;
    m_autoAdvanceDelayOffset = 0.0f;
    m_defaultBgmTransition = "instant";
    m_defaultBgmTransitionDurationSeconds = 1.0f;
    m_playtimeSeconds = 0.0;
    m_autoAdvanceElapsed = 0.0f;
    // D2 (#127): shutdown sonrası canlı handle stale sahneyi servis
    // ediyordu. Sahne görünür durumunu taze-handle değerlerine çek
    // (header in-class default'larıyla birebir aynı).
    m_activeSpeaker    = "Evelyn";
    m_activeDialogue   = "Welcome to Rowl Engine!";
    m_activeBackground = "bg_beach_sunset.png";
    m_activeBackgroundX = 0.0f;
    m_activeBackgroundY = 0.0f;
    m_activeBackgroundWidth  = 1920.0f;
    m_activeBackgroundHeight = 1080.0f;
    m_activeBackgroundRotation = 0.0f;
    m_activeBackgroundParallaxX = 1.0f;
    m_activeBackgroundParallaxY = 1.0f;
    m_activeBackgroundOpacity   = 1.0f;
    m_activeCharacter  = "spr_evelyn.png";
    m_activeCharacterX = 1440.0f;
    m_activeCharacterY = 340.0f;
    m_activeCharacterWidth  = 360.0f;
    m_activeCharacterHeight = 540.0f;
    m_activeCharacterRotation = 0.0f;
    m_activeCharacters.clear();
    m_activeDialogueBoxX = 80.0f;
    m_activeDialogueBoxY = 860.0f;
    m_activeDialogueBoxWidth  = 1760.0f;
    m_activeDialogueBoxHeight = 180.0f;
    m_activeDialogueData = Rowl::Render::DialogueRenderData{};
    m_activeDialogues.clear();
    m_activeChoiceButtons.clear();
    m_hasActiveScript = false;
    m_activeScriptModuleIds.clear();
    m_hasBackground  = true;
    m_hasDialogueBox = true;
    m_lastRecordedDialogueNodeId = 0;
    m_lastSfxPlaybackNodeId = 0;
}

void Engine::shutdown() {
    // D2 (#126-rest): guard yalnız m_initialized'a bakıyordu; başarısız
    // init'in yarım-state'i (B1a sonrası kalamaz ama savunma-derinliği)
    // shutdown'sız kalıyordu. Süpürme idempotent: taze handle'da her
    // sıfırlama zaten-defaulta yazar, VFS boş-mount temizler.
    if (!m_initialized && !m_window) return;

    ROWL_LOG_INFO("Shutting down Rowl Engine...");

    deactivateScripts();
    // B7 (#38): deactivation stops the scripts, but the stale statuses
    // survived shutdown — a host reusing the Engine object (or reading status
    // between shutdown and re-init) saw the previous session's script state.
    m_scriptRuntimeStatuses.clear();

    if (m_scene) {
        m_scene->clear();
        m_scene.reset();
    }

    if (m_audio) {
        m_audio->shutdown();
        m_audio.reset();
    }

    if (m_luaSandbox) {
        m_luaSandbox->shutdown();
        m_luaSandbox.reset();
    }

    if (m_window) {
        m_window->shutdown();
        m_window.reset();
    }

    // D2 shutdown-süpürme: oturum profili + graph + mount + handle.
    resetSessionProfile();
    // D2 (#112): story graph + cursor + gameState bir sonraki oturuma
    // sızmasın. Taze-handle ile özdeş başlangıç.
    m_storyRuntime = StoryRuntime{};
    m_gameState.reset();
    // D2 (#107): VFS mount'ları önceki projenin story/lua/paketini yeni
    // oturuma sızdırıyordu (initialize erken-dönüyordu). VFS per-Engine
    // (RuntimeContext sahipliği); paylaşılan-VFS sözleşmesi c_api.h'de.
    if (getVfs()) {
        getVfs()->clearMountPoints();
    }
    // D2 (#124): gömülü tutamaç sıfırlanmazsa sonraki Init host
    // standalone istese bile bayat pointer ile embedded dalına girer.
    // Tek-çekimlik tüketim: shutdown sonrası re-init config-güdümlü.
    m_externalWindowHandle = nullptr;
    m_externalWindowWidth  = 0;
    m_externalWindowHeight = 0;
    // D2 (#113-kuzeni): proje-override save-dizini de oturuma aittir.
    m_saveDirectoryOverride.clear();

    m_isRunning   = false;
    m_initialized = false;
    ROWL_LOG_INFO("Engine shutdown complete.");
}

Rowl::State::SaveMetadata Engine::buildSaveMetadata() const {
    Rowl::State::SaveMetadata metadata;
    metadata.playtimeSeconds = m_playtimeSeconds;
    metadata.chapterId = getCurrentChapterId();
    for (const auto& chapter : m_storyRuntime.document().chapters) {
        if (chapter.id == metadata.chapterId) {
            metadata.chapterTitle = chapter.title;
            break;
        }
    }
    if (m_gameState && m_gameState->dialogueHistory && !m_gameState->dialogueHistory->empty()) {
        metadata.summary = Rowl::State::truncateSummary(
            m_gameState->dialogueHistory->back().dialogue);
    }
    uint32_t width = 0, height = 0, pitch = 0;
    const uint8_t* pixels = getPixelBuffer(&width, &height, &pitch);
    if (pixels && width > 0 && height > 0) {
        const auto thumbnail =
            Rowl::State::encodeThumbnailPng(pixels, width, height, pitch);
        metadata.thumbnailPng = std::move(thumbnail.png);
        metadata.thumbnailWidth = thumbnail.width;
        metadata.thumbnailHeight = thumbnail.height;
    }
    return metadata;
}

bool Engine::saveGameSlot(int32_t slotIndex) {
    // D1 (#109/#129/#130): init-öncesi save, varolmayan oturumu node-0
    // kayıt olarak dosyaya yazıp success dönüyordu. Fail-closed: dosya
    // yazılmadan StateError + false (quickSave otomatik kapsanır).
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot save: engine is not initialized",
                                "save_game_slot", std::to_string(slotIndex));
        }
        return false;
    }
    if (!Rowl::State::isValidSlot(slotIndex)) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-99)",
                            "save_game_slot", std::to_string(slotIndex));
        return false;
    }
    // D4/D1 (#51): stage-then-commit. checkpoint+metadata önce yerelde
    // hazırlanır; m_gameState'e yalnızca disk yazımı BAŞARILIYSA commit'lenir.
    // Başarısız save artık stepId/node ilerletmez, eski slot dosyası değişmez.
    std::shared_ptr<const Rowl::State::GameState> staged =
        Rowl::State::SessionPersistence::checkpoint(
            m_gameState, m_storyRuntime.currentNodeId());
    if (staged) {
        staged = Rowl::State::GameState::withSaveMetadata(
            staged, buildSaveMetadata());
        // D4/G (#70): stamp the committed graph's content identity so a
        // foreign-graph save aborts at load instead of warn-only merging.
        // Graph-less flows stamp "" → legacy-warn path on load.
        staged = Rowl::State::GameState::withGraphIdentity(
            staged, Rowl::Core::computeGraphIdentity(getStoryGraphDocument()));
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory =
        Rowl::Platform::pathToUtf8(persistence.saveDirectory());
    bool ok = persistence.saveSlot(staged, slotIndex);
    if (!ok) {
        m_context->setError(RuntimeErrorCode::IoError,
                            "Failed to write save slot #" + std::to_string(slotIndex) + " to " + saveDirectory,
                            "save_game_slot", std::to_string(slotIndex));
        return false;
    }
    m_gameState = staged;
    // A2b: the slot page memoizes occupancy — a successful write changes it.
    m_pauseSlotCacheValid = false;
    m_context->setSuccess("save_game_slot", std::to_string(slotIndex));
    return true;
}

bool Engine::loadGameSlot(int32_t slotIndex) {
    // D1 (#108/#129/#130): init-öncesi load, oturumu sessizce uygulayıp
    // setSuccess bırakıyordu; sonraki init imha edip stale OK bırakıyordu.
    // Fail-closed: state'e dokunmadan StateError + false.
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot load: engine is not initialized",
                                "load_game_slot", std::to_string(slotIndex));
        }
        return false;
    }
    if (!Rowl::State::isValidSlot(slotIndex)) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-99)",
                            "load_game_slot", std::to_string(slotIndex));
        return false;
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory =
        Rowl::Platform::pathToUtf8(persistence.saveDirectory());
    const auto loadResult = persistence.loadSlotDetailed(slotIndex);
    if (!loadResult.succeeded()) {
        RuntimeErrorCode errorCode = RuntimeErrorCode::ParseError;
        std::string message = "Failed to parse or validate save slot #" + std::to_string(slotIndex);
        switch (loadResult.status) {
            case Rowl::State::SessionLoadStatus::NotFound:
                errorCode = RuntimeErrorCode::FileNotFound;
                message = "Save slot #" + std::to_string(slotIndex) + " not found in " + saveDirectory;
                break;
            case Rowl::State::SessionLoadStatus::FileTooLarge:
                errorCode = RuntimeErrorCode::FileTooLarge;
                message = "Save slot #" + std::to_string(slotIndex) + " exceeds the read limit";
                break;
            case Rowl::State::SessionLoadStatus::IoError:
                errorCode = RuntimeErrorCode::IoError;
                message = "Failed to read save slot #" + std::to_string(slotIndex);
                break;
            case Rowl::State::SessionLoadStatus::UnsupportedVersion:
                errorCode = RuntimeErrorCode::ValidationError;
                message = "Unsupported save format version " +
                    std::to_string(loadResult.sourceVersion) + " in slot #" +
                    std::to_string(slotIndex);
                break;
            default:
                break;
        }
        m_context->setError(errorCode, message, "load_game_slot", std::to_string(slotIndex));
        return false;
    }

    // D4/V (#43/#44/#46/#50/#52): sarkan-cursor reddi. Kayittaki node
    // commit'li grafta yoksa load commitlenmeden reddedilir (ValidationError).
    // Graf commitlenmemisken (legacy/graphsiz akis) dogrulanacak bir sey
    // olmadigindan eski kabul-davranisi korunur.
    {
        const uint64_t savedNode = loadResult.state ? loadResult.state->activeNodeId : 0;
        const auto& doc = getStoryGraphDocument();
        if (!doc.nodes.empty() && doc.nodes.find(savedNode) == doc.nodes.end()) {
            m_context->setError(RuntimeErrorCode::ValidationError,
                                "Save slot #" + std::to_string(slotIndex) +
                                    " restores node #" + std::to_string(savedNode) +
                                    " absent from the loaded graph",
                                "load_game_slot", std::to_string(slotIndex));
            return false;
        }
    }

    // D4/G (#70): graf-kimliği kilidi. Kayıt, commit'li graftan farklı bir
    // içeriğe aitse load commitlenmeden reddedilir (ValidationError): yabancı
    // değişken/ses/playtime oturuma bulaşmaz, rewind zinciri korunur.
    // Kimliksiz legacy kayıtlar WARN ile eski kabul-davranışını korur;
    // grafsız akışta karşılaştırılacak bir şey yoktur (V ile aynı kural).
    {
        const std::string savedGraph =
            loadResult.state ? loadResult.state->graphIdentity : "";
        const auto& doc = getStoryGraphDocument();
        if (savedGraph.empty()) {
            ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) +
                          " carries no graph identity (legacy save); " +
                          "skipping graph-identity check");
        } else if (!doc.nodes.empty() &&
                   savedGraph !=
                       Rowl::Core::computeGraphIdentity(doc)) {
            m_context->setError(RuntimeErrorCode::ValidationError,
                                "Save slot #" + std::to_string(slotIndex) +
                                    " was recorded from a different story graph; " +
                                    "refusing to apply",
                                "load_game_slot", std::to_string(slotIndex));
            return false;
        }
    }

    // D4/R (#48): rollback anlık-görüntüsü. decode+validate geçti ama restore
    // zinciri (imleç/lua/sahne/ses) patlarsa oturum yarı-göçmüş kalmasın:
    // yakala, geri al, fail-loud dön.
    const auto prevState = m_gameState;
    const uint64_t prevNode = m_storyRuntime.currentNodeId();
    const double prevPlaytime = m_playtimeSeconds;
    const uint64_t prevSfxNode = m_lastSfxPlaybackNodeId;
    // getAllVariables const-ref döner — kopya şart (clearVariables sonrası
    // referans ölürdü).
    const auto prevLuaVars = m_luaSandbox
        ? m_luaSandbox->getAllVariables()
        : std::unordered_map<std::string, std::string>{};
    // #86: restore zinciri sahneyi updateSceneFromComponents ile kurar
    // (kamera replayEntryEffects=false iken bile mutasyona uğrar) ve sesi
    // restoreAudioStateFromGameState ile değiştirir. Throw'da state kadar
    // kamera/canli-mikser de geri alınır.
    const auto prevAudio = captureAudioSnapshot();
    const auto prevCamera = captureCameraSnapshot();
    try {
    m_gameState = loadResult.state;
    m_playtimeSeconds = m_gameState ? m_gameState->playtimeSeconds : 0.0;
    // A2a-tur2: a save pointing outside its graph restored a null cursor
    // silently. Audible now; the restore flow itself is unchanged.
    if (!m_storyRuntime.setCurrentNodeId(m_gameState->activeNodeId)) {
        ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) + " restores node #" +
                      std::to_string(m_gameState->activeNodeId) + " absent from the loaded graph");
    }
    // Loading restores state; it is not a node-entry event and must not replay SFX.
    m_lastSfxPlaybackNodeId = m_storyRuntime.currentNodeId();

    // Sync variables to Lua sandbox
    if (m_luaSandbox && m_gameState->variables) {
        m_luaSandbox->clearVariables();
        for (const auto& [k, v] : m_gameState->variables->data) {
            m_luaSandbox->setVariable(k, v);
        }
    }

    // Synchronize scene to loaded node
    const StoryNode* activeNode = m_storyRuntime.currentNode();
    if (!activeNode) {
        // A2a-tur2: the setCurrentNodeId WARN above already fired for a
        // committed graph; this covers the empty-graph legacy path where the
        // id was accepted with nothing to validate against.
        ROWL_LOG_WARN("Save slot #" + std::to_string(slotIndex) +
                      " restored with no scene to sync (node #" +
                      std::to_string(m_storyRuntime.currentNodeId()) + " not in graph)");
    }
    if (activeNode) {
        const auto& nextNode = *activeNode;
        if (!nextNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : nextNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson, false);
        } else {
            updateActiveScene(
                nextNode.speaker, nextNode.dialogue,
                nextNode.background,
                nextNode.backgroundX, nextNode.backgroundY,
                nextNode.backgroundWidth, nextNode.backgroundHeight,
                nextNode.character,
                nextNode.characterX, nextNode.characterY,
                nextNode.characterWidth, nextNode.characterHeight,
                nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
            );
        }
    }
    restoreAudioStateFromGameState();
    } catch (const std::exception& restoreError) {
        // #86: best-effort geri-alım — state/imleç/lua atamaları + no-throw
        // lua ilkelleri kendi başına fırlatmaz; apply* best-effort + WARN
        // olduğu için catch gövdesi throw-safe'dir. Sahne görselleri içteki
        // updateSceneFromComponents catch'iyle çoktan geri alındı; burada
        // kamera + canlı-mikser de geri alınır (state otoriterdir, WARN
        // duyurur).
        m_gameState = prevState;
        m_playtimeSeconds = prevPlaytime;
        m_lastSfxPlaybackNodeId = prevSfxNode;
        m_storyRuntime.setCurrentNodeId(prevNode);
        if (m_luaSandbox) {
            m_luaSandbox->clearVariables();
            for (const auto& [key, value] : prevLuaVars) m_luaSandbox->setVariable(key, value);
        }
        applyCameraSnapshot(prevCamera);
        applyAudioSnapshot(prevAudio);
        ROWL_LOG_WARN("Load slot #" + std::to_string(slotIndex) +
                      " restore failed (" + restoreError.what() +
                      "); session rolled back");
        m_context->setError(RuntimeErrorCode::UnknownError,
                            "Load slot #" + std::to_string(slotIndex) +
                                " restore failed; session rolled back: " +
                                restoreError.what(),
                            "load_game_slot", std::to_string(slotIndex));
        return false;
    }
    ROWL_LOG_INFO("Loaded Game Slot #" + std::to_string(slotIndex) +
                  " → Node #" + std::to_string(m_storyRuntime.currentNodeId()));
    if (loadResult.migrated()) {
        m_context->setResult({RuntimeErrorCode::Ok, "load_game_slot",
            "Migrated save format version " + std::to_string(loadResult.sourceVersion) +
                " to version " + std::to_string(Rowl::State::GameState::CurrentSaveFormatVersion),
            std::to_string(slotIndex)});
    } else {
        m_context->setSuccess("load_game_slot", std::to_string(slotIndex));
    }
    return true;
}

// ── MS-6 quick slots & pause menu ─────────────────────────────────────────
bool Engine::setQuickSaveSlot(int32_t slotIndex) {
    if (slotIndex < kPauseMenuQuickSlotMin || slotIndex > kPauseMenuQuickSlotMax) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid quick-save slot #" + std::to_string(slotIndex) +
                                " (must be 0-9)",
                            "set_quick_save_slot", std::to_string(slotIndex));
        return false;
    }
    m_activeQuickSlot = slotIndex;
    m_context->setSuccess("set_quick_save_slot", std::to_string(slotIndex));
    ROWL_LOG_INFO("[Player] Active quick-save slot set to #" + std::to_string(slotIndex) +
                  " (F5/F9).");
    return true;
}

bool Engine::quickSave() {
    ROWL_LOG_INFO("[Player] Quick Saving to Slot #" + std::to_string(m_activeQuickSlot) + "...");
    return saveGameSlot(m_activeQuickSlot);
}

bool Engine::quickLoad() {
    ROWL_LOG_INFO("[Player] Quick Loading from Slot #" + std::to_string(m_activeQuickSlot) + "...");
    return loadGameSlot(m_activeQuickSlot);
}

void Engine::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    // Opening always lands on a predictable main page; closing resumes.
    // Pausing never quits — exit requires the two-step menu confirmation.
    m_pauseMode = PauseMenuMode::Main;
    m_pauseSelected = 0;
    m_pauseConfirmQuit = false;
    // A2b: a fresh menu open rebuilds slot occupancy — saves or deletes may
    // have happened out-of-band while unpaused (editor, quick keys).
    m_pauseSlotCacheValid = false;
    ROWL_LOG_INFO(paused ? "[Player] Paused — menu open (Esc/P to resume)."
                         : "[Player] Resumed.");
}

int Engine::pauseMenuRowCount() const {
    return m_pauseMode == PauseMenuMode::Main ? PauseMenuLayout::kMainRows
                                             : PauseMenuLayout::kSlotRows;
}

void Engine::pauseMenuMoveSelection(int direction) {
    const int count = pauseMenuRowCount();
    if (count <= 0) return;
    m_pauseSelected = (m_pauseSelected + direction + count) % count;
    m_pauseConfirmQuit = false;
}

float Engine::pauseMenuVolume(int row) const {
    if (!m_audio) return 1.0f;
    switch (row) {
        case 3: return m_audio->getMasterVolume();
        case 4: return m_audio->getBgmVolume();
        case 5: return m_audio->getSfxVolume();
        case 6: return m_audio->getVoiceVolume();
        default: return 1.0f;
    }
}

void Engine::setPauseMenuVolume(int row, float volume) {
    if (!m_audio) return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    bool mixerRow = true;
    switch (row) {
        case 3: m_audio->setMasterVolume(volume); break;
        case 4: m_audio->setBgmVolume(volume); break;
        case 5: m_audio->setSfxVolume(volume); break;
        case 6: m_audio->setVoiceVolume(volume); break;
        default: mixerRow = false; break;
    }
    // #86: setter commit — slider tıklaması state'e damgalanır (step yok).
    if (mixerRow) commitMixerVolumesToGameState();
}

void Engine::pauseMenuAdjustSelected(int direction) {
    if (m_pauseMode != PauseMenuMode::Main) return;
    const int row = m_pauseSelected;
    if (row >= 3 && row <= 6) {
        setPauseMenuVolume(row, pauseMenuVolume(row) + direction * 0.05f);
    } else if (row == 7) {
        setTextSpeedMultiplier(m_textSpeedMultiplier + direction * 0.25f);
    }
    m_pauseConfirmQuit = false;
}

void Engine::menuChooseSlot(int32_t slotIndex) {
    if (m_pauseMode == PauseMenuMode::Main) return;
    if (slotIndex < kPauseMenuQuickSlotMin || slotIndex > kPauseMenuQuickSlotMax) return;
    if (m_pauseMode == PauseMenuMode::SaveSlots) {
        if (saveGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Saved to slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    } else {
        if (loadGameSlot(slotIndex)) {
            ROWL_LOG_INFO("[Player] Loaded slot #" + std::to_string(slotIndex) + " from pause menu.");
        }
    }
    // Stay paused on the slot page so occupancy refreshes visibly.
}

void Engine::menuActivateSelected() {
    if (!m_paused) return;
    if (m_pauseMode != PauseMenuMode::Main) {
        menuChooseSlot(static_cast<int32_t>(m_pauseSelected));
        return;
    }
    switch (m_pauseSelected) {
        case 0: setPaused(false); break;
        case 1: m_pauseMode = PauseMenuMode::SaveSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case 2: m_pauseMode = PauseMenuMode::LoadSlots; m_pauseSelected = 0; m_pauseConfirmQuit = false; break;
        case 8:
            if (m_pauseConfirmQuit) {
                ROWL_LOG_INFO("[Player] Exit confirmed from pause menu.");
                m_isRunning = false;
            } else {
                m_pauseConfirmQuit = true;
            }
            break;
        default:
            // Value rows (3-7) have no activation of their own; keyboard
            // Confirm still steps them up like Right for parity with clicks.
            pauseMenuAdjustSelected(+1);
            break;
    }
}

void Engine::menuBack() {
    if (!m_paused) return;
    if (m_pauseConfirmQuit) {
        m_pauseConfirmQuit = false;
        return;
    }
    if (m_pauseMode != PauseMenuMode::Main) {
        m_pauseMode = PauseMenuMode::Main;
        m_pauseSelected = 0;
        return;
    }
    setPaused(false);
}

void Engine::pauseMenuCommand(PauseMenuCommand command) {
    if (!m_paused) return;
    switch (command) {
        case PauseMenuCommand::Up: pauseMenuMoveSelection(-1); break;
        case PauseMenuCommand::Down: pauseMenuMoveSelection(+1); break;
        case PauseMenuCommand::Left: pauseMenuAdjustSelected(-1); break;
        case PauseMenuCommand::Right: pauseMenuAdjustSelected(+1); break;
        case PauseMenuCommand::Back: menuBack(); break;
        case PauseMenuCommand::Confirm: menuActivateSelected(); break;
    }
}

void Engine::pauseMenuClick(float virtualX, float virtualY) {
    if (!m_paused) return;
    const int row = PauseMenuLayout::rowAt(virtualX, virtualY, pauseMenuRowCount());
    if (row < 0) return; // gaps and outside miss; selection is kept
    const bool alreadySelected = (row == m_pauseSelected);
    m_pauseSelected = row;
    if (m_pauseMode == PauseMenuMode::Main && row >= 3 && row <= 7) {
        const int dir = PauseMenuLayout::adjustDirection(virtualX);
        if (dir != 0) {
            pauseMenuAdjustSelected(dir);
            return;
        }
        // Middle band only selects (a second tap confirms).
        if (!alreadySelected) {
            m_pauseConfirmQuit = false;
            return;
        }
    }
    menuActivateSelected();
}

static std::string pauseMenuPercent(float volume) {
    return std::to_string(static_cast<int>(volume * 100.0f + 0.5f)) + "%";
}

static std::string pauseMenuSpeedText(float multiplier) {
    const int quarters = static_cast<int>(multiplier * 4.0f + 0.5f);
    const int whole = quarters / 4;
    const int frac = (quarters % 4) * 25;
    return std::to_string(whole) + "." + (frac < 10 ? "0" : "") + std::to_string(frac) + "x";
}

PauseMenuView Engine::getPauseMenuView() const {
    PauseMenuView view;
    view.open = m_paused;
    if (!m_paused) return view;
    view.selected = m_pauseSelected;
    if (m_pauseMode == PauseMenuMode::Main) {
        view.title = "Duraklatildi";
        view.rows = {
            {"Devam Et", "", false},
            {"Oyunu Kaydet", "yuva sec >", false},
            {"Oyunu Yukle", "yuva sec >", false},
            {"Ana Ses", pauseMenuPercent(pauseMenuVolume(3)), true},
            {"Muzik", pauseMenuPercent(pauseMenuVolume(4)), true},
            {"SFX", pauseMenuPercent(pauseMenuVolume(5)), true},
            {"Seslendirme", pauseMenuPercent(pauseMenuVolume(6)), true},
            {"Metin Hizi", pauseMenuSpeedText(m_textSpeedMultiplier), true},
            {"Cikis", m_pauseConfirmQuit ? "emin misin?" : "", false},
        };
        view.hint = m_pauseConfirmQuit
            ? "ENTER: cikisi onayla   ESC: vazgec"
            : "YUKARI/ASAGI: sec   SOL/SAG: ayar   ENTER: tamam   ESC: devam et";
    } else {
        const bool saving = (m_pauseMode == PauseMenuMode::SaveSlots);
        view.title = saving ? "Kayit Yuvasi Sec" : "Yukleme Yuvasi Sec";
        // A2b: occupancy is memoized (see m_pauseSlotCacheValid) — a slot
        // page holds 10 rows and this view rebuilds per UI refresh.
        static_assert(kPauseMenuQuickSlotMax - kPauseMenuQuickSlotMin + 1 ==
                      10, "pause slot cache assumes the 0..9 quick-slot window");
        if (!m_pauseSlotCacheValid) {
            for (int32_t slot = kPauseMenuQuickSlotMin; slot <= kPauseMenuQuickSlotMax; ++slot) {
                m_pauseSlotPresent[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)] =
                    hasSaveSlot(slot);
            }
            m_pauseSlotCacheValid = true;
        }
        for (int32_t slot = kPauseMenuQuickSlotMin; slot <= kPauseMenuQuickSlotMax; ++slot) {
            PauseMenuRow row;
            row.label = "Yuva " + std::to_string(slot);
            row.value = m_pauseSlotPresent[static_cast<size_t>(slot - kPauseMenuQuickSlotMin)]
                ? "dolu" : "bos";
            view.rows.push_back(row);
        }
        view.hint = "ENTER/tik: sec   0-9: dogrudan sec   ESC: geri";
    }
    return view;
}

static void appendPauseMenuJsonString(std::string& out, const std::string& text) {
    out.push_back('"');
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

std::string Engine::getPauseMenuJson() const {
    const PauseMenuView view = getPauseMenuView();
    std::string out = "{\"open\":";
    out += view.open ? "true" : "false";
    out += ",\"mode\":";
    out += std::to_string(static_cast<int>(m_pauseMode));
    out += ",\"selected\":";
    out += std::to_string(view.selected);
    out += ",\"confirm_quit\":";
    out += m_pauseConfirmQuit ? "true" : "false";
    out += ",\"quick_slot\":";
    out += std::to_string(m_activeQuickSlot);
    out += ",\"title\":";
    appendPauseMenuJsonString(out, view.title);
    out += ",\"hint\":";
    appendPauseMenuJsonString(out, view.hint);
    out += ",\"rows\":[";
    for (size_t i = 0; i < view.rows.size(); ++i) {
        if (i > 0) out.push_back(',');
        out += "{\"label\":";
        appendPauseMenuJsonString(out, view.rows[i].label);
        out += ",\"value\":";
        appendPauseMenuJsonString(out, view.rows[i].value);
        out += ",\"selected\":";
        out += (static_cast<int>(i) == view.selected) ? "true" : "false";
        out.push_back('}');
    }
    out += "]}";
    return out;
}

bool Engine::hasSaveSlot(int32_t slotIndex) const {
    if (!Rowl::State::isValidSlot(slotIndex)) return false;
    return sessionPersistence().hasSlot(slotIndex);
}

bool Engine::deleteSaveSlot(int32_t slotIndex) {
    if (!Rowl::State::isValidSlot(slotIndex)) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-99)",
                            "delete_save_slot", std::to_string(slotIndex));
        return false;
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory =
        Rowl::Platform::pathToUtf8(persistence.saveDirectory());
    if (!persistence.hasSlot(slotIndex)) {
        m_context->setError(RuntimeErrorCode::FileNotFound,
                            "Save slot #" + std::to_string(slotIndex) + " does not exist",
                            "delete_save_slot", std::to_string(slotIndex));
        return false;
    }
    bool ok = persistence.deleteSlot(slotIndex);
    if (!ok) {
        m_context->setError(RuntimeErrorCode::IoError,
                            "Failed to delete save slot #" + std::to_string(slotIndex),
                            "delete_save_slot", std::to_string(slotIndex));
        return false;
    }
    // A2b: the slot page memoizes occupancy — a successful delete changes it.
    m_pauseSlotCacheValid = false;
    m_context->setSuccess("delete_save_slot", std::to_string(slotIndex));
    return true;
}

bool Engine::rewind(uint64_t steps) {
    // D1 (#130): rewind yolunda hiçbir guard yoktu — init-öncesi/shutdown-
    // sonrası retained state üzerinde sessizce çalışıyordu. Fail-closed.
    if (!m_initialized) {
        if (m_context) {
            m_context->setError(RuntimeErrorCode::StateError,
                                "Cannot rewind: engine is not initialized",
                                "rewind", std::to_string(steps));
        }
        return false;
    }
    auto rewound = Rowl::State::SessionPersistence::rewind(m_gameState, steps);
    if (!rewound) {
        // D4/R-48: refuse-yolu last-result'a yazılmadan 0 dönüyordu; host
        // bayat Ok kodunu okuyup hatayı kaçırıyordu. Fail-loud.
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            steps == 0
                                ? "Cannot rewind zero steps"
                                : "Cannot rewind " + std::to_string(steps) +
                                      " steps from step #" +
                                      std::to_string(getCurrentStepId()),
                            "rewind", std::to_string(steps));
        return false;
    }

    // D4/R (#48): load ile aynı rollback sözleşmesi — restore patlarsa
    // oturum rewind-öncesine döner, fail-loud.
    const auto prevState = m_gameState;
    const uint64_t prevNode = m_storyRuntime.currentNodeId();
    // #86: load (2499) karşılığı — rewind hedef state'in playtime'ını
    // oturuma işler; eskiden m_playtimeSeconds rewind-öncesinde takılı
    // kalıyordu.
    const double prevPlaytime = m_playtimeSeconds;
    const uint64_t prevSfxNode = m_lastSfxPlaybackNodeId;
    const auto prevLuaVars = m_luaSandbox
        ? m_luaSandbox->getAllVariables()
        : std::unordered_map<std::string, std::string>{};
    // #86: load ile aynı rollback sözleşmesi — kamera + canlı-mikser dahil.
    const auto prevAudio = captureAudioSnapshot();
    const auto prevCamera = captureCameraSnapshot();
    try {
    m_gameState = rewound;
    m_playtimeSeconds = m_gameState ? m_gameState->playtimeSeconds : 0.0;
    // A2a-tur2: same dangle-audibility as the save-load restore above.
    if (!m_storyRuntime.setCurrentNodeId(m_gameState->activeNodeId)) {
        ROWL_LOG_WARN("Rewind of " + std::to_string(steps) +
                      " steps lands on node #" + std::to_string(m_gameState->activeNodeId) +
                      " absent from the loaded graph");
    }

    if (m_luaSandbox && m_gameState->variables) {
        m_luaSandbox->clearVariables();
        for (const auto& [k, v] : m_gameState->variables->data) {
            m_luaSandbox->setVariable(k, v);
        }
    }

    const StoryNode* activeNode = m_storyRuntime.currentNode();
    if (!activeNode) {
        // A2a-tur2: same no-scene-to-sync audibility as the save-load
        // restore above.
        ROWL_LOG_WARN("Rewind completed with no scene to sync (node #" +
                      std::to_string(m_storyRuntime.currentNodeId()) + " not in graph)");
    }
    if (activeNode) {
        const auto& nextNode = *activeNode;
        if (!nextNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : nextNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson, false);
        } else {
            updateActiveScene(
                nextNode.speaker, nextNode.dialogue,
                nextNode.background,
                nextNode.backgroundX, nextNode.backgroundY,
                nextNode.backgroundWidth, nextNode.backgroundHeight,
                nextNode.character,
                nextNode.characterX, nextNode.characterY,
                nextNode.characterWidth, nextNode.characterHeight,
                nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
            );
        }
    }
    restoreAudioStateFromGameState();
    } catch (const std::exception& restoreError) {
        m_gameState = prevState;
        // #86: playtime senkronu (load karşılığı) + kamera/ses geri-alımı.
        m_playtimeSeconds = prevPlaytime;
        m_lastSfxPlaybackNodeId = prevSfxNode;
        m_storyRuntime.setCurrentNodeId(prevNode);
        if (m_luaSandbox) {
            m_luaSandbox->clearVariables();
            for (const auto& [key, value] : prevLuaVars) m_luaSandbox->setVariable(key, value);
        }
        applyCameraSnapshot(prevCamera);
        applyAudioSnapshot(prevAudio);
        ROWL_LOG_WARN("Rewind of " + std::to_string(steps) +
                      " steps restore failed (" + restoreError.what() +
                      "); session rolled back");
        m_context->setError(RuntimeErrorCode::UnknownError,
                            std::string("Rewind restore failed; session rolled back: ") +
                                restoreError.what(),
                            "rewind", std::to_string(steps));
        return false;
    }
    m_context->setSuccess("rewind", std::to_string(steps));
    return true;
}

void Engine::restoreAudioStateFromGameState() {
    if (!m_audio || !m_gameState) return;
    // #86: tam mikser restore'u. Ambience/Ui kazançları bilerek
    // session-local kalır (state'e yazılmaz, buradan okunmaz).
    m_audio->setMasterVolume(m_gameState->masterVolume);
    m_audio->setBgmVolume(m_gameState->bgmVolume);
    m_audio->setSfxVolume(m_gameState->sfxVolume);
    m_audio->setVoiceVolume(m_gameState->voiceVolume);
    if (m_gameState->dspFilter == "Cave" || m_gameState->dspFilter == "CaveReverb") {
        m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::CaveReverb);
    } else if (m_gameState->dspFilter == "Telephone") {
        m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);
    } else if (m_gameState->dspFilter == "Underwater" || m_gameState->dspFilter == "UnderwaterLowPass") {
        m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    } else {
        m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
    }

    if (m_gameState->bgmPlaying && !m_gameState->activeBgm.empty()) {
        m_audio->playAudio(m_gameState->activeBgm, Rowl::Audio::AudioChannelType::Bgm,
                           m_audio->getActiveFilter());
    } else {
        m_audio->stopBgm();
    }
}

// ── #86: transactional restore snapshot helpers ─────────────────────────────
// capture* salt-okuma (throw-safe); apply* best-effort + WARN, dışarı asla
// fırlatmaz — catch gövdelerinden çağrılmaya uygundur.
Engine::AudioSnapshot Engine::captureAudioSnapshot() const {
    AudioSnapshot snapshot;
    if (!m_audio) return snapshot;
    snapshot.hasAudio = true;
    snapshot.masterVolume = m_audio->getMasterVolume();
    snapshot.bgmVolume = m_audio->getBgmVolume();
    snapshot.sfxVolume = m_audio->getSfxVolume();
    snapshot.voiceVolume = m_audio->getVoiceVolume();
    snapshot.bgmPath = m_audio->getCurrentBgmPath();
    snapshot.bgmPlaying = m_audio->isBgmPlaying();
    switch (m_audio->getActiveFilter()) {
        case Rowl::Audio::DSPFilterType::CaveReverb: snapshot.dspFilter = 1; break;
        case Rowl::Audio::DSPFilterType::Telephone: snapshot.dspFilter = 2; break;
        case Rowl::Audio::DSPFilterType::UnderwaterLowPass: snapshot.dspFilter = 3; break;
        case Rowl::Audio::DSPFilterType::Normal: break;
    }
    return snapshot;
}

void Engine::applyAudioSnapshot(const AudioSnapshot& snapshot) {
    if (!snapshot.hasAudio || !m_audio) return;
    try {
        // Kazanç + filtre her zaman geri yazılır (idempotent setter'lar).
        m_audio->setMasterVolume(snapshot.masterVolume);
        m_audio->setBgmVolume(snapshot.bgmVolume);
        m_audio->setSfxVolume(snapshot.sfxVolume);
        m_audio->setVoiceVolume(snapshot.voiceVolume);
        Rowl::Audio::DSPFilterType filter = Rowl::Audio::DSPFilterType::Normal;
        if (snapshot.dspFilter == 1) filter = Rowl::Audio::DSPFilterType::CaveReverb;
        else if (snapshot.dspFilter == 2) filter = Rowl::Audio::DSPFilterType::Telephone;
        else if (snapshot.dspFilter == 3) filter = Rowl::Audio::DSPFilterType::UnderwaterLowPass;
        m_audio->applyDspFilter(filter);
        // BGM niyeti SADECE sapmışsa düzeltilir: mutasyon-suz throw yolunda
        // aynı parçayı baştan çalmak duyulur bir restart olurdu.
        if (snapshot.bgmPlaying && !snapshot.bgmPath.empty()) {
            if (!m_audio->isBgmPlaying() || m_audio->getCurrentBgmPath() != snapshot.bgmPath) {
                m_audio->playAudio(snapshot.bgmPath, Rowl::Audio::AudioChannelType::Bgm, filter);
            }
        } else if (m_audio->isBgmPlaying()) {
            m_audio->stopBgm();
        }
    } catch (const std::exception& e) {
        ROWL_LOG_WARN("Audio snapshot re-apply failed: " + std::string(e.what()));
    } catch (...) {
        ROWL_LOG_WARN("Audio snapshot re-apply failed (unknown error)");
    }
}

Engine::CameraSnapshot Engine::captureCameraSnapshot() const {
    CameraSnapshot snapshot;
    const auto* camera = m_window ? m_window->getCamera() : nullptr;
    if (!camera) return snapshot;
    snapshot.hasCamera = true;
    snapshot.x = camera->getPositionX();
    snapshot.y = camera->getPositionY();
    snapshot.zoom = camera->getZoom();
    snapshot.rotation = camera->getRotation();
    return snapshot;
}

void Engine::applyCameraSnapshot(const CameraSnapshot& snapshot) {
    if (!snapshot.hasCamera) return;
    auto* camera = m_window ? m_window->getCamera() : nullptr;
    if (!camera) return;
    try {
        // Setter'lar in-flight tween'leri iptal eder: tam restore.
        camera->setPosition(snapshot.x, snapshot.y);
        camera->setZoom(snapshot.zoom);
        camera->setRotation(snapshot.rotation);
    } catch (const std::exception& e) {
        ROWL_LOG_WARN("Camera snapshot re-apply failed: " + std::string(e.what()));
    } catch (...) {
        ROWL_LOG_WARN("Camera snapshot re-apply failed (unknown error)");
    }
}

void Engine::commitMixerVolumesToGameState() {
    // #86: setter commit — canlı kazançları step ilerletmeden state'e
    // damgalar (withMixerVolumes: yapısal-paylaşım, rewind zinciri uzamaz).
    if (!m_audio || !m_gameState) return;
    m_gameState = Rowl::State::GameState::withMixerVolumes(
        m_gameState,
        m_audio->getMasterVolume(), m_audio->getBgmVolume(),
        m_audio->getSfxVolume(), m_audio->getVoiceVolume());
}

uint64_t Engine::getCurrentStepId() const {
    return m_gameState ? m_gameState->stepId : 0;
}

void Engine::setScriptVariable(const std::string& key, const std::string& value) {
    if (m_luaSandbox) {
        m_luaSandbox->setVariable(key, value);
    }
    if (m_gameState) {
        m_gameState = Rowl::State::GameState::createNextState(
            m_gameState, m_storyRuntime.currentNodeId(), key, value);
    }
}

std::string Engine::getScriptVariable(const std::string& key) const {
    if (m_luaSandbox) {
        return m_luaSandbox->getVariable(key);
    }
    if (m_gameState) {
        return m_gameState->getVariable(key);
    }
    return "";
}

bool Engine::evaluateCondition(const std::string& conditionExpr) {
    // Fail-closed: without a live sandbox no branch may be taken. The error is
    // recorded so RowlEngine_GetLastResultCode() reflects the failure to hosts.
    if (!m_luaSandbox) {
        m_context->setError(RuntimeErrorCode::ScriptRuntimeError,
                            "Lua sandbox is unavailable; failing closed on condition evaluation",
                            "evaluate_condition", conditionExpr);
        return false;
    }
    bool ok = m_luaSandbox->evaluateCondition(conditionExpr);
    if (!ok && !m_luaSandbox->getLastError().empty()) {
        // B7 (#27): the old single branch reported ScriptSyntaxError for EVERY
        // failure — including instruction-limit poison and runtime throws — so
        // hosts could never distinguish a typo from a dead session. The
        // sandbox tags the phase; the wrapper maps it to the existing codes
        // (no new RuntimeErrorCode — no C ABI / C# churn).
        const auto phase = m_luaSandbox->getLastConditionPhase();
        const RuntimeErrorCode code =
            (phase == Rowl::Scripting::LuaSandbox::ConditionPhase::Syntax)
                ? RuntimeErrorCode::ScriptSyntaxError
                : RuntimeErrorCode::ScriptRuntimeError;
        m_context->setError(code,
                            m_luaSandbox->getLastError(),
                            "evaluate_condition", conditionExpr);
        return false;
    }
    m_context->setSuccess("evaluate_condition", conditionExpr);
    return ok;
}

bool Engine::executeScript(const std::string& scriptCode) {
    if (m_luaSandbox) {
        if (!m_luaSandbox->executeString(scriptCode)) {
            m_context->setError(RuntimeErrorCode::ScriptRuntimeError,
                                m_luaSandbox->getLastError().empty() ? "Script execution failed" : m_luaSandbox->getLastError(),
                                "execute_script", "");
            return false;
        }

        // Scripts persist game data through rowl.var_set. Capture its complete
        // post-script snapshot as one immutable state transition so save/load
        // and rewind cannot lose a multi-variable script update.
        if (m_gameState) {
            m_gameState = Rowl::State::GameState::createNextStateWithVariables(
                m_gameState, m_storyRuntime.currentNodeId(),
                m_luaSandbox->getAllVariables());
        }
        m_context->setSuccess("execute_script", "");
        return true;
    }
    m_context->setError(RuntimeErrorCode::StateError, "Lua sandbox is not initialized", "execute_script", "");
    return false;
}

void Engine::setDialogueVoiceBlip(const std::string& soundPath, float basePitch, float pitchVariance, int cadence, bool skipPunctuation, int channelType) {
    m_activeDialogueData.typewriterSound = soundPath;
    m_activeDialogueData.voiceBlipPitch = std::clamp(basePitch, 0.25f, 4.0f);
    m_activeDialogueData.voiceBlipPitchVariance = std::clamp(pitchVariance, 0.0f, 1.0f);
    m_activeDialogueData.voiceBlipCadence = std::max(1, cadence);
    m_activeDialogueData.voiceBlipSkipPunctuation = skipPunctuation;
    m_activeDialogueData.voiceBlipChannel = (channelType == 2) ? 2 : 1;
    for (auto& dlg : m_activeDialogues) {
        dlg.typewriterSound = m_activeDialogueData.typewriterSound;
        dlg.voiceBlipPitch = m_activeDialogueData.voiceBlipPitch;
        dlg.voiceBlipPitchVariance = m_activeDialogueData.voiceBlipPitchVariance;
        dlg.voiceBlipCadence = m_activeDialogueData.voiceBlipCadence;
        dlg.voiceBlipSkipPunctuation = m_activeDialogueData.voiceBlipSkipPunctuation;
        dlg.voiceBlipChannel = m_activeDialogueData.voiceBlipChannel;
    }
}

void Engine::playVoiceBlip(const std::string& soundPath, float pitch, float volume, int channelType) {
    if (m_audio) {
        Rowl::Audio::AudioChannelType ch = (channelType == 2)
            ? Rowl::Audio::AudioChannelType::Sfx
            : Rowl::Audio::AudioChannelType::Voice;
        m_audio->playVoiceBlip(soundPath, pitch, volume, ch);
    }
}

uint32_t Engine::getVoiceBlipCount() const {
    return m_audio ? m_audio->getVoiceBlipCount() : 0;
}

void Engine::resetVoiceBlipCount() {
    if (m_audio) {
        m_audio->resetVoiceBlipCount();
    }
}

} // namespace Rowl::Core
