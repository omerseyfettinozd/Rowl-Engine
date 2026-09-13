#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"
#include "rowl/render/frame_composition.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include "rowl/platform/mobile_input.hpp"
#include "rowl/platform/sdl_subsystem_lease.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <fstream>

#ifndef ROWL_SHADER_DIR
#define ROWL_SHADER_DIR ""
#endif

namespace Rowl::Render {

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

void fnvMixBytes(uint64_t& hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kFnvPrime;
    }
}

void fnvMixString(uint64_t& hash, const std::string& value) {
    fnvMixBytes(hash, value.data(), value.size());
    uint64_t terminator = 0xFFULL;
    fnvMixBytes(hash, &terminator, sizeof(terminator));
}

void fnvMixF32(uint64_t& hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    fnvMixBytes(hash, &bits, sizeof(bits));
}

void fnvMixBool(uint64_t& hash, bool value) {
    const uint8_t byte = value ? 1u : 0u;
    fnvMixBytes(hash, &byte, sizeof(byte));
}

void fnvMixI32(uint64_t& hash, int value) {
    const auto bits = static_cast<int32_t>(value);
    fnvMixBytes(hash, &bits, sizeof(bits));
}

void fnvMixU64(uint64_t& hash, uint64_t value) {
    fnvMixBytes(hash, &value, sizeof(value));
}

static SDL_Color parseHexColor(const std::string& hex, uint8_t defaultA = 255) {
    if (hex.empty()) return {255, 255, 255, defaultA};
    std::string clean = hex;
    if (clean[0] == '#') clean = clean.substr(1);

    uint32_t val = 0;
    try {
        val = std::stoul(clean, nullptr, 16);
    } catch (...) {
        return {255, 255, 255, defaultA};
    }

    if (clean.length() == 6) {
        return {
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF),
            defaultA
        };
    } else if (clean.length() == 8) {
        return {
            static_cast<uint8_t>((val >> 24) & 0xFF),
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF)
        };
    }
    return {255, 255, 255, defaultA};
}

constexpr int kMaxTextureDimension = 8'192;
constexpr uint64_t kMaxTexturePixels = 16ULL * 1024 * 1024;
constexpr uint64_t kMinimumTextureCacheBytes = 1ULL * 1024ULL * 1024ULL;
constexpr size_t kMaxMissingTextureCacheEntries = 512;
float touchCoordinateToPhysical(float normalized, uint32_t extent) {
    return std::clamp(normalized, 0.0f, 1.0f) * static_cast<float>(extent);
}

bool hasSafeTextureDimensions(int width, int height) {
    return width > 0 && height > 0 &&
           width <= kMaxTextureDimension && height <= kMaxTextureDimension &&
           static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= kMaxTexturePixels;
}

unsigned char* loadTextureMemorySafely(const uint8_t* bytes, int byteCount,
                                       int* width, int* height, int* channels) {
    int infoWidth = 0, infoHeight = 0, infoChannels = 0;
    if (!stbi_info_from_memory(bytes, byteCount, &infoWidth, &infoHeight, &infoChannels)) return nullptr;
    if (!hasSafeTextureDimensions(infoWidth, infoHeight)) {
        ROWL_LOG_WARN("Rejected texture buffer with unsafe dimensions");
        return nullptr;
    }
    return stbi_load_from_memory(bytes, byteCount, width, height, channels, 4);
}

std::vector<uint8_t> loadMsdfShaderCode() {
#if !ROWL_GPU_MSDF_SHADER_AVAILABLE
    return {};
#else
    std::vector<std::filesystem::path> candidates;
    if (const char* basePath = SDL_GetBasePath()) {
        candidates.emplace_back(basePath);
        candidates.back() /= "shaders/msdf_text.frag.spv";
    }
    if constexpr (sizeof(ROWL_SHADER_DIR) > 1) {
        candidates.emplace_back(ROWL_SHADER_DIR);
        candidates.back() /= "msdf_text.frag.spv";
    }

    for (const auto& path : candidates) {
        std::ifstream shader(path, std::ios::binary | std::ios::ate);
        if (!shader) continue;
        const auto size = shader.tellg();
        if (size <= 0) continue;
        std::vector<uint8_t> code(static_cast<size_t>(size));
        shader.seekg(0);
        shader.read(reinterpret_cast<char*>(code.data()), size);
        if (shader) return code;
    }
    return {};
#endif
}

} // namespace

Window::Window(Rowl::VFS::VFSManager* vfs)
    : m_vfs(vfs),
      m_camera(std::make_unique<Camera2D>(1920.0f, 1080.0f)),
      m_transitionManager(std::make_unique<TransitionManager>()) {
    if (!m_vfs) {
        m_ownedVfs = std::make_shared<Rowl::VFS::VFSManager>();
        m_vfs = m_ownedVfs.get();
    }
}

Window::~Window() {
    if (m_initialized) {
        shutdown();
    }
}

void Window::setVfs(Rowl::VFS::VFSManager* vfs) {
    invalidateFrameCache();
    if (vfs) {
        m_ownedVfs.reset();
        m_vfs = vfs;
    } else {
        m_ownedVfs = std::make_shared<Rowl::VFS::VFSManager>();
        m_vfs = m_ownedVfs.get();
    }
}

Rowl::VFS::VFSManager& Window::vfs() const {
    return *m_vfs;
}

bool Window::initializeOffscreen(uint32_t width, uint32_t height) {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing SDL3 Offscreen Surface & Software Renderer (" +
                  std::to_string(width) + "x" + std::to_string(height) + ")...");

    if (!Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }
    m_videoLeaseHeld = true;

    m_width = width;
    m_height = height;

    m_offscreenSurface = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    if (!m_offscreenSurface) {
        ROWL_LOG_ERROR("SDL_CreateSurface (offscreen) failed: " + std::string(SDL_GetError()));
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }

    m_sdlRenderer = SDL_CreateSoftwareRenderer(m_offscreenSurface);
    if (!m_sdlRenderer) {
        ROWL_LOG_ERROR("SDL_CreateSoftwareRenderer failed: " + std::string(SDL_GetError()));
        SDL_DestroySurface(m_offscreenSurface);
        m_offscreenSurface = nullptr;
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }

    m_isOpen = true;
    m_initialized = true;
    m_isOffscreen = true;

    initFontRenderer();

    ROWL_LOG_INFO("SDL3 Offscreen Engine Surface initialized (" +
                  std::to_string(width) + "x" + std::to_string(height) + " RGBA32)");
    return true;
}

const uint8_t* Window::getPixelBuffer() const {
    if (!m_offscreenSurface) return nullptr;
    return static_cast<const uint8_t*>(m_offscreenSurface->pixels);
}

uint32_t Window::getPixelPitch() const {
    if (!m_offscreenSurface) return 0;
    return static_cast<uint32_t>(m_offscreenSurface->pitch);
}

