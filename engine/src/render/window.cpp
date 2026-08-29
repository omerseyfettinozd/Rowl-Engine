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
    initFontRenderer();
    return true;
}

void Window::initFontRenderer() {
    if (!m_fontRenderer) {
        m_fontRenderer = std::make_unique<FontRenderer>();
    }

    const std::vector<std::string> fontCandidates = {
        "Assets/fonts/default.ttf",
        "Assets/fonts/default_bold.ttf",
        "fonts/default.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
    };

    for (const auto& path : fontCandidates) {
        if (std::filesystem::exists(path)) {
            if (m_fontRenderer->loadFont(path)) {
                ROWL_LOG_INFO("✅ Loaded Visual Novel TTF Font: " + path);
                break;
            }
        }
    }
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

void Window::clearTextureCache() {
    for (auto& [name, tex] : m_textureCache) {
        if (tex) {
            SDL_DestroyTexture(tex);
        }
    }
    m_textureCache.clear();
    ROWL_LOG_INFO("Hardware Texture Cache Cleared.");
}

SDL_Texture* Window::loadTexture(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;

    namespace fs = std::filesystem;

    // Normalize slashes
    std::string normPath = filename;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    // Cache hit: only return valid textures
    auto it = m_textureCache.find(normPath);
    if (it != m_textureCache.end() && it->second != nullptr) {
        return it->second;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* data = nullptr;
    std::string sourceInfo;

    std::string bareName = fs::path(normPath).filename().string();

    // 1. Direct absolute or relative filesystem check
    if (fs::exists(normPath) && fs::is_regular_file(normPath)) {
        data = stbi_load(normPath.c_str(), &width, &height, &channels, 4);
        if (data) {
            sourceInfo = "Direct Path [" + normPath + "]";
        }
    }

    // 2. Try VFS Manager candidates
    if (!data) {
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
                data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
                if (data) {
                    sourceInfo = "VFS [" + candidate + "]";
                    break;
                }
            }
        }
    }

    // 3. Search all active VFS physical mount directories directly on disk
    if (!data) {
        const auto& mountPoints = Rowl::VFS::VFSManager::instance().getMountPoints();
        for (const auto& [prefix, source] : mountPoints) {
            if (auto loose = std::dynamic_pointer_cast<Rowl::VFS::LooseDirectorySource>(source)) {
                fs::path baseDir(loose->getPhysicalPath());
                std::vector<fs::path> diskCandidates = {
                    baseDir / normPath,
                    baseDir / bareName,
                    baseDir / "images" / bareName,
                    baseDir / "Assets" / "images" / bareName
                };
                for (const auto& dp : diskCandidates) {
                    if (fs::exists(dp) && fs::is_regular_file(dp)) {
                        data = stbi_load(dp.string().c_str(), &width, &height, &channels, 4);
                        if (data) {
                            sourceInfo = "VFS Mount Disk [" + dp.string() + "]";
                            break;
                        }
                    }
                }
                if (data) break;
            }
        }
    }

    // 4. Fallback search relative to CWD
    if (!data) {
        fs::path cwd = fs::current_path();
        std::vector<fs::path> searchPaths = {
            cwd / normPath,
            cwd / bareName,
            cwd / "Assets" / "images" / bareName,
            cwd / "Assets" / "images" / normPath,
            cwd / "Assets" / bareName,
            cwd / ".." / "Assets" / "images" / bareName,
            cwd / ".." / "Assets" / bareName
        };

        for (const auto& p : searchPaths) {
            if (fs::exists(p) && fs::is_regular_file(p)) {
                data = stbi_load(p.string().c_str(), &width, &height, &channels, 4);
                if (data) {
                    sourceInfo = p.string();
                    break;
                }
            }
        }
    }

    if (!data) {
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
        m_textureCache[filename] = texture;
        m_textureCache[normPath] = texture;
        m_textureCache[bareName] = texture;
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
    const DialogueRenderData& dlg
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

    // 3. Render Dialogue Box (Only if enabled / present)
    if (dlg.hasDialogueBox) {
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
            if (m_fontRenderer && m_fontRenderer->isLoaded() && m_offscreenSurface) {
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
            } else {
                SDL_SetRenderDrawColor(m_sdlRenderer, 255, 255, 255, 255);
                SDL_RenderDebugText(m_sdlRenderer, tagX + (16.0f * metrics.scaleFactor), tagY + (tagH - 8.0f) / 2.0f, dlg.speaker.c_str());
            }
        }

        // Dialogue Content Text (with TrueType Scalable Font + Typewriter Progression + Text Alignment)
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

            if (m_fontRenderer && m_fontRenderer->isLoaded() && m_offscreenSurface) {
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
            } else {
                SDL_SetRenderDrawColor(m_sdlRenderer, textColor.r, textColor.g, textColor.b, textColor.a);
                SDL_RenderDebugText(m_sdlRenderer, physBoxX + paddingLeft, physBoxY + paddingTop, dlg.dialogue.c_str());
            }
        }
    }
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