#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <string>
#include <fstream>

#ifndef ROWL_SHADER_DIR
#define ROWL_SHADER_DIR ""
#endif

namespace Rowl::Render {

namespace {

constexpr int kMaxTextureDimension = 8'192;
constexpr uint64_t kMaxTexturePixels = 16ULL * 1024 * 1024;
constexpr uint64_t kMinimumTextureCacheBytes = 1ULL * 1024ULL * 1024ULL;

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

Window::Window() = default;

Window::~Window() {
    if (m_initialized) {
        shutdown();
    }
}

bool Window::initializeOffscreen(uint32_t width, uint32_t height) {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing SDL3 Offscreen Surface & Software Renderer (" +
                  std::to_string(width) + "x" + std::to_string(height) + ")...");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }

    m_width = width;
    m_height = height;

    m_offscreenSurface = SDL_CreateSurface(static_cast<int>(width), static_cast<int>(height), SDL_PIXELFORMAT_RGBA32);
    if (!m_offscreenSurface) {
        ROWL_LOG_ERROR("SDL_CreateSurface (offscreen) failed: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }

    m_sdlRenderer = SDL_CreateSoftwareRenderer(m_offscreenSurface);
    if (!m_sdlRenderer) {
        ROWL_LOG_ERROR("SDL_CreateSoftwareRenderer failed: " + std::string(SDL_GetError()));
        SDL_DestroySurface(m_offscreenSurface);
        m_offscreenSurface = nullptr;
        SDL_Quit();
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

bool Window::initialize(const std::string& title, uint32_t width, uint32_t height, bool vsync) {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing SDL3 Windowing & Hardware Graphics Subsystem...");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }

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
        SDL_Quit();
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
        SDL_Quit();
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
    const auto metadata = Rowl::VFS::VFSManager::instance().readString("fonts/msdf/default.json");
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

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ROWL_LOG_ERROR("SDL_Init(SDL_INIT_VIDEO) failed: " + std::string(SDL_GetError()));
        return false;
    }

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
        SDL_Quit();
        return false;
    }

    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, nullptr);
    if (!m_sdlRenderer) {
        ROWL_LOG_ERROR("SDL_CreateRenderer (embedded) failed: " + std::string(SDL_GetError()));
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
        SDL_Quit();
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
        auto bytes = Rowl::VFS::VFSManager::instance().readBytes(vf);
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

void Window::pollEvents(bool& outShouldQuit) {
    if (!m_initialized) return;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
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
                    Rowl::Core::Engine::instance().advanceToNextNode();
                } else if (event.key.key == SDLK_F5) {
                    ROWL_LOG_INFO("[Player] F5 pressed: Quick Saving to Slot #0...");
                    Rowl::Core::Engine::instance().saveGameSlot(0);
                } else if (event.key.key == SDLK_F9) {
                    ROWL_LOG_INFO("[Player] F9 pressed: Quick Loading from Slot #0...");
                    Rowl::Core::Engine::instance().loadGameSlot(0);
                } else if (event.key.key == SDLK_BACKSPACE || event.key.key == SDLK_Z) {
                    ROWL_LOG_INFO("[Player] Rewind key pressed: Rewinding 1 step...");
                    Rowl::Core::Engine::instance().rewind(1);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (!Rowl::Core::Engine::instance().handlePointerDown(event.button.x, event.button.y)) {
                        Rowl::Core::Engine::instance().advanceToNextNode();
                    }
                }
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
    m_buttonFontCache.clear();
    ROWL_LOG_INFO("Hardware Texture Cache Cleared (" + std::to_string(uniqueTextures.size()) + " unique textures freed).");
}

void Window::setTextureCacheBudgetBytes(uint64_t bytes) {
    m_textureCacheBudgetBytes = std::max(bytes, kMinimumTextureCacheBytes);
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
    }
    return true;
}

SDL_Texture* Window::loadTexture(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;

    // Normalize slashes
    std::string normPath = filename;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    if (m_missingTextureCache.contains(normPath)) return nullptr;

    // Cache hit: only return valid textures
    auto it = m_textureCache.find(normPath);
    if (it != m_textureCache.end() && it->second != nullptr) {
        touchTexture(it->second);
        return it->second;
    }

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
        auto bytes = Rowl::VFS::VFSManager::instance().readBytes(candidate);
        if (!bytes.empty()) {
            data = loadTextureMemorySafely(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels);
            if (data) {
                sourceInfo = "VFS [" + candidate + "]";
                break;
            }
        }
    }