bool Window::initialize(const std::string& title, uint32_t width, uint32_t height, bool vsync) {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing SDL3 Windowing & Hardware Graphics Subsystem...");

    if (!Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }
    m_videoLeaseHeld = true;

    m_width = width;
    m_height = height;

    m_sdlWindow = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!m_sdlWindow) {
        ROWL_LOG_ERROR("SDL_CreateWindow failed: " + std::string(SDL_GetError()));
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }
    m_eventWindowId = SDL_GetWindowID(m_sdlWindow);
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(m_eventWindowId)) {
        ROWL_LOG_ERROR("Visible SDL windows must share one UI/event thread.");
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
        m_eventWindowId = 0;
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }
    // Keep SDL_Renderer command semantics for sprites/UI while using its GPU
    // backend, which permits an MSDF fragment state only around text draws.
    m_sdlRenderer = SDL_CreateGPURenderer(nullptr, m_sdlWindow);
    if (!m_sdlRenderer) {
        ROWL_LOG_WARN("GPU renderer unavailable; preserving standard SDL renderer fallback: " + std::string(SDL_GetError()));
        m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, nullptr);
    }
    if (!m_sdlRenderer) {
        ROWL_LOG_ERROR("SDL_CreateRenderer failed: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(m_eventWindowId);
        m_eventWindowId = 0;
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }

    if (vsync) {
        SDL_SetRenderVSync(m_sdlRenderer, 1);
    }

    m_isOpen = true;
    m_initialized = true;

    initGpuMsdfRenderer();

    ROWL_LOG_INFO("SDL3 Window successfully created (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    return true;
}

void Window::initGpuMsdfRenderer() {
    if (!m_sdlRenderer || m_isOffscreen) return;
    auto* device = SDL_GetGPURendererDevice(m_sdlRenderer);
    if (!device || !(SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV)) return;
    const auto code = loadMsdfShaderCode();
    if (code.empty()) { ROWL_LOG_WARN("MSDF GPU shader artifact is unavailable; using font fallback"); return; }
    SDL_GPUShaderCreateInfo info{};
    info.code = code.data(); info.code_size = code.size(); info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV; info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    m_msdfFragmentShader = SDL_CreateGPUShader(device, &info);
    if (!m_msdfFragmentShader) { ROWL_LOG_WARN("MSDF GPU shader could not be created: " + std::string(SDL_GetError())); return; }
    SDL_GPURenderStateCreateInfo stateInfo{};
    stateInfo.fragment_shader = m_msdfFragmentShader;
    m_msdfRenderState = SDL_CreateGPURenderState(m_sdlRenderer, &stateInfo);
    if (!m_msdfRenderState) { ROWL_LOG_WARN("MSDF GPU render state could not be created: " + std::string(SDL_GetError())); SDL_ReleaseGPUShader(device, m_msdfFragmentShader); m_msdfFragmentShader = nullptr; }
    if (!m_msdfRenderState) return;
    const auto metadata = vfs().readString("fonts/msdf/default.json");
    m_msdfRenderer = std::make_unique<MsdfRenderer>();
    m_msdfAtlasTexture = loadTexture("fonts/msdf/default.png");
    if (metadata.empty() || !m_msdfAtlasTexture || !m_msdfRenderer->loadAtlasMetadata(metadata)) {
        m_msdfRenderer.reset(); m_msdfAtlasTexture = nullptr;
        ROWL_LOG_WARN("MSDF atlas unavailable; using font fallback");
    } else {
        ROWL_LOG_INFO("MSDF GPU text renderer initialized");
    }
}

bool Window::renderGpuMsdfText(const std::string& text, float x, float baseline, float px, SDL_Color color) {
    if (!m_msdfRenderState || !m_msdfRenderer || !m_msdfAtlasTexture) return false;
    if (!SDL_SetGPURenderState(m_sdlRenderer, m_msdfRenderState)) return false;
    float pen = x;
    for (unsigned char c : text) {
        const auto* glyph = m_msdfRenderer->findGlyph(c);
        if (!glyph) { pen += px * .5f; continue; }
        SDL_FRect src{glyph->atlasLeft, glyph->atlasTop, glyph->atlasRight-glyph->atlasLeft, glyph->atlasBottom-glyph->atlasTop};
        SDL_FRect dst{pen + glyph->planeLeft*px, baseline + glyph->planeTop*px,
                      (glyph->planeRight-glyph->planeLeft)*px, (glyph->planeBottom-glyph->planeTop)*px};
        SDL_SetTextureColorMod(m_msdfAtlasTexture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(m_msdfAtlasTexture, color.a);
        SDL_RenderTexture(m_sdlRenderer, m_msdfAtlasTexture, &src, &dst);
        pen += glyph->advance*px;
    }
    SDL_SetGPURenderState(m_sdlRenderer, nullptr);
    return true;
}

void Window::shutdownGpuMsdfRenderer() {
    if (m_msdfRenderState) { SDL_DestroyGPURenderState(m_msdfRenderState); m_msdfRenderState = nullptr; }
    if (m_msdfFragmentShader && m_sdlRenderer) {
        if (auto* device = SDL_GetGPURendererDevice(m_sdlRenderer)) SDL_ReleaseGPUShader(device, m_msdfFragmentShader);
        m_msdfFragmentShader = nullptr;
    }
}

bool Window::initializeEmbedded(void* nativeHandle, uint32_t width, uint32_t height, bool vsync) {
    if (m_initialized) return true;
    if (!nativeHandle) {
        ROWL_LOG_ERROR("initializeEmbedded called with null native handle!");
        return false;
    }

    ROWL_LOG_INFO("Initializing SDL3 in Embedded mode (native handle: " +
                  std::to_string(reinterpret_cast<uintptr_t>(nativeHandle)) + ")");

    if (!Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }
    m_videoLeaseHeld = true;

    m_width  = width;
    m_height = height;

    // SDL3 native handle embedding via properties
    SDL_PropertiesID props = SDL_CreateProperties();
#if defined(_WIN32)
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, nativeHandle);
#elif defined(__APPLE__)
    SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_COCOA_WINDOW_POINTER, nativeHandle);
#else
    // X11 Window XID
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER,
                          static_cast<Sint64>(reinterpret_cast<uintptr_t>(nativeHandle)));
#endif
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, true);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,  static_cast<Sint64>(width));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<Sint64>(height));

    m_sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (!m_sdlWindow) {
        ROWL_LOG_ERROR("SDL_CreateWindowWithProperties (embedded) failed: " + std::string(SDL_GetError()));
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }
    m_eventWindowId = SDL_GetWindowID(m_sdlWindow);
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(m_eventWindowId)) {
        ROWL_LOG_ERROR("Visible SDL windows must share one UI/event thread.");
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
        m_eventWindowId = 0;
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }
    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, nullptr);
    if (!m_sdlRenderer) {
        ROWL_LOG_ERROR("SDL_CreateRenderer (embedded) failed: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(m_eventWindowId);
        m_eventWindowId = 0;
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
        return false;
    }

    if (vsync) SDL_SetRenderVSync(m_sdlRenderer, 1);

    m_isOpen      = true;
    m_initialized = true;
    m_isEmbedded  = true;

    ROWL_LOG_INFO("SDL3 Embedded Window initialized (" +
                  std::to_string(width) + "x" + std::to_string(height) + ")");
    initFontRenderer();
    return true;
}

void Window::reloadFonts() {
    invalidateFrameCache();
    initFontRenderer();
    if (!m_msdfRenderer) initGpuMsdfRenderer();
}

void Window::initFontRenderer() {
    if (!m_fontRenderer) {
        m_fontRenderer = std::make_unique<FontRenderer>();
    }

    namespace fs = std::filesystem;

    // 1. Try VFS Manager memory buffer candidates
    std::vector<std::string> vfsFontCandidates = {
        "fonts/default.ttf",
        "fonts/default_bold.ttf",
        "Assets/fonts/default.ttf",
        "Assets/fonts/default_bold.ttf",
        "default.ttf",
        "default_bold.ttf"
    };

    for (const auto& vf : vfsFontCandidates) {
        auto bytes = vfs().readBytes(vf);
        if (!bytes.empty()) {
            if (m_fontRenderer->loadFontFromMemory(bytes.data(), bytes.size())) {
                ROWL_LOG_INFO("✅ Loaded Visual Novel Font from VFS [" + vf + "]");
                return;
            }
        }
    }

    // 2. System fonts are a non-project fallback for readable debug output.
    // Project fonts must come through the VFS above, whose mount set is
    // established from the selected project root.
    const std::vector<std::string> systemFontCandidates = {
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/cantarell/Cantarell-VF.otf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFPro.ttf"
    };

    for (const auto& path : systemFontCandidates) {
        if (fs::exists(path) && fs::is_regular_file(path)) {
            if (m_fontRenderer->loadFont(path)) {
                ROWL_LOG_INFO("✅ Loaded Visual Novel TTF Font from System: " + path);
                return;
            }
        }
    }

    ROWL_LOG_WARN("⚠️ No TrueType Font could be loaded. Fallback debug text will be used.");
}

void Window::resizeViewport(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth < 50 || newHeight < 50) return;
    m_width  = newWidth;
    m_height = newHeight;
    if (m_sdlWindow) {
        SDL_SetWindowSize(m_sdlWindow, static_cast<int>(newWidth), static_cast<int>(newHeight));
    }
    ROWL_LOG_INFO("Viewport resized to " + std::to_string(newWidth) + "x" + std::to_string(newHeight));
}

