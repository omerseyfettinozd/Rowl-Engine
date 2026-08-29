#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <climits>
#include <SDL3/SDL.h>

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

    /// Measures the total pixel width of a single line of UTF-8 text at given font size.
    float measureTextWidth(const std::string& utf8Text, float fontSize);

    /// Wraps UTF-8 text to fit within maxWidth at given font size.
    std::vector<std::string> wrapText(const std::string& utf8Text, float fontSize, float maxWidth);

    /// Returns total number of UTF-8 codepoints in the string.
    static size_t countCodepoints(const std::string& utf8Text);

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
        size_t maxVisibleCodepoints = SIZE_MAX
    );

private:
    static uint32_t getNextCodepoint(const std::string& str, size_t& byteIndex);
    const Glyph* getGlyph(uint32_t codepoint, int pixelHeight);

    std::vector<uint8_t> m_fontBuffer;
    void* m_fontInfo = nullptr; // stbtt_fontinfo pointer
    std::unordered_map<uint64_t, Glyph> m_glyphCache; // key = ((uint64_t)pixelHeight << 32) | codepoint
    bool m_loaded = false;
};

} // namespace Rowl::Render
