#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <climits>
#include <SDL3/SDL.h>
#include "rowl/text/text_shaper.hpp"

namespace Rowl::Render {

struct Glyph {
    int width = 0;
    int height = 0;
    int xoff = 0;
    int yoff = 0;
    int advance = 0;
    std::vector<uint8_t> bitmap; // 8-bit alpha mask
};

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    bool loadFont(const std::string& fontPath);
    bool loadFontFromMemory(const uint8_t* data, size_t size);
    bool isLoaded() const { return m_loaded; }

    /// Faz 3 Dilim 5 — accessibility display settings. Text scale
    /// multiplies every shaping/measurement/render font size (dialogue,
    /// typewriter and HUD text apply it with no call-site changes);
    /// high contrast adds a dark outline behind every glyph so author
    /// colors stay readable on any background.
    void setTextScale(float scale);
    float textScale() const { return m_textScale; }
    void setHighContrast(bool enabled);
    bool highContrast() const { return m_highContrast; }

    /// Measures the total pixel width of a single line of UTF-8 text at given font size.
    float measureTextWidth(const std::string& utf8Text, float fontSize);

    /// Wraps UTF-8 text to fit within maxWidth at given font size.
    std::vector<std::string> wrapText(const std::string& utf8Text, float fontSize, float maxWidth);

    /// Returns total number of UTF-8 codepoints in the string.
    static size_t countCodepoints(const std::string& utf8Text);

    /// Extracts the next UTF-8 codepoint from the string and advances byteIndex.
    static uint32_t getNextCodepoint(const std::string& str, size_t& byteIndex);

    /// Shapes markup once for consumers that need the exact render/reveal
    /// layout. The returned glyph vector is the measurement authority too.
    Rowl::Text::ShapedText shapeText(const std::string& markup, float fontSize,
                                     float maxWidth = 0.0f) const;
    std::shared_ptr<const Rowl::Text::ShapedText> shapeTextShared(
        const std::string& markup, float fontSize, float maxWidth = 0.0f) const;

    size_t countRevealUnits(const std::string& markup, float fontSize) const;

    void renderShapedText(SDL_Surface* targetSurface,
                          const Rowl::Text::ShapedText& shaped,
                          float x, float y, float fontSize, SDL_Color color,
                          float maxWidth = 0.0f, float maxHeight = 0.0f,
                          const std::string& alignment = "Left",
                          size_t maxVisibleRevealUnits = SIZE_MAX);

    /// Renders UTF-8 text directly to SDL_Surface with anti-aliasing, wrapping, alignment, and typewriter limit.
    void renderText(
        SDL_Surface* targetSurface,
        const std::string& utf8Text,
        float x, float y,
        float fontSize,
        SDL_Color color,
        float maxWidth = 0.0f,
        float maxHeight = 0.0f,
        const std::string& alignment = "Left",
        size_t maxVisibleRevealUnits = SIZE_MAX
    );

private:
    const Glyph* getGlyph(uint32_t codepoint, int pixelHeight);
    float effectiveFontSize(float fontSize) const { return fontSize * m_textScale; }

    std::vector<uint8_t> m_fontBuffer;
    void* m_fontInfo = nullptr; // stbtt_fontinfo pointer
    std::unordered_map<uint64_t, Glyph> m_glyphCache; // key = ((uint64_t)pixelHeight << 32) | codepoint
    std::unordered_map<uint64_t, Glyph> m_shapedGlyphCache;
    struct ShapeCacheEntry {
        std::string markup;
        float fontSize = 0.0f;
        float maxWidth = 0.0f;
        float textScale = 1.0f;
        std::shared_ptr<const Rowl::Text::ShapedText> layout;
    };
    mutable std::vector<ShapeCacheEntry> m_shapeCache;
    Rowl::Text::TextShaper m_textShaper;
    bool m_loaded = false;
    float m_textScale = 1.0f;
    bool m_highContrast = false;
};

} // namespace Rowl::Render