bool Window::mapPhysicalToVirtual(float physicalX, float physicalY,
                                  uint32_t virtualWidth, uint32_t virtualHeight,
                                  float& outVirtualX, float& outVirtualY,
                                  bool& outBezelTap) const {
    outBezelTap = false;
    outVirtualX = 0.0f;
    outVirtualY = 0.0f;
    const ViewportMetrics metrics =
        AspectGuardian::calculateViewport(m_width, m_height, virtualWidth, virtualHeight);
    if (metrics.scaleFactor <= 0.0f) return false;
    // Letterbox/pillarbox margins are not story canvas. Report them so the
    // caller can consume the tap without advancing the story.
    if (!AspectGuardian::containsPhysicalPoint(physicalX, physicalY, metrics)) {
        outBezelTap = true;
        return true;
    }
    outVirtualX = (physicalX - static_cast<float>(metrics.x)) / metrics.scaleFactor;
    outVirtualY = (physicalY - static_cast<float>(metrics.y)) / metrics.scaleFactor;
    return true;
}

void Window::setInputHandler(std::function<void(const Rowl::Platform::RuntimeInputEvent&)> handler) {
    m_inputHandler = std::move(handler);
}

void Window::startTransition(const std::string& kind, float durationSeconds, const std::string& colorHex) {
    if (!m_transitionManager) return;
    m_transitionManager->captureSnapshot(m_offscreenSurface, m_sdlRenderer);
    m_transitionManager->startTransitionFromKind(kind, durationSeconds, colorHex);
}

bool Window::isTransitionActive() const {
    return m_transitionManager && m_transitionManager->isTransitionActive();
}

void Window::update(float dt) {
    if (m_camera) m_camera->update(dt);
    if (m_transitionManager) m_transitionManager->update(dt);

    if (m_screenFx.flashActive) {
        m_screenFx.flashElapsed += dt;
        if (m_screenFx.flashElapsed >= m_screenFx.flashDuration) {
            m_screenFx.flashActive = false;
            m_screenFx.flashElapsed = 0.0f;
        }
    }
}

void Window::triggerScreenFlash(uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity) {
    if (!std::isfinite(durationSeconds) || !std::isfinite(intensity)) return;
    if (durationSeconds <= 0.0f) {
        m_screenFx.flashActive = false;
        m_screenFx.flashDuration = 0.0f;
        m_screenFx.flashElapsed = 0.0f;
        return;
    }
    m_screenFx.flashActive = true;
    m_screenFx.flashR = r;
    m_screenFx.flashG = g;
    m_screenFx.flashB = b;
    m_screenFx.flashDuration = std::clamp(durationSeconds, 0.01f, 60.0f);
    m_screenFx.flashElapsed = 0.0f;
    m_screenFx.flashIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

void Window::triggerScreenFlashHex(const std::string& colorHex, float durationSeconds, float intensity) {
    SDL_Color c = parseHexColor(colorHex, 255);
    triggerScreenFlash(c.r, c.g, c.b, durationSeconds, intensity);
}

bool Window::isScreenFlashActive() const {
    return m_screenFx.flashActive;
}

float Window::getScreenFlashProgress() const {
    if (!m_screenFx.flashActive || m_screenFx.flashDuration <= 0.0f) return 1.0f;
    return std::clamp(m_screenFx.flashElapsed / m_screenFx.flashDuration, 0.0f, 1.0f);
}

void Window::setScreenTint(uint8_t r, uint8_t g, uint8_t b, float opacity) {
    if (!std::isfinite(opacity)) return;
    if (opacity <= 0.001f) {
        clearScreenTint();
        return;
    }
    m_screenFx.hasTint = true;
    m_screenFx.tintR = r;
    m_screenFx.tintG = g;
    m_screenFx.tintB = b;
    m_screenFx.tintOpacity = std::clamp(opacity, 0.0f, 1.0f);
}

void Window::setScreenTintHex(const std::string& colorHex, float opacity) {
    if (colorHex.empty()) {
        clearScreenTint();
        return;
    }
    SDL_Color c = parseHexColor(colorHex, 255);
    setScreenTint(c.r, c.g, c.b, opacity);
}

void Window::clearScreenTint() {
    m_screenFx.hasTint = false;
    m_screenFx.tintOpacity = 0.0f;
}

float Window::getScreenTintOpacity() const {
    return m_screenFx.hasTint ? m_screenFx.tintOpacity : 0.0f;
}

bool Window::hasScreenTint() const {
    return m_screenFx.hasTint && m_screenFx.tintOpacity > 0.001f;
}

void Window::setVignette(float intensity, float radius, const std::string& colorHex) {
    if (!std::isfinite(intensity) || !std::isfinite(radius)) return;
    if (intensity <= 0.001f) {
        m_screenFx.vignetteEnabled = false;
        m_screenFx.vignetteIntensity = 0.0f;
        return;
    }
    m_screenFx.vignetteEnabled = true;
    m_screenFx.vignetteIntensity = std::clamp(intensity, 0.0f, 1.0f);
    m_screenFx.vignetteRadius = std::clamp(radius, 0.0f, 1.0f);
    SDL_Color c = parseHexColor(colorHex, 255);
    m_screenFx.vignetteR = c.r;
    m_screenFx.vignetteG = c.g;
    m_screenFx.vignetteB = c.b;
}

float Window::getVignetteIntensity() const {
    return m_screenFx.vignetteEnabled ? m_screenFx.vignetteIntensity : 0.0f;
}

bool Window::isVignetteActive() const {
    return m_screenFx.vignetteEnabled && m_screenFx.vignetteIntensity > 0.001f;
}

void Window::ensureVignetteTexture() {
    if (m_vignetteTexture || !m_sdlRenderer) return;

    constexpr int kVignetteSize = 256;
    SDL_Surface* surface = SDL_CreateSurface(kVignetteSize, kVignetteSize, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return;

    uint32_t* pixels = static_cast<uint32_t*>(surface->pixels);
    const float center = (kVignetteSize - 1) * 0.5f;
    const float maxRadius = center * 1.41421356f;
    const float innerRadius = center * m_screenFx.vignetteRadius;

    for (int y = 0; y < kVignetteSize; ++y) {
        for (int x = 0; x < kVignetteSize; ++x) {
            float dx = static_cast<float>(x) - center;
            float dy = static_cast<float>(y) - center;
            float dist = std::sqrt(dx * dx + dy * dy);

            float alpha = 0.0f;
            if (dist > innerRadius) {
                float norm = (dist - innerRadius) / (maxRadius - innerRadius);
                norm = std::clamp(norm, 0.0f, 1.0f);
                alpha = norm * norm * (3.0f - 2.0f * norm); // smoothstep
            }
            uint8_t a = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
            pixels[y * kVignetteSize + x] = SDL_MapRGBA(SDL_GetPixelFormatDetails(surface->format), nullptr, 255, 255, 255, a);
        }
    }

    m_vignetteTexture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
    SDL_DestroySurface(surface);
}

void Window::renderScreenEffects(const ViewportMetrics& metrics) {
    if (!m_sdlRenderer) return;

    SDL_FRect canvasRect = {
        static_cast<float>(metrics.x),
        static_cast<float>(metrics.y),
        static_cast<float>(metrics.width),
        static_cast<float>(metrics.height)
    };

    // 1. Color Tint Overlay
    if (m_screenFx.hasTint && m_screenFx.tintOpacity > 0.001f) {
        Uint8 alpha = static_cast<Uint8>(std::clamp(m_screenFx.tintOpacity, 0.0f, 1.0f) * 255.0f);
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_sdlRenderer, m_screenFx.tintR, m_screenFx.tintG, m_screenFx.tintB, alpha);
            SDL_RenderFillRect(m_sdlRenderer, &canvasRect);
        }
    }

    // 2. Vignette Post-Process
    if (m_screenFx.vignetteEnabled && m_screenFx.vignetteIntensity > 0.001f) {
        ensureVignetteTexture();
        if (m_vignetteTexture) {
            Uint8 vAlpha = static_cast<Uint8>(std::clamp(m_screenFx.vignetteIntensity, 0.0f, 1.0f) * 255.0f);
            SDL_SetTextureBlendMode(m_vignetteTexture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureColorMod(m_vignetteTexture, m_screenFx.vignetteR, m_screenFx.vignetteG, m_screenFx.vignetteB);
            SDL_SetTextureAlphaMod(m_vignetteTexture, vAlpha);
            SDL_RenderTexture(m_sdlRenderer, m_vignetteTexture, nullptr, &canvasRect);
        }
    }

    // 3. Screen Flash (quadratic decay for sudden hit/lightning)
    if (m_screenFx.flashActive && m_screenFx.flashDuration > 0.0f) {
        float progress = std::clamp(m_screenFx.flashElapsed / m_screenFx.flashDuration, 0.0f, 1.0f);
        float decay = 1.0f - progress;
        float alphaFactor = decay * decay * m_screenFx.flashIntensity;
        Uint8 alpha = static_cast<Uint8>(std::clamp(alphaFactor, 0.0f, 1.0f) * 255.0f);
        if (alpha > 0) {
            SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_sdlRenderer, m_screenFx.flashR, m_screenFx.flashG, m_screenFx.flashB, alpha);
            SDL_RenderFillRect(m_sdlRenderer, &canvasRect);
        }
    }
}

