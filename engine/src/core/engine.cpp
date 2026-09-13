#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/scene/scene.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include <chrono>
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
    if (!m_saveDirectoryOverride.empty()) return m_saveDirectoryOverride;
    if (const auto host = getPlatformHost()) {
        const auto path = host->writableSavePath();
        if (!path.empty()) return path.string();
    }
    return "saves";
}

Rowl::State::SessionPersistence& Engine::sessionPersistence() const {
    m_sessionPersistence.setSaveDirectory(getSaveDirectory());
    return m_sessionPersistence;
}

void Engine::handleRuntimeInput(const Rowl::Platform::RuntimeInputEvent& event) {
    switch (event.type) {
        case Rowl::Platform::RuntimeInputEvent::Type::Advance:
            advanceToNextNode();
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::QuickSave:
            saveGameSlot(0);
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::QuickLoad:
            loadGameSlot(0);
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::Rewind:
            rewind(1);
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::PointerDown:
            if (!handlePointerDown(event.x, event.y)) advanceToNextNode();
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::SwipeForward:
            advanceToNextNode();
            break;
        case Rowl::Platform::RuntimeInputEvent::Type::SwipeBack:
            rewind(1);
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
        ROWL_LOG_WARN("Engine is already initialized.");
        return true;
    }

    if (config.virtualWidth == 0 || config.virtualHeight == 0 ||
        config.virtualWidth > kMaxVirtualCanvasDimension ||
        config.virtualHeight > kMaxVirtualCanvasDimension) {
        ROWL_LOG_ERROR("Invalid virtual canvas dimensions: " +
                       std::to_string(config.virtualWidth) + "x" + std::to_string(config.virtualHeight));
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
        return false;
    }

    m_window->setInputHandler([this](const Rowl::Platform::RuntimeInputEvent& event) {
        handleRuntimeInput(event);
    });

    // Initialize Entity-Component Scene Manager
    m_scene = std::make_unique<Rowl::Scene::Scene>();

    // Initialize Audio Engine Subsystem
    m_audio = std::make_unique<Rowl::Audio::AudioEngine>(getVfs());
    m_audio->initialize();

    // Initialize Sandboxed Lua Scripting Environment
    m_luaSandbox = std::make_unique<Rowl::Scripting::LuaSandbox>();
    m_luaSandbox->initialize();

    // Initialize GameState Subsystem (Root Step #1)
    m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.currentNodeId());

    // Load story graph from disk
    loadStoryGraphFile();

    m_initialized = true;
    m_isRunning   = true;
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
            updateSceneFromComponents(compsJson.dump());
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
        if (m_isPlaying) {
            for (auto& dlg : m_activeDialogues) {
                dlg.elapsedTypewriterTime = 0.0f;
                dlg.lastBlipCodepointIndex = 0;
            }
            m_activeDialogueData.elapsedTypewriterTime = 0.0f;
            m_activeDialogueData.lastBlipCodepointIndex = 0;
        } else {
            for (auto& dlg : m_activeDialogues) {
                dlg.elapsedTypewriterTime = 9999.0f;
                dlg.lastBlipCodepointIndex = 99999;
            }
            m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
            m_activeDialogueData.lastBlipCodepointIndex = 99999;
        }
        ROWL_LOG_INFO("Engine Reset to Start Node #" + std::to_string(m_storyRuntime.currentNodeId()));
    }
}

const uint8_t* Engine::getPixelBuffer(uint32_t* outW, uint32_t* outH) const {
    if (outW) *outW = m_window ? m_window->getWidth() : 0;
    if (outH) *outH = m_window ? m_window->getHeight() : 0;
    return m_window ? m_window->getPixelBuffer() : nullptr;
}

