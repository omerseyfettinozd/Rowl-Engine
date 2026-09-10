#pragma once

#include <string>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/msdf_renderer.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Surface;
struct SDL_GPUShader;
struct SDL_GPURenderState;

namespace Rowl::VFS {
class VFSManager;
}

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
    bool isPlaying = false;
    int textSpeed = 30; // ms per char
    float elapsedTypewriterTime = 0.0f; // seconds
    bool autoAdvance = false;
    float autoAdvanceDelay = 2.0f;

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

struct ChoiceButtonRenderData {
    std::string optionId;
    std::string text;
    std::string backgroundImage;
    float x = 680.0f;
    float y = 520.0f;
    float width = 560.0f;
    float height = 64.0f;
    float fontSize = 22.0f;
    float opacity = 1.0f;
    float borderThickness = 2.0f;
    float cornerRadius = 8.0f;
    std::string textColor = "#FFFFFF";
    std::string backgroundColor = "#1E293B";
    std::string hoverColor = "#0EA5E9";
    std::string borderColor = "#38BDF8";
    std::string textAlignment = "Center";
    std::string fontFamily = "Default";
    bool enabled = true;
};

/// Input remains owned by the runtime that created the window. Window never
/// reaches into a process-global Engine instance to handle a player action.
struct RuntimeInputEvent {
    enum class Type { Advance, QuickSave, QuickLoad, Rewind, PointerDown };
    Type type;
    float x = 0.0f;
    float y = 0.0f;
};

class Window {
public:
    explicit Window(Rowl::VFS::VFSManager* vfs = nullptr);
    ~Window();

    // Disable copy/move
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void setVfs(Rowl::VFS::VFSManager* vfs);
    Rowl::VFS::VFSManager* getVfs() const { return m_vfs; }

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

    void setInputHandler(std::function<void(const RuntimeInputEvent&)> handler);

    void pollEvents(bool& outShouldQuit);
    void beginFrame();

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        const std::vector<DialogueRenderData>& dialogues,
        const std::vector<ChoiceButtonRenderData>& choices = {}
    );

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

    SDL_Texture* loadTexture(const std::string& filename);
    void clearTextureCache();
    size_t getNegativeTextureCacheSize() const { return m_missingTextureCache.size(); }
    size_t getTextureCacheTextureCount() const { return m_textureMemoryBytes.size(); }
    uint64_t getTextureCacheBytes() const;
    uint64_t getTextureCacheBudgetBytes() const { return m_textureCacheBudgetBytes; }
    uint64_t getTextureCacheEvictionCount() const { return m_textureCacheEvictionCount; }
    double getLastFrameTextureLoadMilliseconds() const { return m_lastFrameTextureLoadMilliseconds; }
    double getLastFrameNonTextureRenderMilliseconds() const { return m_lastFrameNonTextureRenderMilliseconds; }
    void setTextureCacheBudgetBytes(uint64_t bytes);
    FontRenderer* getFontRenderer() const { return m_fontRenderer.get(); }
    void reloadFonts();

    /**
     * Renders a 2D sprite/texture at virtual canvas coordinates (default 1920x1080),
     * automatically projected to physical window coordinates via AspectGuardian.
     */
    void drawSprite(const std::string& filename,
                    float virtualX,
                    float virtualY,
                    float virtualWidth,
                    float virtualHeight,
                    float opacity = 1.0f);

    SDL_Renderer* getRenderer() const { return m_sdlRenderer; }
    bool isGpuMsdfAvailable() const { return m_msdfRenderState != nullptr; }

private:
    void initFontRenderer();
    void initGpuMsdfRenderer();
    void shutdownGpuMsdfRenderer();
    bool renderGpuMsdfText(const std::string&, float, float, float, SDL_Color);
    bool evictTexturesToFit(uint64_t incomingBytes);
    void destroyCachedTexture(SDL_Texture* texture);
    void touchTexture(SDL_Texture* texture);
    void rememberMissingTexture(std::string path);

    SDL_Window*   m_sdlWindow         = nullptr;
    SDL_Renderer* m_sdlRenderer       = nullptr;
    SDL_Surface*  m_offscreenSurface  = nullptr;
    std::unordered_map<std::string, SDL_Texture*> m_textureCache;
    std::unordered_map<SDL_Texture*, uint64_t> m_textureMemoryBytes;
    std::unordered_map<SDL_Texture*, uint64_t> m_textureLastUsed;
    std::unordered_set<std::string> m_missingTextureCache;
    // These assets exist but do not fit the active device budget. They are
    // retried when the host raises that budget, unlike genuinely missing files.
    std::unordered_set<std::string> m_budgetRejectedTextureCache;
    std::unique_ptr<FontRenderer> m_fontRenderer;
    SDL_GPUShader* m_msdfFragmentShader = nullptr;
    SDL_GPURenderState* m_msdfRenderState = nullptr;
    std::unique_ptr<MsdfRenderer> m_msdfRenderer;
    SDL_Texture* m_msdfAtlasTexture = nullptr;
    std::unordered_map<std::string, std::unique_ptr<FontRenderer>> m_buttonFontCache;

    uint32_t m_width       = 1920;
    uint32_t m_height      = 1080;
    uint64_t m_textureCacheBudgetBytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t m_textureUseClock = 0;
    uint64_t m_textureCacheEvictionCount = 0;
    double m_lastFrameTextureLoadMilliseconds = 0.0;
    double m_lastFrameNonTextureRenderMilliseconds = 0.0;
    bool m_collectingFrameProfile = false;
    bool m_isOpen          = false;
    bool m_initialized     = false;
    bool m_isEmbedded      = false; // true → rendering into host control
    bool m_isOffscreen     = false; // true → rendering to RGBA32 surface
    std::function<void(const RuntimeInputEvent&)> m_inputHandler;
    Rowl::VFS::VFSManager* m_vfs = nullptr;
    std::shared_ptr<Rowl::VFS::VFSManager> m_ownedVfs;
    bool m_videoLeaseHeld = false;
    Rowl::VFS::VFSManager& vfs() const;
};

} // namespace Rowl::Render