void Window::pollEvents(bool& outShouldQuit) {
    if (!m_initialized) return;

    if (m_eventWindowId == 0) return;
    for (const SDL_Event& event : Rowl::Platform::SdlEventDispatcher::takeEvents(m_eventWindowId)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                outShouldQuit = true;
                m_isOpen = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    ROWL_LOG_INFO("Escape key pressed. Requesting exit...");
                    outShouldQuit = true;
                    m_isOpen = false;
                } else if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::Advance});
                } else if (event.key.key == SDLK_F5) {
                    ROWL_LOG_INFO("[Player] F5 pressed: Quick Saving to Slot #0...");
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::QuickSave});
                } else if (event.key.key == SDLK_F9) {
                    ROWL_LOG_INFO("[Player] F9 pressed: Quick Loading from Slot #0...");
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::QuickLoad});
                } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_Z) {
                    ROWL_LOG_INFO("[Player] Rewind key pressed: Rewinding 1 step...");
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::Rewind});
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerDown,
                                                        event.button.x, event.button.y});
                }
                break;
            case SDL_EVENT_FINGER_DOWN: {
                const float x = touchCoordinateToPhysical(event.tfinger.x, m_width);
                const float y = touchCoordinateToPhysical(event.tfinger.y, m_height);
                m_touchStarts[event.tfinger.fingerID] = {x, y};
                break;
            }
            case SDL_EVENT_FINGER_UP: {
                const auto start = m_touchStarts.find(event.tfinger.fingerID);
                if (start == m_touchStarts.end()) break;

                const float x = touchCoordinateToPhysical(event.tfinger.x, m_width);
                const float y = touchCoordinateToPhysical(event.tfinger.y, m_height);
                const auto gesture = Rowl::Platform::MobileInput::classifyTouchGesture(
                    start->second.first, start->second.second, x, y,
                    static_cast<float>(m_width), static_cast<float>(m_height));
                m_touchStarts.erase(start);
                if (gesture == Rowl::Platform::InputEventType::SwipeForward) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::SwipeForward, x, y});
                } else if (gesture == Rowl::Platform::InputEventType::SwipeBack) {
                    if (m_inputHandler) m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::SwipeBack, x, y});
                } else if (m_inputHandler) {
                    m_inputHandler({Rowl::Platform::RuntimeInputEvent::Type::PointerDown, x, y});
                }
                break;
            }
            case SDL_EVENT_FINGER_CANCELED:
                m_touchStarts.erase(event.tfinger.fingerID);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                int rw = 0, rh = 0;
                if (SDL_GetRenderOutputSize(m_sdlRenderer, &rw, &rh) && rw > 0 && rh > 0) {
                    m_width = static_cast<uint32_t>(rw);
                    m_height = static_cast<uint32_t>(rh);
                } else {
                    m_width = static_cast<uint32_t>(event.window.data1);
                    m_height = static_cast<uint32_t>(event.window.data2);
                }
                ROWL_LOG_TRACE("Window resized to physical render output: " + std::to_string(m_width) + "x" + std::to_string(m_height));
                break;
        }
    }
}

void Window::beginFrame() {
    if (!m_initialized || !m_sdlRenderer) return;

    // Dark sleek theme background (#1A1A24)
    SDL_SetRenderDrawColor(m_sdlRenderer, 26, 26, 36, 255);
    SDL_RenderClear(m_sdlRenderer);
}

void Window::clearTextureCache() {
    invalidateFrameCache();
    const bool hadMsdfAtlas = m_msdfAtlasTexture != nullptr;
    std::unordered_set<SDL_Texture*> uniqueTextures;
    for (auto& [name, tex] : m_textureCache) {
        if (tex) {
            uniqueTextures.insert(tex);
        }
    }
    for (auto* tex : uniqueTextures) {
        SDL_DestroyTexture(tex);
    }
    m_textureCache.clear();
    m_textureMemoryBytes.clear();
    m_textureLastUsed.clear();
    m_missingTextureCache.clear();
    m_budgetRejectedTextureCache.clear();
    m_buttonFontCache.clear();
    m_textureCacheEvictionCount = 0;
    m_textureUseClock = 0;
    m_msdfAtlasTexture = nullptr;
    if (hadMsdfAtlas) {
        m_msdfRenderer.reset();
        shutdownGpuMsdfRenderer();
    }
    ROWL_LOG_INFO("Hardware Texture Cache Cleared (" + std::to_string(uniqueTextures.size()) + " unique textures freed).");
}

void Window::setTextureCacheBudgetBytes(uint64_t bytes) {
    const uint64_t previousBudget = m_textureCacheBudgetBytes;
    m_textureCacheBudgetBytes = std::max(bytes, kMinimumTextureCacheBytes);
    if (m_textureCacheBudgetBytes > previousBudget) {
        // A previously oversized asset may now fit; do not keep its fallback
        // state after the host moves to a larger device profile.
        m_budgetRejectedTextureCache.clear();
    }
    if (!evictTexturesToFit(0)) {
        ROWL_LOG_WARN("Texture cache budget cannot be met while the MSDF atlas is pinned");
    }
}

uint64_t Window::getTextureCacheBytes() const {
    uint64_t total = 0;
    for (const auto& [texture, bytes] : m_textureMemoryBytes) {
        (void)texture;
        total += bytes;
    }
    return total;
}

void Window::touchTexture(SDL_Texture* texture) {
    if (texture) m_textureLastUsed[texture] = ++m_textureUseClock;
}

void Window::rememberMissingTexture(std::string path) {
    if (m_missingTextureCache.contains(path)) return;
    if (m_missingTextureCache.size() >= kMaxMissingTextureCacheEntries) {
        m_missingTextureCache.erase(m_missingTextureCache.begin());
    }
    m_missingTextureCache.insert(std::move(path));
}

void Window::destroyCachedTexture(SDL_Texture* texture) {
    if (!texture) return;
    for (auto it = m_textureCache.begin(); it != m_textureCache.end();) {
        if (it->second == texture) {
            it = m_textureCache.erase(it);
        } else {
            ++it;
        }
    }
    m_textureMemoryBytes.erase(texture);
    m_textureLastUsed.erase(texture);
    SDL_DestroyTexture(texture);
}

bool Window::evictTexturesToFit(uint64_t incomingBytes) {
    if (incomingBytes > m_textureCacheBudgetBytes) return false;
    while (getTextureCacheBytes() > m_textureCacheBudgetBytes - incomingBytes) {
        SDL_Texture* leastRecentlyUsed = nullptr;
        uint64_t oldestUse = UINT64_MAX;
        for (const auto& [texture, lastUsed] : m_textureLastUsed) {
            if (texture != m_msdfAtlasTexture && lastUsed < oldestUse) {
                leastRecentlyUsed = texture;
                oldestUse = lastUsed;
            }
        }
        if (!leastRecentlyUsed) return false;
        ROWL_LOG_INFO("Evicting least-recently-used texture to respect cache budget");
        destroyCachedTexture(leastRecentlyUsed);
        ++m_textureCacheEvictionCount;
    }
    return true;
}