void Engine::advanceToNextNode(uint32_t choiceIndex) {
    if (m_storyRuntime.empty()) return;
    m_autoAdvanceElapsed = 0.0f;

    // If typewriter is still typing out any line, clicking reveals the full text immediately
    bool anyTyping = false;
    for (const auto& dlg : m_activeDialogues) {
        if (m_isPlaying && dlg.typewriterEnabled && dlg.textSpeed > 0) {
            size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(dlg.dialogue);
            float msPerChar = static_cast<float>(dlg.textSpeed);
            float elapsedMs = dlg.elapsedTypewriterTime * 1000.0f;
            size_t visibleCodepoints = static_cast<size_t>(elapsedMs / msPerChar);
            if (visibleCodepoints < totalCodepoints) {
                anyTyping = true;
                break;
            }
        }
    }
    if (!anyTyping && m_isPlaying && m_activeDialogueData.typewriterEnabled && m_activeDialogueData.textSpeed > 0) {
        size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(m_activeDialogueData.dialogue);
        float msPerChar = static_cast<float>(m_activeDialogueData.textSpeed);
        float elapsedMs = m_activeDialogueData.elapsedTypewriterTime * 1000.0f;
        size_t visibleCodepoints = static_cast<size_t>(elapsedMs / msPerChar);
        if (visibleCodepoints < totalCodepoints) {
            anyTyping = true;
        }
    }

    if (anyTyping) {
        for (auto& dlg : m_activeDialogues) {
            dlg.elapsedTypewriterTime = 9999.0f;
            dlg.lastBlipCodepointIndex = 99999;
        }
        m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
        m_activeDialogueData.lastBlipCodepointIndex = 99999;
        return;
    }

    const auto advanceResult = m_storyRuntime.advance(choiceIndex);
    if (advanceResult == StoryRuntime::AdvanceResult::CurrentNodeMissing) {
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
            updateSceneFromComponents(compsJson.dump());
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
        if (m_isPlaying) {
            for (auto& dlg : m_activeDialogues) {
                dlg.elapsedTypewriterTime = 0.0f;
                dlg.lastBlipCodepointIndex = 0;
            }
            m_activeDialogueData.elapsedTypewriterTime = 0.0f;
            m_activeDialogueData.lastBlipCodepointIndex = 0;
        } else {
            for (auto& dlg : m_activeDialogues) {
                dlg.elapsedTypewriterTime = 9999.0f;
                dlg.lastBlipCodepointIndex = 99999;
            }
            m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
            m_activeDialogueData.lastBlipCodepointIndex = 99999;
        }
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
    if (!m_window || m_activeChoiceButtons.empty()) return false;
    const auto metrics = Rowl::Render::AspectGuardian::calculateViewport(
        m_window->getWidth(), m_window->getHeight(), 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return false;
    // Letterbox/pillarbox margins are not story canvas. Consume input there so
    // the caller does not turn a bezel tap into an accidental advance.
    if (!Rowl::Render::AspectGuardian::containsPhysicalPoint(physicalX, physicalY, metrics)) {
        return true;
    }
    const float x = (physicalX - static_cast<float>(metrics.x)) / metrics.scaleFactor;
    const float y = (physicalY - static_cast<float>(metrics.y)) / metrics.scaleFactor;
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
    };
    try {
        auto root = nlohmann::json::parse(componentsJson);
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
                dlgData.x = data.value("x", 80.0f);
                dlgData.y = data.value("y", 860.0f);
                dlgData.width = data.value("width", 1760.0f);
                dlgData.height = data.value("height", 180.0f);
                dlgData.scale = data.value("scale", 1.0f);
                dlgData.typewriterEnabled = data.value("typewriter_enabled", true);
                dlgData.textSpeed = data.value("text_speed", 30);
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
                if (m_isPlaying) {
                    dlgData.elapsedTypewriterTime = 0.0f;
                    dlgData.lastBlipCodepointIndex = 0;
                } else {
                    dlgData.elapsedTypewriterTime = 9999.0f;
                    dlgData.lastBlipCodepointIndex = 99999;
                }

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
                m_activeBackgroundParallaxX = data.value("parallax_x", 1.0f);
                m_activeBackgroundParallaxY = data.value("parallax_y", 1.0f);
                m_activeBackgroundOpacity = std::clamp(data.value("opacity", 1.0f), 0.0f, 1.0f);
                m_hasBackground = !m_activeBackground.empty();
            } else if (type == "character") {
                CharacterRenderData cd;
                cd.sprite = data.value("sprite", "");
                if (!cd.sprite.empty()) {
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
                    m_activeCharacters.push_back(cd);

                    // Set legacy single-character fallback to first character
                    if (m_activeCharacters.size() == 1) {
                        m_activeCharacter = cd.sprite;
                        m_activeCharacterX = cd.x;
                        m_activeCharacterY = cd.y;
                        m_activeCharacterWidth = cd.width;
                        m_activeCharacterHeight = cd.height;
                        m_activeCharacterRotation = cd.rotation;
                    }
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

                    // Lua condition check
                    std::string condition = option.value("condition", "");
                    if (!condition.empty() && condition != "true" && condition != "1") {
                        if (m_luaSandbox && !m_luaSandbox->evaluateCondition(condition)) {
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
                m_audio->isBgmPlaying(), filter);
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
        updateSceneFromComponents(componentsJson.dump());
    } else {
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
    const std::filesystem::path graphPath(jsonPath);
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
    std::ifstream f(jsonPath);
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

void Engine::loadStoryGraphFile() {
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
                return;
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
        if (std::filesystem::exists(p)) {
            loadStoryGraphFromPath(p);
            return;
        }
    }
    loadActiveStoryFile();
}

void Engine::loadActiveStoryFile() {
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
                    if (nodeId != 0) m_storyRuntime.setCurrentNodeId(nodeId);

                    if (data.contains("components") && data["components"].is_array()) {
                        updateSceneFromComponents(data["components"].dump());
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
                    return;
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
        std::error_code fileError;
        if (std::filesystem::is_regular_file(path, fileError) && !fileError &&
            std::filesystem::file_size(path, fileError) <= kMaxStoryJsonBytes && !fileError) {
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    nlohmann::json data = nlohmann::json::parse(f);
                    uint64_t nodeId = data.value("node_id", static_cast<uint64_t>(0));
                    if (nodeId != 0) m_storyRuntime.setCurrentNodeId(nodeId);

                    if (data.contains("components") && data["components"].is_array()) {
                        updateSceneFromComponents(data["components"].dump());
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
                    return;
                } catch (const std::exception& e) {
                    ROWL_LOG_ERROR("Active story load error in " + path + ": " + e.what());
                }
            }
        }
    }
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

    for (auto& dlg : m_activeDialogues) {
        dlg.isPlaying = m_isPlaying;
        if (m_isPlaying && dlg.typewriterEnabled && dlg.textSpeed > 0) {
            dlg.elapsedTypewriterTime += deltaTime * m_textSpeedMultiplier;

            size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(dlg.dialogue);
            float msPerChar = static_cast<float>(dlg.textSpeed);
            float elapsedMs = dlg.elapsedTypewriterTime * 1000.0f;
            size_t currentVisible = static_cast<size_t>(elapsedMs / msPerChar);
            if (currentVisible > totalCodepoints) currentVisible = totalCodepoints;

            if (currentVisible > dlg.lastBlipCodepointIndex && m_audio) {
                size_t startChar = dlg.lastBlipCodepointIndex;
                size_t endChar = currentVisible;
                dlg.lastBlipCodepointIndex = currentVisible;

                for (size_t charIdx = startChar; charIdx < endChar; ++charIdx) {
                    if (dlg.voiceBlipCadence > 1 && (charIdx % dlg.voiceBlipCadence) != 0) {
                        continue;
                    }

                    size_t byteIdx = 0;
                    uint32_t cp = 0;
                    for (size_t c = 0; c <= charIdx && byteIdx < dlg.dialogue.length(); ++c) {
                        cp = Rowl::Render::FontRenderer::getNextCodepoint(dlg.dialogue, byteIdx);
                    }

                    if (dlg.voiceBlipSkipPunctuation && isPunctuationOrWhitespace(cp)) {
                        continue;
                    }

                    float hash = static_cast<float>(((charIdx * 2654435761u) ^ (cp * 2246822519u)) % 1000) / 1000.0f;
                    float pitchMod = dlg.voiceBlipPitch + (hash * 2.0f - 1.0f) * dlg.voiceBlipPitchVariance;
                    pitchMod = std::clamp(pitchMod, 0.25f, 4.0f);

                    Rowl::Audio::AudioChannelType ch = (dlg.voiceBlipChannel == 2)
                        ? Rowl::Audio::AudioChannelType::Sfx
                        : Rowl::Audio::AudioChannelType::Voice;

                    m_audio->playVoiceBlip(dlg.typewriterSound, pitchMod, dlg.voiceBlipVolume, ch);
                    break;
                }
            }
        }
    }
    m_activeDialogueData.isPlaying = m_isPlaying;
    if (m_isPlaying && m_activeDialogueData.typewriterEnabled && m_activeDialogueData.textSpeed > 0) {
        if (!m_activeDialogues.empty()) {
            m_activeDialogueData.elapsedTypewriterTime = m_activeDialogues[0].elapsedTypewriterTime;
            m_activeDialogueData.lastBlipCodepointIndex = m_activeDialogues[0].lastBlipCodepointIndex;
        } else {
            m_activeDialogueData.elapsedTypewriterTime += deltaTime * m_textSpeedMultiplier;
            if (m_audio) {
                size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(m_activeDialogueData.dialogue);
                float msPerChar = static_cast<float>(m_activeDialogueData.textSpeed);
                float elapsedMs = m_activeDialogueData.elapsedTypewriterTime * 1000.0f;
                size_t currentVisible = static_cast<size_t>(elapsedMs / msPerChar);
                if (currentVisible > totalCodepoints) currentVisible = totalCodepoints;

                if (currentVisible > m_activeDialogueData.lastBlipCodepointIndex) {
                    size_t startChar = m_activeDialogueData.lastBlipCodepointIndex;
                    size_t endChar = currentVisible;
                    m_activeDialogueData.lastBlipCodepointIndex = currentVisible;

                    for (size_t charIdx = startChar; charIdx < endChar; ++charIdx) {
                        if (m_activeDialogueData.voiceBlipCadence > 1 && (charIdx % m_activeDialogueData.voiceBlipCadence) != 0) {
                            continue;
                        }
                        size_t byteIdx = 0;
                        uint32_t cp = 0;
                        for (size_t c = 0; c <= charIdx && byteIdx < m_activeDialogueData.dialogue.length(); ++c) {
                            cp = Rowl::Render::FontRenderer::getNextCodepoint(m_activeDialogueData.dialogue, byteIdx);
                        }
                        if (m_activeDialogueData.voiceBlipSkipPunctuation && isPunctuationOrWhitespace(cp)) {
                            continue;
                        }
                        float hash = static_cast<float>(((charIdx * 2654435761u) ^ (cp * 2246822519u)) % 1000) / 1000.0f;
                        float pitchMod = m_activeDialogueData.voiceBlipPitch + (hash * 2.0f - 1.0f) * m_activeDialogueData.voiceBlipPitchVariance;
                        pitchMod = std::clamp(pitchMod, 0.25f, 4.0f);
                        Rowl::Audio::AudioChannelType ch = (m_activeDialogueData.voiceBlipChannel == 2)
                            ? Rowl::Audio::AudioChannelType::Sfx
                            : Rowl::Audio::AudioChannelType::Voice;
                        m_audio->playVoiceBlip(m_activeDialogueData.typewriterSound, pitchMod, m_activeDialogueData.voiceBlipVolume, ch);
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
            autoAdvanceDelay = std::max(autoAdvanceDelay, dialogue.autoAdvanceDelay + m_autoAdvanceDelayOffset);
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

    m_window->renderVisualNovelFrame(
        m_hasBackground,
        m_activeBackground,
        m_activeBackgroundX,  m_activeBackgroundY,
        m_activeBackgroundWidth, m_activeBackgroundHeight,
        m_activeCharacters,
        m_activeDialogues,
        m_activeChoiceButtons,
        m_activeBackgroundRotation,
        m_activeBackgroundParallaxX,
        m_activeBackgroundParallaxY,
        m_activeBackgroundOpacity
    );

    // Update & Render Entity-Component Scene
    if (m_scene) {
        m_scene->update(deltaTime);
        m_scene->render(m_window.get());
    }

    if (m_audio) {
        m_audio->update(deltaTime);
    }
    if (m_hasActiveScript && m_luaSandbox) {
        for (const auto& moduleId : m_activeScriptModuleIds) {
            if (!m_luaSandbox->callOptionalModuleFunction(moduleId, "on_update", deltaTime)) {
                markScriptStatus(moduleId, {}, "failed", m_luaSandbox->getLastError());
            }
        }
    }

    m_window->endFrame();
}

void Engine::setTextSpeedMultiplier(float multiplier) {
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
                {m_storyRuntime.currentNodeId(), dialogue.speaker, dialogue.dialogue, true});
        }
    }
    if (!entries.empty()) {
        m_gameState = Rowl::State::GameState::withDialogueHistory(m_gameState, entries);
        m_lastRecordedDialogueNodeId = m_storyRuntime.currentNodeId();
    }
}

bool Engine::areActiveDialoguesComplete() const {
    for (const auto& dialogue : m_activeDialogues) {
        if (!dialogue.typewriterEnabled || dialogue.textSpeed <= 0) continue;
        const auto total = Rowl::Render::FontRenderer::countCodepoints(dialogue.dialogue);
        const auto visible = static_cast<std::size_t>(
            (dialogue.elapsedTypewriterTime * 1000.0f) / static_cast<float>(dialogue.textSpeed));
        if (visible < total) return false;
    }
    return true;
}

void Engine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Rowl Engine...");

    deactivateScripts();

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

    m_isRunning   = false;
    m_initialized = false;
    ROWL_LOG_INFO("Engine shutdown complete.");
}

bool Engine::saveGameSlot(int32_t slotIndex) {
    if (slotIndex < 0 || slotIndex > 100) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-100)",
                            "save_game_slot", std::to_string(slotIndex));
        return false;
    }
    if (!m_gameState) {
        m_gameState = Rowl::State::GameState::createInitialState(m_storyRuntime.currentNodeId());
    }
    if (m_gameState->activeNodeId != m_storyRuntime.currentNodeId()) {
        m_gameState = Rowl::State::GameState::createNextState(
            m_gameState, m_storyRuntime.currentNodeId());
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory = persistence.saveDirectory();
    bool ok = persistence.saveSlot(m_gameState, slotIndex);
    if (!ok) {
        m_context->setError(RuntimeErrorCode::IoError,
                            "Failed to write save slot #" + std::to_string(slotIndex) + " to " + saveDirectory,
                            "save_game_slot", std::to_string(slotIndex));
        return false;
    }
    m_context->setSuccess("save_game_slot", std::to_string(slotIndex));
    return true;
}

bool Engine::loadGameSlot(int32_t slotIndex) {
    if (slotIndex < 0 || slotIndex > 100) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-100)",
                            "load_game_slot", std::to_string(slotIndex));
        return false;
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory = persistence.saveDirectory();
    if (!persistence.hasSlot(slotIndex)) {
        m_context->setError(RuntimeErrorCode::FileNotFound,
                            "Save slot #" + std::to_string(slotIndex) + " not found in " + saveDirectory,
                            "load_game_slot", std::to_string(slotIndex));
        return false;
    }
    auto loaded = persistence.loadSlot(slotIndex);
    if (!loaded) {
        m_context->setError(RuntimeErrorCode::ParseError,
                            "Failed to parse or validate save slot #" + std::to_string(slotIndex),
                            "load_game_slot", std::to_string(slotIndex));
        return false;
    }

    m_gameState = loaded;
    m_storyRuntime.setCurrentNodeId(m_gameState->activeNodeId);
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
            updateSceneFromComponents(compsJson.dump(), false);
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
    ROWL_LOG_INFO("Loaded Game Slot #" + std::to_string(slotIndex) +
                  " → Node #" + std::to_string(m_storyRuntime.currentNodeId()));
    m_context->setSuccess("load_game_slot", std::to_string(slotIndex));
    return true;
}

