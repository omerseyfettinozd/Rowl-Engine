#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;

namespace Rowl::Render {

struct CharacterRenderData {
    std::string sprite;
    float x = 1440.0f;
    float y = 340.0f;
    float width = 360.0f;
    float height = 540.0f;
};

struct DialogueRenderData {
    bool hasDialogueBox = true;
    std::string speaker;
    std::string dialogue;
    float x = 80.0f;
    float y = 860.0f;
    float width = 1760.0f;
    float height = 180.0f;
    float scale = 1.0f;

    // Typewriter & Timing
    bool typewriterEnabled = false;
    int textSpeed = 30; // ms per char
    float elapsedTypewriterTime = 0.0f; // seconds

    // Typography & Colors
    float fontSize = 24.0f;
    float speakerFontSize = 20.0f;
    std::string textColor = "#F1F5F9";
    std::string speakerColor = "#38BDF8";
    std::string textAlignment = "Left";

    // Box Visuals & Opacity
    float boxOpacity = 0.88f;
    std::string boxColor = "#0F0F1A";
    std::string borderColor = "#00F0FF";
    float borderThickness = 2.0f;
    float cornerRadius = 8.0f;
    std::string customBoxTexture;
};

class Window {
public:
    Window();
    ~Window();

    // Disable copy/move
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    /**
     * Offscreen initialization: renders into an internal RGBA32 surface/buffer (e.g. 1920x1080)
     * instead of a native OS window.
     */
    bool initializeOffscreen(uint32_t width, uint32_t height);

    /**
     * Provides direct access to the RGBA32 pixel memory pointer for zero-copy/fast host sharing.
     */
    const uint8_t* getPixelBuffer() const;

    /**
     * Standard initialization: creates an SDL3 top-level window.
     * Used in standalone / runtime-only mode.
     */
    bool initialize(const std::string& title,
                    uint32_t width,
                    uint32_t height,
                    bool vsync = true);

    /**
     * Embedded initialization: renders into an existing native OS handle.
     */
    bool initializeEmbedded(void* nativeHandle,
                             uint32_t width,
                             uint32_t height,
                             bool vsync = true);

    /**
     * Notify the window of a viewport resize (e.g. host control resized).
     */
    void resizeViewport(uint32_t newWidth, uint32_t newHeight);

    void pollEvents(bool& outShouldQuit);
    void beginFrame();

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        const DialogueRenderData& dialogueData
    );

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        bool hasDialogueBox,
        const std::string& speaker,
        const std::string& dialogue,
        float dlgX,  float dlgY,  float dlgW,  float dlgH
    );
    void endFrame();
    void shutdown();

    bool isOpen()          const { return m_isOpen; }
    uint32_t getWidth()    const { return m_width; }
    uint32_t getHeight()   const { return m_height; }
    bool isEmbedded()      const { return m_isEmbedded; }
    bool isOffscreen()     const { return m_isOffscreen; }

    SDL_Window*   getNativeWindow()   const { return m_sdlWindow; }
    SDL_Renderer* getNativeRenderer() const { return m_sdlRenderer; }

    SDL_Texture* loadTexture(const std::string& filename);
    void clearTextureCache();

private:
    struct TextWrapCache {
        std::string dialogue;
        float boxWidth = 0.0f;
        float scaleFactor = 0.0f;
        std::vector<std::string> wrappedLines;
    };

    SDL_Window*   m_sdlWindow         = nullptr;
    SDL_Renderer* m_sdlRenderer       = nullptr;
    SDL_Surface*  m_offscreenSurface  = nullptr;
    std::unordered_map<std::string, SDL_Texture*> m_textureCache;
    TextWrapCache m_textWrapCache;

    uint32_t m_width       = 1920;
    uint32_t m_height      = 1080;
    bool m_isOpen          = false;
    bool m_initialized     = false;
    bool m_isEmbedded      = false; // true → rendering into host control
    bool m_isOffscreen     = false; // true → rendering to RGBA32 surface
};

} // namespace Rowl::Render