SDL_Texture* Window::loadTexture(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;

    // Normalize slashes
    std::string normPath = filename;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    if (m_missingTextureCache.contains(normPath)) return nullptr;
    if (m_budgetRejectedTextureCache.contains(normPath)) return nullptr;

    // Cache hit: only return valid textures
    auto it = m_textureCache.find(normPath);
    if (it != m_textureCache.end() && it->second != nullptr) {
        touchTexture(it->second);
        return it->second;
    }

    const auto loadStarted = std::chrono::steady_clock::now();
    const auto recordLoadTime = [this, loadStarted] {
        if (!m_collectingFrameProfile) return;
        m_lastFrameTextureLoadMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - loadStarted).count();
    };

    int width = 0, height = 0, channels = 0;
    unsigned char* data = nullptr;
    std::string sourceInfo;

    std::string bareName = std::filesystem::path(normPath).filename().string();

    // Asset paths are resolved only through the selected project's VFS.
    // This prevents CWD, parent-directory, or arbitrary absolute paths from
    // silently becoming runtime assets after a project switch.
    std::vector<std::string> vfsCandidates = {
        normPath,
        bareName,
        "images/" + bareName,
        "images/" + normPath,
        "Assets/images/" + bareName,
        "Assets/images/" + normPath,
        "Assets/" + bareName,
        "Assets/" + normPath
    };

    for (const auto& candidate : vfsCandidates) {
        auto bytes = vfs().readBytes(candidate);
        if (!bytes.empty()) {
            data = loadTextureMemorySafely(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels);
            if (data) {
                sourceInfo = "VFS [" + candidate + "]";
                break;
            }
        }
    }

    if (!data) {
        rememberMissingTexture(std::move(normPath));
        recordLoadTime();
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_RGBA32, data, width * 4
    );

    if (!surface) {
        stbi_image_free(data);
        recordLoadTime();
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(data);

    if (texture) {
        const uint64_t textureBytes = static_cast<uint64_t>(width) *
                                      static_cast<uint64_t>(height) * 4ULL;
        if (!evictTexturesToFit(textureBytes)) {
            SDL_DestroyTexture(texture);
            if (m_budgetRejectedTextureCache.size() >= kMaxMissingTextureCacheEntries) {
                m_budgetRejectedTextureCache.erase(m_budgetRejectedTextureCache.begin());
            }
            m_budgetRejectedTextureCache.insert(normPath);
            ROWL_LOG_WARN("Texture exceeds the configured cache budget: " + filename);
            recordLoadTime();
            return nullptr;
        }
        m_missingTextureCache.erase(normPath);
        m_textureCache[filename] = texture;
        m_textureCache[normPath] = texture;
        m_textureCache[bareName] = texture;
        m_textureMemoryBytes[texture] = textureBytes;
        touchTexture(texture);
        ROWL_LOG_INFO("✅ Loaded Hardware Texture: " + filename + " (" + std::to_string(width) + "x" + std::to_string(height) + ") from " + sourceInfo);
    }
    recordLoadTime();
    return texture;
}