    if (!data) {
        m_missingTextureCache.insert(normPath);
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_RGBA32, data, width * 4
    );

    if (!surface) {
        stbi_image_free(data);
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
            ROWL_LOG_WARN("Texture exceeds the configured cache budget: " + filename);
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
    return texture;
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

void Window::renderVisualNovelFrame(
    bool hasBackground,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::vector<CharacterRenderData>& characters,
    const std::vector<DialogueRenderData>& dialogues,
    const std::vector<ChoiceButtonRenderData>& choices
) {
    if (!m_initialized || !m_sdlRenderer) return;

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

    // 1. Render Background Texture or Fill into Virtual Viewport
    if (hasBackground && !background.empty()) {
        float physBgX, physBgY;
        AspectGuardian::virtualToPhysical(bgX, bgY, metrics, physBgX, physBgY);
        float scaledBgW = bgW * metrics.scaleFactor;
        float scaledBgH = bgH * metrics.scaleFactor;
        SDL_FRect vpRect = { physBgX, physBgY, scaledBgW, scaledBgH };

        SDL_Texture* bgTex = loadTexture(background);
        if (bgTex) {
            SDL_RenderTexture(m_sdlRenderer, bgTex, nullptr, &vpRect);
        } else {
            SDL_SetRenderDrawColor(m_sdlRenderer, 20, 24, 38, 255);
            SDL_RenderFillRect(m_sdlRenderer, &vpRect);
        }
    }

    // 2. Render Character Sprites / Portraits (Multi-Character Support with Proportional Uniform Fit)
    for (const auto& ch : characters) {
        if (ch.sprite.empty()) continue;

        float scaledCharW = ch.width * metrics.scaleFactor;
        float scaledCharH = ch.height * metrics.scaleFactor;
        float physCharX, physCharY;
        AspectGuardian::virtualToPhysical(ch.x, ch.y, metrics, physCharX, physCharY);

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
                SDL_RenderTexture(m_sdlRenderer, charTex, nullptr, &dstRect);
            } else {
                SDL_FRect charBox = { physCharX, physCharY, scaledCharW, scaledCharH };
                SDL_RenderTexture(m_sdlRenderer, charTex, nullptr, &charBox);
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

    // In offscreen mode, flush SDL graphics pipeline to m_offscreenSurface BEFORE drawing direct TrueType text
    if (m_isOffscreen && m_sdlRenderer) {
        SDL_RenderPresent(m_sdlRenderer);
    }

    // 4. Render High-Quality Anti-Aliased TrueType Text directly onto Offscreen Surface
    if (m_fontRenderer && m_fontRenderer->isLoaded() && m_offscreenSurface) {
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
                    auto bytes = Rowl::VFS::VFSManager::instance().readBytes(choice.fontFamily);
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
    }
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

void Window::endFrame() {
    if (!m_initialized || !m_sdlRenderer) return;

    if (!m_isOffscreen) {
        SDL_RenderPresent(m_sdlRenderer);
    }
}

void Window::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down SDL3 Windowing & Graphics Subsystem...");

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

    shutdownGpuMsdfRenderer();

    if (m_sdlRenderer) {
        SDL_DestroyRenderer(m_sdlRenderer);
        m_sdlRenderer = nullptr;
    }

    if (m_sdlWindow) {
        SDL_DestroyWindow(m_sdlWindow);
        m_sdlWindow = nullptr;
    }

    if (m_offscreenSurface) {
        SDL_DestroySurface(m_offscreenSurface);
        m_offscreenSurface = nullptr;
    }

    SDL_Quit();

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
    float physX = 0.0f, physY = 0.0f;
    AspectGuardian::virtualToPhysical(virtualX, virtualY, metrics, physX, physY);

    float scaledW = virtualWidth * metrics.scaleFactor;
    float scaledH = virtualHeight * metrics.scaleFactor;

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
