#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <vector>

namespace Rowl::Render {

Window::Window() = default;

Window::~Window() {
    if (m_initialized) {
        shutdown();
    }
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
                m_width = static_cast<uint32_t>(event.window.data1);
                m_height = static_cast<uint32_t>(event.window.data2);
                ROWL_LOG_TRACE("Window resized to: " + std::to_string(m_width) + "x" + std::to_string(m_height));
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

    auto it = m_textureCache.find(filename);
    if (it != m_textureCache.end()) {
        return it->second;
    }

    std::vector<std::string> searchPaths = {
        filename,
        "data/" + filename,
        "../data/" + filename,
        "/home/chaple/Belgeler/Rowl Engine/data/" + filename
    };

    std::string foundPath;
    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        m_textureCache[filename] = nullptr;
        return nullptr;
    }

    int width, height, channels;
    unsigned char* data = stbi_load(foundPath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        ROWL_LOG_ERROR("stbi_load failed for image: " + foundPath);
        m_textureCache[filename] = nullptr;
        return nullptr;
    }

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        width, height, SDL_PIXELFORMAT_RGBA32, data, width * 4
    );

    if (!surface) {
        stbi_image_free(data);
        m_textureCache[filename] = nullptr;
        return nullptr;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
    SDL_DestroySurface(surface);
    stbi_image_free(data);

    m_textureCache[filename] = texture;
    if (texture) {
        ROWL_LOG_INFO("✅ Loaded Hardware Texture: " + foundPath + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    }
    return texture;
}

void Window::renderVisualNovelFrame(const std::string& speaker, const std::string& dialogue, const std::string& background, const std::string& character, float characterX, float characterY, float dialogueBoxY) {
    if (!m_initialized || !m_sdlRenderer) return;

    // Calculate Aspect Guardian resolution metrics (1920x1080 virtual canvas)
    ViewportMetrics metrics = AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);

    // Clear physical screen to letterbox black (#0B0F19)
    SDL_SetRenderDrawColor(m_sdlRenderer, 11, 15, 25, 255);
    SDL_RenderClear(m_sdlRenderer);

    // 1. Render Background Texture or Fill into 16:9 Virtual Viewport
    SDL_FRect vpRect = { static_cast<float>(metrics.x), static_cast<float>(metrics.y), static_cast<float>(metrics.width), static_cast<float>(metrics.height) };
    SDL_Texture* bgTex = loadTexture(background);
    if (bgTex) {
        SDL_RenderTexture(m_sdlRenderer, bgTex, nullptr, &vpRect);
    } else {
        SDL_SetRenderDrawColor(m_sdlRenderer, 20, 24, 38, 255);
        SDL_RenderFillRect(m_sdlRenderer, &vpRect);
    }

    // Top atmosphere banner (virtual Y: 0..80)
    float bannerX, bannerY;
    AspectGuardian::virtualToPhysical(0.0f, 0.0f, metrics, bannerX, bannerY);
    SDL_FRect topBanner = { bannerX, bannerY, static_cast<float>(metrics.width), 80.0f * metrics.scaleFactor };
    SDL_SetRenderDrawColor(m_sdlRenderer, 15, 23, 42, 220);
    SDL_RenderFillRect(m_sdlRenderer, &topBanner);

    SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
    SDL_RenderDebugText(m_sdlRenderer, bannerX + 20.0f * metrics.scaleFactor, bannerY + 20.0f * metrics.scaleFactor, "ROWL ENGINE - VISUAL NOVEL RUNTIME (1920x1080 Aspect Guardian Active)");

    std::string bgInfo = "Active Background: " + (background.empty() ? "bg_beach_sunset.png" : background);
    SDL_SetRenderDrawColor(m_sdlRenderer, 148, 163, 184, 255);
    SDL_RenderDebugText(m_sdlRenderer, bannerX + 20.0f * metrics.scaleFactor, bannerY + 45.0f * metrics.scaleFactor, bgInfo.c_str());

    // 2. Render Character Sprite / Portrait (Virtual Position: characterX, characterY, W=360, H=540)
    float charW = 360.0f * metrics.scaleFactor;
    float charH = 540.0f * metrics.scaleFactor;
    float physCharX, physCharY;
    AspectGuardian::virtualToPhysical(characterX, characterY, metrics, physCharX, physCharY);

    SDL_FRect charBox = { physCharX, physCharY, charW, charH };
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
        SDL_RenderDebugText(m_sdlRenderer, physCharX + 30.0f * metrics.scaleFactor, physCharY + 250.0f * metrics.scaleFactor, charInfo.c_str());
    }

    // 3. Dialogue Box (Virtual Position: X=80, Y=dialogueBoxY, W=1760, H=180)
    float boxW = 1760.0f * metrics.scaleFactor;
    float boxH = 180.0f * metrics.scaleFactor;
    float physBoxX, physBoxY;
    AspectGuardian::virtualToPhysical(80.0f, dialogueBoxY, metrics, physBoxX, physBoxY);

    SDL_FRect dlgBox = { physBoxX, physBoxY, boxW, boxH };
    SDL_SetRenderDrawColor(m_sdlRenderer, 15, 15, 26, 240);
    SDL_RenderFillRect(m_sdlRenderer, &dlgBox);

    SDL_SetRenderDrawColor(m_sdlRenderer, 0, 240, 255, 255);
    SDL_RenderRect(m_sdlRenderer, &dlgBox);

    // Speaker Name Tag Badge
    float tagW = 200.0f * metrics.scaleFactor;
    float tagH = 36.0f * metrics.scaleFactor;
    SDL_FRect speakerTag = { physBoxX + (24.0f * metrics.scaleFactor), physBoxY - (18.0f * metrics.scaleFactor), tagW, tagH };
    SDL_SetRenderDrawColor(m_sdlRenderer, 37, 99, 235, 255);
    SDL_RenderFillRect(m_sdlRenderer, &speakerTag);

    std::string speakerText = speaker.empty() ? "Evelyn" : speaker;
    SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
    SDL_RenderDebugText(m_sdlRenderer, physBoxX + (40.0f * metrics.scaleFactor), physBoxY - (8.0f * metrics.scaleFactor), speakerText.c_str());

    // Dialogue Content Text
    std::string dlgText = dialogue.empty() ? "Welcome to Rowl Engine!" : dialogue;
    SDL_SetRenderDrawColor(m_sdlRenderer, 241, 245, 249, 255);
    SDL_RenderDebugText(m_sdlRenderer, physBoxX + (28.0f * metrics.scaleFactor), physBoxY + (40.0f * metrics.scaleFactor), dlgText.c_str());
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

    SDL_Quit();

    m_isOpen = false;
    m_initialized = false;
    ROWL_LOG_INFO("SDL3 Window Shutdown Complete.");
}

} // namespace Rowl::Render