bool Engine::hasSaveSlot(int32_t slotIndex) const {
    return sessionPersistence().hasSlot(slotIndex);
}

bool Engine::deleteSaveSlot(int32_t slotIndex) {
    if (slotIndex < 0 || slotIndex > 100) {
        m_context->setError(RuntimeErrorCode::InvalidArgument,
                            "Invalid save slot index #" + std::to_string(slotIndex) + " (must be 0-100)",
                            "delete_save_slot", std::to_string(slotIndex));
        return false;
    }
    auto& persistence = sessionPersistence();
    const std::string saveDirectory = persistence.saveDirectory();
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
    m_context->setSuccess("delete_save_slot", std::to_string(slotIndex));
    return true;
}

bool Engine::rewind(uint64_t steps) {
    if (!m_gameState) return false;
    auto rewound = Rowl::State::GameState::rewind(m_gameState, steps);
    if (!rewound || rewound == m_gameState) return false;

    m_gameState = rewound;
    m_storyRuntime.setCurrentNodeId(m_gameState->activeNodeId);

    if (m_luaSandbox && m_gameState->variables) {
        m_luaSandbox->clearVariables();
        for (const auto& [k, v] : m_gameState->variables->data) {
            m_luaSandbox->setVariable(k, v);
        }
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
            updateSceneFromComponents(compsJson.dump(), false);
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
    return true;
}

void Engine::restoreAudioStateFromGameState() {
    if (!m_audio || !m_gameState) return;
    m_audio->setBgmVolume(m_gameState->bgmVolume);
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
    if (m_luaSandbox) {
        bool ok = m_luaSandbox->evaluateCondition(conditionExpr);
        if (!ok && !m_luaSandbox->getLastError().empty()) {
            m_context->setError(RuntimeErrorCode::ScriptSyntaxError,
                                m_luaSandbox->getLastError(),
                                "evaluate_condition", conditionExpr);
            return false;
        }
        m_context->setSuccess("evaluate_condition", conditionExpr);
        return ok;
    }
    m_context->setSuccess("evaluate_condition", conditionExpr);
    return true;
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