void Window::renderVisualNovelFrame(
    bool hasBackground,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::vector<CharacterRenderData>& characters,
    const std::vector<DialogueRenderData>& dialogues,
    const std::vector<ChoiceButtonRenderData>& choices,
    float bgRotation,
    float bgParallaxX,
    float bgParallaxY,
    float bgOpacity
) {
    if (!m_initialized || !m_sdlRenderer) return;
    // Identical-frame fast path: any in-flight camera move, transition or
    // flash forces a re-render; otherwise equal content hashes reuse the
    // readable surface pixels from the previous identical frame.
    const bool dynamicsInFlight =
        (m_camera && m_camera->isMoving()) ||
        (m_transitionManager && m_transitionManager->isTransitionActive()) ||
        m_screenFx.flashActive;
    const uint64_t contentHash = hashFrameContent(
        hasBackground, background, bgX, bgY, bgW, bgH, characters,
        dialogues, choices, bgRotation, bgParallaxX, bgParallaxY, bgOpacity);
    if (m_frameCacheValid && !dynamicsInFlight && contentHash == m_lastFrameContentHash) {
        m_lastFrameTextureLoadMilliseconds = 0.0;
        m_lastFrameTextRasterizationMilliseconds = 0.0;
        m_lastFrameRendererFlushMilliseconds = 0.0;
        m_lastFrameNonTextureRenderMilliseconds = 0.0;
        m_lastFrameReusedCache = true;
        return;
    }
    m_lastFrameReusedCache = false;
    const auto frameRenderStarted = std::chrono::steady_clock::now();
    m_lastFrameTextureLoadMilliseconds = 0.0;
    m_lastFrameTextRasterizationMilliseconds = 0.0;
    m_lastFrameRendererFlushMilliseconds = 0.0;
    m_collectingFrameProfile = true;

    // Dynamically query physical size if standalone, or use host-provided size if embedded
    if (!m_isEmbedded) {
        int currentPhysW = 0, currentPhysH = 0;
        if (SDL_GetRenderOutputSize(m_sdlRenderer, &currentPhysW, &currentPhysH) && currentPhysW > 10 && currentPhysH > 10) {
            m_width = static_cast<uint32_t>(currentPhysW);
            m_height = static_cast<uint32_t>(currentPhysH);
        }
    }

    // Safety fallback for collapsed or uninitialized viewport bounds
    if (m_width < 10)  m_width  = 1920;
    if (m_height < 10) m_height = 1080;

    // Calculate Aspect Guardian resolution metrics (1920x1080 virtual canvas)
    ViewportMetrics metrics = AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);

    // Clear physical screen to letterbox black (#0B0F19)
    SDL_SetRenderDrawColor(m_sdlRenderer, 11, 15, 25, 255);
    SDL_RenderClear(m_sdlRenderer);

    // Protect letterbox / pillarbox margins from camera zoom and shake bleed
    SDL_Rect canvasClip = { metrics.x, metrics.y, metrics.width, metrics.height };
    SDL_SetRenderClipRect(m_sdlRenderer, &canvasClip);

    // 1. Render Background Texture or Fill into Virtual Viewport
    if (hasBackground && !background.empty()) {
        float camBgX = bgX, camBgY = bgY, camBgW = bgW, camBgH = bgH;
        if (m_camera) {
            m_camera->transformRectParallax(bgX, bgY, bgW, bgH, bgParallaxX, bgParallaxY, camBgX, camBgY, camBgW, camBgH);
        }
        float physBgX, physBgY;
        AspectGuardian::virtualToPhysical(camBgX, camBgY, metrics, physBgX, physBgY);
        float scaledBgW = camBgW * metrics.scaleFactor;
        float scaledBgH = camBgH * metrics.scaleFactor;
        SDL_FRect vpRect = { physBgX, physBgY, scaledBgW, scaledBgH };

        SDL_Texture* bgTex = loadTexture(background);
        if (bgTex) {
            Uint8 alpha = static_cast<Uint8>(std::clamp(bgOpacity, 0.0f, 1.0f) * 255.0f);
            SDL_SetTextureAlphaMod(bgTex, alpha);
            if (std::abs(bgRotation) > 1e-4f) {
                SDL_RenderTextureRotated(m_sdlRenderer, bgTex, nullptr, &vpRect, static_cast<double>(bgRotation), nullptr, SDL_FLIP_NONE);
            } else {
                SDL_RenderTexture(m_sdlRenderer, bgTex, nullptr, &vpRect);
            }
            SDL_SetTextureAlphaMod(bgTex, 255);
        } else {
            SDL_SetRenderDrawColor(m_sdlRenderer, 20, 24, 38, static_cast<Uint8>(std::clamp(bgOpacity, 0.0f, 1.0f) * 255.0f));
            SDL_RenderFillRect(m_sdlRenderer, &vpRect);
        }
    }

    // 2. Render Character Sprites / Portraits (Multi-Character Support with Proportional Uniform Fit)
    for (const auto& ch : characters) {
        if (ch.sprite.empty()) continue;

        float camCharX = ch.x, camCharY = ch.y, camCharW = ch.width, camCharH = ch.height;
        if (m_camera) {
            m_camera->transformRect(ch.x, ch.y, ch.width, ch.height, camCharX, camCharY, camCharW, camCharH);
        }

        float scaledCharW = camCharW * metrics.scaleFactor;
        float scaledCharH = camCharH * metrics.scaleFactor;
        float physCharX, physCharY;
        AspectGuardian::virtualToPhysical(camCharX, camCharY, metrics, physCharX, physCharY);

        SDL_Texture* charTex = loadTexture(ch.sprite);
        if (charTex) {
            float texW = 0.0f, texH = 0.0f;
            if (SDL_GetTextureSize(charTex, &texW, &texH) && texW > 0.0f && texH > 0.0f && scaledCharW > 0.0f && scaledCharH > 0.0f) {
                // Exact Uniform Proportional Fit inside (physCharX, physCharY, scaledCharW, scaledCharH)
                float texAspect = texW / texH;
                float boxAspect = scaledCharW / scaledCharH;
                float drawW = scaledCharW;
                float drawH = scaledCharH;
                float drawX = physCharX;
                float drawY = physCharY;

                if (texAspect > boxAspect) {
                    // Texture is proportionally wider than bounding box: fit width, center vertically
                    drawW = scaledCharW;
                    drawH = scaledCharW / texAspect;
                    drawY = physCharY + (scaledCharH - drawH) / 2.0f;
                } else {
                    // Texture is proportionally taller than bounding box: fit height, center horizontally
                    drawH = scaledCharH;
                    drawW = scaledCharH * texAspect;
                    drawX = physCharX + (scaledCharW - drawW) / 2.0f;
                }

                SDL_FRect dstRect = { drawX, drawY, drawW, drawH };
                if (std::abs(ch.rotation) > 1e-4f) {
                    SDL_RenderTextureRotated(m_sdlRenderer, charTex, nullptr, &dstRect, static_cast<double>(ch.rotation), nullptr, SDL_FLIP_NONE);
                } else {
                    SDL_RenderTexture(m_sdlRenderer, charTex, nullptr, &dstRect);
                }
            } else {
                SDL_FRect charBox = { physCharX, physCharY, scaledCharW, scaledCharH };
                if (std::abs(ch.rotation) > 1e-4f) {
                    SDL_RenderTextureRotated(m_sdlRenderer, charTex, nullptr, &charBox, static_cast<double>(ch.rotation), nullptr, SDL_FLIP_NONE);
                } else {
                    SDL_RenderTexture(m_sdlRenderer, charTex, nullptr, &charBox);
                }
            }
        } else {
            SDL_FRect charBox = { physCharX, physCharY, scaledCharW, scaledCharH };
            SDL_SetRenderDrawColor(m_sdlRenderer, 30, 41, 59, 220);
            SDL_RenderFillRect(m_sdlRenderer, &charBox);
            SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
            SDL_RenderRect(m_sdlRenderer, &charBox);

            std::string charInfo = "[ CHAR: " + ch.sprite + " ]";
            SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
            SDL_RenderDebugText(m_sdlRenderer, physCharX + 20.0f * metrics.scaleFactor, physCharY + (scaledCharH / 2.0f), charInfo.c_str());
        }
    }

    // Active Scene Transition (Crossfade, Fade to Color, Wipe) applied over scene
    if (m_transitionManager && m_transitionManager->isTransitionActive()) {
        m_transitionManager->renderTransition(m_sdlRenderer, metrics);
    }

    // Screen Visual FX Pipeline (Screen Tint, Screen Flash, Vignette Post-Process)
    renderScreenEffects(metrics);

    // 3. Render Dialogue Boxes (Supports 1 or multiple dialogue boxes simultaneously)
    for (const auto& dlg : dialogues) {
        if (!dlg.hasDialogueBox) continue;

        float scaledDlgW = dlg.width * metrics.scaleFactor;
        float scaledDlgH = dlg.height * metrics.scaleFactor;
        float physBoxX, physBoxY;
        AspectGuardian::virtualToPhysical(dlg.x, dlg.y, metrics, physBoxX, physBoxY);

        SDL_FRect dlgBox = { physBoxX, physBoxY, scaledDlgW, scaledDlgH };

        // Parse custom box styling & opacity
        uint8_t boxAlpha = static_cast<uint8_t>(std::clamp(dlg.boxOpacity, 0.0f, 1.0f) * 255.0f);
        SDL_Color boxColor = parseHexColor(dlg.boxColor, boxAlpha);
        SDL_SetRenderDrawColor(m_sdlRenderer, boxColor.r, boxColor.g, boxColor.b, boxColor.a);
        SDL_RenderFillRect(m_sdlRenderer, &dlgBox);

        // Border
        SDL_Color borderColor = parseHexColor(dlg.borderColor, 255);
        SDL_SetRenderDrawColor(m_sdlRenderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
        SDL_RenderRect(m_sdlRenderer, &dlgBox);
        if (dlg.borderThickness > 1.5f) {
            SDL_FRect innerBox = { physBoxX + 1.0f, physBoxY + 1.0f, scaledDlgW - 2.0f, scaledDlgH - 2.0f };
            SDL_RenderRect(m_sdlRenderer, &innerBox);
        }

        // Speaker Name Tag Badge (if speaker name provided)
        if (!dlg.speaker.empty()) {
            float speakerFontPx = dlg.speakerFontSize * metrics.scaleFactor;
            float speakerTextW = (m_fontRenderer && m_fontRenderer->isLoaded())
                ? m_fontRenderer->measureTextWidth(dlg.speaker, speakerFontPx)
                : (static_cast<float>(dlg.speaker.length()) * 10.0f * metrics.scaleFactor);

            float tagW = std::clamp(speakerTextW + (32.0f * metrics.scaleFactor), 120.0f * metrics.scaleFactor, scaledDlgW * 0.8f);
            float tagH = (dlg.speakerFontSize * 1.4f + 12.0f) * metrics.scaleFactor;
            float tagX = physBoxX + (20.0f * metrics.scaleFactor);
            float tagY = physBoxY - (tagH * 0.6f);

            SDL_FRect speakerTag = { tagX, tagY, tagW, tagH };

            SDL_Color speakerTagColor = parseHexColor(dlg.speakerColor, 255);
            SDL_SetRenderDrawColor(m_sdlRenderer, speakerTagColor.r, speakerTagColor.g, speakerTagColor.b, speakerTagColor.a);
            SDL_RenderFillRect(m_sdlRenderer, &speakerTag);

            // Border on speaker badge
            SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 180);
            SDL_RenderRect(m_sdlRenderer, &speakerTag);

            // Draw speaker name text
            if (!m_fontRenderer || !m_fontRenderer->isLoaded() || !m_offscreenSurface) {
                if (!renderGpuMsdfText(dlg.speaker, tagX + 16.0f * metrics.scaleFactor, tagY + tagH * .72f, speakerFontPx, {255,255,255,255})) {
                    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
                    SDL_RenderDebugText(m_sdlRenderer, tagX + (16.0f * metrics.scaleFactor), tagY + (tagH - 8.0f) / 2.0f, dlg.speaker.c_str());
                }
            }
        }

        // Dialogue Content Text (Debug fallback before present)
        if (!dlg.dialogue.empty()) {
            SDL_Color textColor = parseHexColor(dlg.textColor, 255);

            float paddingLeft = 24.0f * metrics.scaleFactor;
            float paddingTop = 28.0f * metrics.scaleFactor;

            if (!m_fontRenderer || !m_fontRenderer->isLoaded() || !m_offscreenSurface) {
                if (!renderGpuMsdfText(dlg.dialogue, physBoxX + paddingLeft, physBoxY + paddingTop + dlg.fontSize * metrics.scaleFactor, dlg.fontSize * metrics.scaleFactor, textColor)) {
                    SDL_SetRenderDrawColor(m_sdlRenderer, textColor.r, textColor.g, textColor.b, textColor.a);
                    SDL_RenderDebugText(m_sdlRenderer, physBoxX + paddingLeft, physBoxY + paddingTop, dlg.dialogue.c_str());
                }
            }
        }
    }

    // 4. Player choice buttons. Their rectangles use the same 1920x1080
    // virtual coordinate system as every other scene element.
    for (const auto& choice : choices) {
        if (!choice.enabled) continue;
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(choice.x, choice.y, metrics, px, py);
        SDL_FRect rect{px, py, choice.width * metrics.scaleFactor, choice.height * metrics.scaleFactor};
        SDL_Color bg = parseHexColor(choice.backgroundColor, static_cast<uint8_t>(255.0f * std::clamp(choice.opacity, 0.0f, 1.0f)));
        SDL_SetRenderDrawColor(m_sdlRenderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderFillRect(m_sdlRenderer, &rect);
        if (!choice.backgroundImage.empty()) {
            if (auto* texture = loadTexture(choice.backgroundImage)) SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &rect);
        }
        SDL_Color border = parseHexColor(choice.borderColor, 255);
        SDL_SetRenderDrawColor(m_sdlRenderer, border.r, border.g, border.b, border.a);
        SDL_RenderRect(m_sdlRenderer, &rect);
        if (!m_fontRenderer || !m_fontRenderer->isLoaded() || !m_offscreenSurface) {
            SDL_Color text = parseHexColor(choice.textColor, 255);
            if (!renderGpuMsdfText(choice.text, px + 12.0f * metrics.scaleFactor,
                                   py + rect.h * .5f + choice.fontSize * metrics.scaleFactor * .35f,
                                   choice.fontSize * metrics.scaleFactor, text)) {
                SDL_SetRenderDrawColor(m_sdlRenderer, text.r, text.g, text.b, text.a);
                SDL_RenderDebugText(m_sdlRenderer, px + 12.0f, py + rect.h * 0.5f - 4.0f, choice.text.c_str());
            }
        }
    }

    SDL_SetRenderClipRect(m_sdlRenderer, nullptr);

    // In offscreen mode, flush SDL graphics pipeline to m_offscreenSurface BEFORE drawing direct TrueType text
    if (m_isOffscreen && m_sdlRenderer) {
        const auto rendererFlushStarted = std::chrono::steady_clock::now();
        SDL_RenderPresent(m_sdlRenderer);
        m_lastFrameRendererFlushMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - rendererFlushStarted).count();
    }

    // 4. Render High-Quality Anti-Aliased TrueType Text directly onto Offscreen Surface
    if (m_fontRenderer && m_fontRenderer->isLoaded() && m_offscreenSurface) {
        const auto textRasterizationStarted = std::chrono::steady_clock::now();
        for (const auto& dlg : dialogues) {
            if (!dlg.hasDialogueBox) continue;

            float scaledDlgW = dlg.width * metrics.scaleFactor;
            float scaledDlgH = dlg.height * metrics.scaleFactor;
            float physBoxX, physBoxY;
            AspectGuardian::virtualToPhysical(dlg.x, dlg.y, metrics, physBoxX, physBoxY);

            // 4a. Speaker Name Text
            if (!dlg.speaker.empty()) {
                float speakerFontPx = dlg.speakerFontSize * metrics.scaleFactor;
                float speakerTextW = m_fontRenderer->measureTextWidth(dlg.speaker, speakerFontPx);
                float tagW = std::clamp(speakerTextW + (32.0f * metrics.scaleFactor), 120.0f * metrics.scaleFactor, scaledDlgW * 0.8f);
                float tagH = (dlg.speakerFontSize * 1.4f + 12.0f) * metrics.scaleFactor;
                float tagX = physBoxX + (20.0f * metrics.scaleFactor);
                float tagY = physBoxY - (tagH * 0.6f);
                float textDrawY = tagY + (tagH - speakerFontPx) / 2.0f - (2.0f * metrics.scaleFactor);

                m_fontRenderer->renderText(
                    m_offscreenSurface,
                    dlg.speaker,
                    tagX + (16.0f * metrics.scaleFactor),
                    textDrawY,
                    speakerFontPx,
                    {255, 255, 255, 255},
                    tagW - (32.0f * metrics.scaleFactor),
                    tagH,
                    "Left"
                );
            }

            // 4b. Dialogue Content Text (with Typewriter Progression + Text Alignment)
            if (!dlg.dialogue.empty()) {
                SDL_Color textColor = parseHexColor(dlg.textColor, 255);
                float paddingLeft = 24.0f * metrics.scaleFactor;
                float paddingTop = 28.0f * metrics.scaleFactor;
                float maxLineWidth = scaledDlgW - (48.0f * metrics.scaleFactor);
                float maxDialogueHeight = scaledDlgH - (36.0f * metrics.scaleFactor);
                float fontPx = dlg.fontSize * metrics.scaleFactor;

                // Calculate visible codepoints based on typewriter progression
                size_t totalCodepoints = FontRenderer::countCodepoints(dlg.dialogue);
                size_t visibleCodepoints = totalCodepoints;
                if (dlg.isPlaying && dlg.typewriterEnabled && dlg.textSpeed > 0) {
                    float msPerChar = static_cast<float>(dlg.textSpeed);
                    float elapsedMs = dlg.elapsedTypewriterTime * 1000.0f;
                    visibleCodepoints = static_cast<size_t>(elapsedMs / msPerChar);
                    if (visibleCodepoints > totalCodepoints) visibleCodepoints = totalCodepoints;
                }

                m_fontRenderer->renderText(
                    m_offscreenSurface,
                    dlg.dialogue,
                    physBoxX + paddingLeft,
                    physBoxY + paddingTop,
                    fontPx,
                    textColor,
                    maxLineWidth,
                    maxDialogueHeight,
                    dlg.textAlignment,
                    visibleCodepoints
                );
            }
        }
        for (const auto& choice : choices) {
            if (!choice.enabled || choice.text.empty()) continue;
            FontRenderer* choiceFont = m_fontRenderer.get();
            if (!choice.fontFamily.empty() && choice.fontFamily != "Default") {
                auto found = m_buttonFontCache.find(choice.fontFamily);
                if (found == m_buttonFontCache.end()) {
                    auto renderer = std::make_unique<FontRenderer>();
                    auto bytes = vfs().readBytes(choice.fontFamily);
                    if (!bytes.empty() && renderer->loadFontFromMemory(bytes.data(), bytes.size())) {
                        found = m_buttonFontCache.emplace(choice.fontFamily, std::move(renderer)).first;
                    }
                }
                if (found != m_buttonFontCache.end()) choiceFont = found->second.get();
            }
            float px = 0.0f, py = 0.0f;
            AspectGuardian::virtualToPhysical(choice.x, choice.y, metrics, px, py);
            const float w = choice.width * metrics.scaleFactor;
            const float h = choice.height * metrics.scaleFactor;
            const float fontPx = choice.fontSize * metrics.scaleFactor;
            choiceFont->renderText(m_offscreenSurface, choice.text, px + 12.0f * metrics.scaleFactor,
                py + (h - fontPx) * 0.5f, fontPx, parseHexColor(choice.textColor, 255),
                w - 24.0f * metrics.scaleFactor, h, choice.textAlignment);
        }
        m_lastFrameTextRasterizationMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - textRasterizationStarted).count();
    }

    m_collectingFrameProfile = false;
    const double totalRenderMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - frameRenderStarted).count();
    m_lastFrameNonTextureRenderMilliseconds = std::max(
        0.0, totalRenderMilliseconds - m_lastFrameTextureLoadMilliseconds);
    m_lastFrameContentHash = contentHash;
    m_frameCacheValid = true;
}

