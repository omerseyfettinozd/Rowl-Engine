#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>

namespace Rowl::Render {

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

    m_sdlRenderer = SDL_CreateRenderer(m_sdlWindow, nullptr);
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

    ROWL_LOG_INFO("SDL3 Window successfully created (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    return true;
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
    return true;
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
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    Rowl::Core::Engine::instance().advanceToNextNode();
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

SDL_Texture* Window::loadTexture(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;

    // Cache hit: return previously loaded texture (nullptr means "known-missing", skip)
    auto it = m_textureCache.find(filename);
    if (it != m_textureCache.end()) {
        return it->second; // may be nullptr if previously failed
    }

    // Cache miss: never cache nullptr — so we retry on next call if file appears later.
    namespace fs = std::filesystem;
    fs::path cwd = fs::current_path();
    std::vector<fs::path> searchPaths = {
        cwd / filename,
        cwd / "Assets" / "images" / filename,
        cwd / "Assets" / filename,
        cwd / ".." / "Assets" / "images" / filename,
        cwd / ".." / "Assets" / filename,
    };

    fs::path foundPath;
    for (const auto& p : searchPaths) {
        if (fs::exists(p) && fs::is_regular_file(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        // Do NOT cache nullptr — allow retry if file is added later
        return nullptr;
    }

    int width, height, channels;
    unsigned char* data = stbi_load(foundPath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        ROWL_LOG_ERROR("stbi_load failed for image: " + foundPath.string());
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

    // Only cache successful loads
    if (texture) {
        m_textureCache[filename] = texture;
        ROWL_LOG_INFO("✅ Loaded Hardware Texture: " + foundPath.string() + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    return texture;
}

void Window::renderVisualNovelFrame(
    const std::string& speaker,
    const std::string& dialogue,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::string& character,
    float charX, float charY, float charW, float charH,
    float dlgX, float dlgY, float dlgW, float dlgH
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

    // (Top atmosphere debug banner removed for clean gameplay presentation)

    // 2. Render Character Sprite / Portrait
    float scaledCharW = charW * metrics.scaleFactor;
    float scaledCharH = charH * metrics.scaleFactor;
    float physCharX, physCharY;
    AspectGuardian::virtualToPhysical(charX, charY, metrics, physCharX, physCharY);

    SDL_FRect charBox = { physCharX, physCharY, scaledCharW, scaledCharH };
    SDL_Texture* charTex = loadTexture(character);
    if (charTex) {
        SDL_RenderTexture(m_sdlRenderer, charTex, nullptr, &charBox);
    } else {
        SDL_SetRenderDrawColor(m_sdlRenderer, 30, 41, 59, 220);
        SDL_RenderFillRect(m_sdlRenderer, &charBox);
        SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
        SDL_RenderRect(m_sdlRenderer, &charBox);

        std::string charInfo = "[ CHAR: " + (character.empty() ? "spr_evelyn.png" : character) + " ]";
        SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
        SDL_RenderDebugText(m_sdlRenderer, physCharX + 20.0f * metrics.scaleFactor, physCharY + (scaledCharH / 2.0f), charInfo.c_str());
    }

    // 3. Render Dialogue Box
    float scaledDlgW = dlgW * metrics.scaleFactor;
    float scaledDlgH = dlgH * metrics.scaleFactor;
    float physBoxX, physBoxY;
    AspectGuardian::virtualToPhysical(dlgX, dlgY, metrics, physBoxX, physBoxY);

    SDL_FRect dlgBox = { physBoxX, physBoxY, scaledDlgW, scaledDlgH };
    SDL_SetRenderDrawColor(m_sdlRenderer, 15, 15, 26, 240);
    SDL_RenderFillRect(m_sdlRenderer, &dlgBox);

    SDL_SetRenderDrawColor(m_sdlRenderer, 0, 240, 255, 255);
    SDL_RenderRect(m_sdlRenderer, &dlgBox);

    // Speaker Name Tag Badge
    float tagW = std::clamp(160.0f * metrics.scaleFactor, 60.0f, scaledDlgW * 0.8f);
    float tagH = 32.0f * metrics.scaleFactor;
    SDL_FRect speakerTag = { physBoxX + (16.0f * metrics.scaleFactor), physBoxY - (16.0f * metrics.scaleFactor), tagW, tagH };
    SDL_SetRenderDrawColor(m_sdlRenderer, 37, 99, 235, 255);
    SDL_RenderFillRect(m_sdlRenderer, &speakerTag);

    std::string speakerText = speaker.empty() ? "Evelyn" : speaker;
    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
    SDL_RenderDebugText(m_sdlRenderer, physBoxX + (28.0f * metrics.scaleFactor), physBoxY - (8.0f * metrics.scaleFactor), speakerText.c_str());

    // Dialogue Content Text
    std::string dlgText = dialogue.empty() ? "Welcome to Rowl Engine!" : dialogue;
    SDL_SetRenderDrawColor(m_sdlRenderer, 241, 245, 249, 255);
    SDL_RenderDebugText(m_sdlRenderer, physBoxX + (24.0f * metrics.scaleFactor), physBoxY + (28.0f * metrics.scaleFactor), dlgText.c_str());
}

void Window::endFrame() {
    if (!m_initialized || !m_sdlRenderer) return;

    SDL_RenderPresent(m_sdlRenderer);
}

void Window::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down SDL3 Windowing & Graphics Subsystem...");

    for (auto& [name, tex] : m_textureCache) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    m_textureCache.clear();

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

} // namespace Rowl::Render