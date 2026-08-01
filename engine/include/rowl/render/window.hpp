#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <unordered_map>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Rowl::Render {

class Window {
public:
    Window();
    ~Window();

    // Disable copy/move
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool initialize(const std::string& title, uint32_t width, uint32_t height, bool vsync = true);
    void pollEvents(bool& outShouldQuit);
    void beginFrame();
    void renderVisualNovelFrame(
        const std::string& speaker,
        const std::string& dialogue,
        const std::string& background,
        float bgX, float bgY, float bgW, float bgH,
        const std::string& character,
        float charX, float charY, float charW, float charH,
        float dlgX, float dlgY, float dlgW, float dlgH
    );
    void endFrame();
    void shutdown();

    bool isOpen() const { return m_isOpen; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }

    SDL_Window* getNativeWindow() const { return m_sdlWindow; }
    SDL_Renderer* getNativeRenderer() const { return m_sdlRenderer; }

    SDL_Texture* loadTexture(const std::string& filename);

private:
    SDL_Window* m_sdlWindow = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
    std::unordered_map<std::string, SDL_Texture*> m_textureCache;
    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    bool m_isOpen = false;
    bool m_initialized = false;
};

} // namespace Rowl::Render