void Window::renderVisualNovelFrame(
    bool hasBackground,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::vector<CharacterRenderData>& characters,
    const DialogueRenderData& dlg
) {
    std::vector<DialogueRenderData> dlgs;
    if (dlg.hasDialogueBox) {
        dlgs.push_back(dlg);
    }
    renderVisualNovelFrame(hasBackground, background, bgX, bgY, bgW, bgH, characters, dlgs);
}

void Window::renderVisualNovelFrame(
    bool hasBackground,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::vector<CharacterRenderData>& characters,
    bool hasDialogueBox,
    const std::string& speaker,
    const std::string& dialogue,
    float dlgX, float dlgY, float dlgW, float dlgH
) {
    DialogueRenderData dlg;
    dlg.hasDialogueBox = hasDialogueBox;
    dlg.speaker = speaker;
    dlg.dialogue = dialogue;
    dlg.x = dlgX;
    dlg.y = dlgY;
    dlg.width = dlgW;
    dlg.height = dlgH;
    dlg.typewriterEnabled = false; // Legacy direct call has typewriter disabled by default
    renderVisualNovelFrame(hasBackground, background, bgX, bgY, bgW, bgH, characters, dlg);
}

uint64_t Window::hashFrameContent(bool hasBackground, const std::string& background,
                              float bgX, float bgY, float bgW, float bgH,
                              const std::vector<CharacterRenderData>& characters,
                              const std::vector<DialogueRenderData>& dialogues,
                              const std::vector<ChoiceButtonRenderData>& choices,
                              float bgRotation, float bgParallaxX, float bgParallaxY,
                              float bgOpacity) const {
    uint64_t hash = kFnvOffsetBasis;
    fnvMixBool(hash, hasBackground);
    fnvMixString(hash, background);
    fnvMixF32(hash, bgX);
    fnvMixF32(hash, bgY);
    fnvMixF32(hash, bgW);
    fnvMixF32(hash, bgH);
    fnvMixF32(hash, bgRotation);
    fnvMixF32(hash, bgParallaxX);
    fnvMixF32(hash, bgParallaxY);
    fnvMixF32(hash, bgOpacity);
    fnvMixU64(hash, characters.size());
    for (const auto& ch : characters) {
        fnvMixString(hash, ch.sprite);
        fnvMixF32(hash, ch.x);
        fnvMixF32(hash, ch.y);
        fnvMixF32(hash, ch.width);
        fnvMixF32(hash, ch.height);
        fnvMixF32(hash, ch.rotation);
        fnvMixF32(hash, ch.scaleX);
        fnvMixF32(hash, ch.scaleY);
        fnvMixString(hash, ch.voiceBlipSound);
        fnvMixF32(hash, ch.voiceBlipPitch);
        fnvMixF32(hash, ch.voiceBlipPitchVariance);
        fnvMixI32(hash, ch.voiceBlipCadence);
    }
    fnvMixU64(hash, dialogues.size());
    for (const auto& dlg : dialogues) {
        fnvMixBool(hash, dlg.hasDialogueBox);
        fnvMixString(hash, dlg.speaker);
        fnvMixString(hash, dlg.dialogue);
        fnvMixF32(hash, dlg.x);
        fnvMixF32(hash, dlg.y);
        fnvMixF32(hash, dlg.width);
        fnvMixF32(hash, dlg.height);
        fnvMixF32(hash, dlg.scale);
        fnvMixBool(hash, dlg.typewriterEnabled);
        fnvMixBool(hash, dlg.isPlaying);
        fnvMixI32(hash, dlg.textSpeed);
        fnvMixF32(hash, dlg.elapsedTypewriterTime);
        fnvMixBool(hash, dlg.autoAdvance);
        fnvMixF32(hash, dlg.autoAdvanceDelay);
        fnvMixString(hash, dlg.typewriterSound);
        fnvMixF32(hash, dlg.voiceBlipPitch);
        fnvMixF32(hash, dlg.voiceBlipPitchVariance);
        fnvMixI32(hash, dlg.voiceBlipCadence);
        fnvMixBool(hash, dlg.voiceBlipSkipPunctuation);
        fnvMixI32(hash, dlg.voiceBlipChannel);
        fnvMixF32(hash, dlg.voiceBlipVolume);
        fnvMixU64(hash, dlg.lastBlipCodepointIndex);
        fnvMixF32(hash, dlg.fontSize);
        fnvMixF32(hash, dlg.speakerFontSize);
        fnvMixString(hash, dlg.textColor);
        fnvMixString(hash, dlg.speakerColor);
        fnvMixString(hash, dlg.textAlignment);
        fnvMixF32(hash, dlg.boxOpacity);
        fnvMixString(hash, dlg.boxColor);
        fnvMixString(hash, dlg.borderColor);
        fnvMixF32(hash, dlg.borderThickness);
        fnvMixF32(hash, dlg.cornerRadius);
        fnvMixString(hash, dlg.customBoxTexture);
    }
    fnvMixU64(hash, choices.size());
    for (const auto& choice : choices) {
        fnvMixString(hash, choice.optionId);
        fnvMixString(hash, choice.text);
        fnvMixString(hash, choice.backgroundImage);
        fnvMixF32(hash, choice.x);
        fnvMixF32(hash, choice.y);
        fnvMixF32(hash, choice.width);
        fnvMixF32(hash, choice.height);
        fnvMixF32(hash, choice.fontSize);
        fnvMixF32(hash, choice.opacity);
        fnvMixF32(hash, choice.borderThickness);
        fnvMixF32(hash, choice.cornerRadius);
        fnvMixString(hash, choice.textColor);
        fnvMixString(hash, choice.backgroundColor);
        fnvMixString(hash, choice.hoverColor);
        fnvMixString(hash, choice.borderColor);
        fnvMixString(hash, choice.textAlignment);
        fnvMixString(hash, choice.fontFamily);
        fnvMixBool(hash, choice.enabled);
    }
    // Dynamic render state that is not part of the packed frame.
    fnvMixF32(hash, m_camera ? m_camera->getShakeOffsetX() : 0.0f);
    fnvMixF32(hash, m_camera ? m_camera->getShakeOffsetY() : 0.0f);
    fnvMixBool(hash, m_transitionManager && m_transitionManager->isTransitionActive());
    fnvMixBool(hash, m_screenFx.flashActive);
    fnvMixF32(hash, m_screenFx.flashElapsed);
    fnvMixBool(hash, m_screenFx.hasTint);
    fnvMixBytes(hash, &m_screenFx.tintR, 3);
    fnvMixF32(hash, m_screenFx.tintOpacity);
    fnvMixBool(hash, m_screenFx.vignetteEnabled);
    fnvMixF32(hash, m_screenFx.vignetteIntensity);
    fnvMixF32(hash, m_screenFx.vignetteRadius);
    fnvMixBytes(hash, &m_screenFx.vignetteR, 3);
    fnvMixU64(hash, (static_cast<uint64_t>(m_width) << 32) | m_height);
    return hash;
}

void Window::renderComposedFrame(const ComposedFrame& frame) {
    renderVisualNovelFrame(
        frame.hasBackground,
        frame.background,
        frame.backgroundX, frame.backgroundY,
        frame.backgroundWidth, frame.backgroundHeight,
        frame.characters,
        frame.dialogues,
        frame.choices,
        frame.backgroundRotation,
        frame.backgroundParallaxX,
        frame.backgroundParallaxY,
        frame.backgroundOpacity
    );
}

void Window::endFrame() {
    if (!m_initialized || !m_sdlRenderer) return;

    if (!m_isOffscreen) {
        SDL_RenderPresent(m_sdlRenderer);
    }
}

void Window::shutdown() {
    if (!m_initialized) return;
    invalidateFrameCache();

    ROWL_LOG_INFO("Shutting down SDL3 Windowing & Graphics Subsystem...");

    clearTextureCache();
    shutdownGpuMsdfRenderer();

    if (m_vignetteTexture) {
        SDL_DestroyTexture(m_vignetteTexture);
        m_vignetteTexture = nullptr;
    }

    if (m_sdlRenderer) {
        SDL_DestroyRenderer(m_sdlRenderer);
        m_sdlRenderer = nullptr;
    }

    if (m_sdlWindow) {
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(m_eventWindowId);
        m_eventWindowId = 0;
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
    }
    if (m_offscreenSurface) {
        SDL_DestroySurface(m_offscreenSurface);
        m_offscreenSurface = nullptr;
    }

    if (m_videoLeaseHeld) {
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_VIDEO);
        m_videoLeaseHeld = false;
    }

    m_isOpen = false;
    m_initialized = false;
    ROWL_LOG_INFO("SDL3 Window Shutdown Complete.");
}

void Window::drawSprite(const std::string& filename,
                        float virtualX,
                        float virtualY,
                        float virtualWidth,
                        float virtualHeight,
                        float opacity) {
    if (!m_initialized || !m_sdlRenderer || filename.empty()) return;

    if (!m_isEmbedded) {
        int currentPhysW = 0, currentPhysH = 0;
        if (SDL_GetRenderOutputSize(m_sdlRenderer, &currentPhysW, &currentPhysH) && currentPhysW > 10 && currentPhysH > 10) {
            m_width = static_cast<uint32_t>(currentPhysW);
            m_height = static_cast<uint32_t>(currentPhysH);
        }
    }

    if (m_width < 10)  m_width  = 1920;
    if (m_height < 10) m_height = 1080;

    ViewportMetrics metrics = AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
    float camX = virtualX, camY = virtualY, camW = virtualWidth, camH = virtualHeight;
    if (m_camera) {
        m_camera->transformRect(virtualX, virtualY, virtualWidth, virtualHeight, camX, camY, camW, camH);
    }
    float physX = 0.0f, physY = 0.0f;
    AspectGuardian::virtualToPhysical(camX, camY, metrics, physX, physY);

    float scaledW = camW * metrics.scaleFactor;
    float scaledH = camH * metrics.scaleFactor;

    SDL_Texture* tex = loadTexture(filename);
    if (tex) {
        if (virtualWidth <= 0.0f || virtualHeight <= 0.0f) {
            float tw = 0.0f, th = 0.0f;
            SDL_GetTextureSize(tex, &tw, &th);
            if (virtualWidth <= 0.0f) scaledW = tw * metrics.scaleFactor;
            if (virtualHeight <= 0.0f) scaledH = th * metrics.scaleFactor;
        }

        SDL_FRect dstRect = { physX, physY, scaledW, scaledH };
        float alphaClamped = std::clamp(opacity, 0.0f, 1.0f);
        SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alphaClamped * 255.0f));
        SDL_RenderTexture(m_sdlRenderer, tex, nullptr, &dstRect);
    }
}

} // namespace Rowl::Render
