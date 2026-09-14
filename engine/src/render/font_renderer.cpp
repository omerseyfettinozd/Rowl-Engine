#include "rowl/render/font_renderer.hpp"
#include "rowl/core/logger.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>

#define STB_TRUETYPE_IMPLEMENTATION
#include "thirdparty/stb_truetype.h"

namespace Rowl::Render {

FontRenderer::FontRenderer() {
    m_fontInfo = new stbtt_fontinfo();
}

FontRenderer::~FontRenderer() {
    if (m_fontInfo) {
        delete static_cast<stbtt_fontinfo*>(m_fontInfo);
        m_fontInfo = nullptr;
    }
}

bool FontRenderer::loadFont(const std::string& fontPath) {
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ROWL_LOG_WARN("Could not open font file: " + fontPath);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    m_fontBuffer.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(m_fontBuffer.data()), size)) {
        ROWL_LOG_ERROR("Failed to read font file data: " + fontPath);
        return false;
    }

    return loadFontFromMemory(m_fontBuffer.data(), m_fontBuffer.size());
}

bool FontRenderer::loadFontFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    if (m_fontBuffer.empty() || m_fontBuffer.data() != data) {
        m_fontBuffer.assign(data, data + size);
    }

    auto* info = static_cast<stbtt_fontinfo*>(m_fontInfo);
    if (!stbtt_InitFont(info, m_fontBuffer.data(), 0)) {
        ROWL_LOG_ERROR("stbtt_InitFont failed to parse font buffer!");
        m_loaded = false;
        return false;
    }

    m_glyphCache.clear();
    m_shapedGlyphCache.clear();
    m_shapeCache.clear();
    const bool advanced = m_textShaper.loadFontFromMemory(
        m_fontBuffer.data(), m_fontBuffer.size());
    m_loaded = true;
    ROWL_LOG_INFO(std::string("✅ TrueType Font Loaded Successfully (") +
        (advanced ? "HarfBuzz/FriBidi" : "stb fallback") + ").");
    return true;
}

uint32_t FontRenderer::getNextCodepoint(const std::string& str, size_t& byteIndex) {
    if (byteIndex >= str.length()) return 0;

    unsigned char c0 = static_cast<unsigned char>(str[byteIndex++]);
    if (c0 < 0x80) {
        return c0;
    } else if ((c0 & 0xE0) == 0xC0) {
        if (byteIndex >= str.length()) return c0;
        unsigned char c1 = static_cast<unsigned char>(str[byteIndex++]);
        return ((c0 & 0x1F) << 6) | (c1 & 0x3F);
    } else if ((c0 & 0xF0) == 0xE0) {
        if (byteIndex + 1 >= str.length()) { byteIndex = str.length(); return c0; }
        unsigned char c1 = static_cast<unsigned char>(str[byteIndex++]);
        unsigned char c2 = static_cast<unsigned char>(str[byteIndex++]);
        return ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
    } else if ((c0 & 0xF8) == 0xF0) {
        if (byteIndex + 2 >= str.length()) { byteIndex = str.length(); return c0; }
        unsigned char c1 = static_cast<unsigned char>(str[byteIndex++]);
        unsigned char c2 = static_cast<unsigned char>(str[byteIndex++]);
        unsigned char c3 = static_cast<unsigned char>(str[byteIndex++]);
        return ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    }
    return c0;
}

size_t FontRenderer::countCodepoints(const std::string& utf8Text) {
    size_t count = 0;
    size_t i = 0;
    while (i < utf8Text.length()) {
        getNextCodepoint(utf8Text, i);
        count++;
    }
    return count;
}

const Glyph* FontRenderer::getGlyph(uint32_t codepoint, int pixelHeight) {
    if (!m_loaded) return nullptr;

    uint64_t key = (static_cast<uint64_t>(pixelHeight) << 32) | static_cast<uint64_t>(codepoint);
    auto it = m_glyphCache.find(key);
    if (it != m_glyphCache.end()) {
        return &it->second;
    }

    auto* info = static_cast<stbtt_fontinfo*>(m_fontInfo);
    float scale = stbtt_ScaleForPixelHeight(info, static_cast<float>(pixelHeight));

    int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(codepoint));
    if (glyphIndex == 0 && codepoint != ' ') {
        // Fallback: try '?' or default glyph
        glyphIndex = stbtt_FindGlyphIndex(info, '?');
    }

    int advanceWidth = 0, leftSideBearing = 0;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advanceWidth, &leftSideBearing);

    Glyph glyph;
    glyph.advance = static_cast<int>(std::round(advanceWidth * scale));

    if (codepoint == ' ' || codepoint == '\t' || codepoint == '\n' || codepoint == '\r') {
        glyph.width = 0;
        glyph.height = 0;
        glyph.xoff = 0;
        glyph.yoff = 0;
        m_glyphCache[key] = glyph;
        return &m_glyphCache[key];
    }

    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* bitmap = stbtt_GetGlyphBitmap(info, scale, scale, glyphIndex, &w, &h, &xoff, &yoff);
    if (bitmap) {
        glyph.width = w;
        glyph.height = h;
        glyph.xoff = xoff;
        glyph.yoff = yoff;
        glyph.bitmap.assign(bitmap, bitmap + (w * h));
        stbtt_FreeBitmap(bitmap, nullptr);
    }

    m_glyphCache[key] = glyph;
    return &m_glyphCache[key];
}

float FontRenderer::measureTextWidth(const std::string& utf8Text, float fontSize) {
    if (!m_loaded || utf8Text.empty()) return 0.0f;

    if (m_textShaper.isAdvancedBackendActive()) {
        return shapeTextShared(utf8Text, fontSize)->width;
    }

    const std::string plainText = Rowl::Text::stripMarkup(utf8Text);
    int pixelHeight = static_cast<int>(std::round(fontSize));
    if (pixelHeight < 8) pixelHeight = 8;

    float totalWidth = 0.0f;
    size_t i = 0;
    while (i < plainText.length()) {
        uint32_t cp = getNextCodepoint(plainText, i);
        const Glyph* g = getGlyph(cp, pixelHeight);
        if (g) {
            totalWidth += static_cast<float>(g->advance);
        }
    }
    return totalWidth;
}

Rowl::Text::ShapedText FontRenderer::shapeText(const std::string& markup,
                                                float fontSize,
                                                float maxWidth) const {
    return *shapeTextShared(markup, fontSize, maxWidth);
}

std::shared_ptr<const Rowl::Text::ShapedText> FontRenderer::shapeTextShared(
    const std::string& markup, float fontSize, float maxWidth) const {
    for (const auto& cached : m_shapeCache) {
        if (cached.markup == markup && cached.fontSize == fontSize &&
            cached.maxWidth == maxWidth) return cached.layout;
    }
    Rowl::Text::ShapeOptions options;
    options.fontSize = fontSize;
    options.maxWidth = maxWidth;
    auto layout = std::make_shared<Rowl::Text::ShapedText>(
        m_textShaper.shapeMarkup(markup, options));
    if (m_shapeCache.size() >= 16) m_shapeCache.erase(m_shapeCache.begin());
    m_shapeCache.push_back({markup, fontSize, maxWidth, layout});
    return layout;
}

size_t FontRenderer::countRevealUnits(const std::string& markup,
                                      float fontSize) const {
    return shapeText(markup, fontSize).revealUnits.size();
}

void FontRenderer::renderShapedText(
    SDL_Surface* targetSurface, const Rowl::Text::ShapedText& shaped,
    float startX, float startY, float fontSize, SDL_Color color,
    float maxWidth, float maxHeight, const std::string& alignment,
    size_t maxVisibleRevealUnits) {
    if (!targetSurface || maxVisibleRevealUnits == 0) return;
    bool mustUnlock = false;
    if (SDL_MUSTLOCK(targetSurface)) {
        if (!SDL_LockSurface(targetSurface)) return;
        mustUnlock = true;
    }
    for (const auto& line : shaped.lines) {
        if (maxHeight > 0.0f && line.baseline > maxHeight) break;
        float lineX = startX;
        if (alignment == "Center" && maxWidth > 0.0f)
            lineX += (maxWidth - line.width) * 0.5f;
        else if (alignment == "Right" && maxWidth > 0.0f)
            lineX += maxWidth - line.width;
        for (uint32_t index = line.firstGlyph;
             index < line.firstGlyph + line.glyphCount; ++index) {
            const auto& shapedGlyph = shaped.glyphs[index];
            if (shapedGlyph.revealIndex >= maxVisibleRevealUnits) continue;
            const float glyphSize = shapedGlyph.style.hasSize
                ? shapedGlyph.style.size : fontSize;
            const int pixelHeight = std::max(1, static_cast<int>(std::round(glyphSize)));
            const uint64_t key = (static_cast<uint64_t>(pixelHeight) << 32) |
                                 shapedGlyph.glyphIndex;
            auto found = m_shapedGlyphCache.find(key);
            if (found == m_shapedGlyphCache.end()) {
                Rowl::Text::RasterizedShapedGlyph bitmap;
                if (!m_textShaper.rasterizeGlyph(shapedGlyph.glyphIndex,
                                                 glyphSize, bitmap)) continue;
                Glyph cached;
                cached.width = bitmap.width;
                cached.height = bitmap.height;
                cached.xoff = bitmap.bearingX;
                cached.yoff = -bitmap.bearingY;
                cached.bitmap = std::move(bitmap.bitmap);
                found = m_shapedGlyphCache.emplace(key, std::move(cached)).first;
            }
            const Glyph& glyph = found->second;
            const SDL_Color glyphColor = shapedGlyph.style.hasColor
                ? SDL_Color{shapedGlyph.style.color.r, shapedGlyph.style.color.g,
                            shapedGlyph.style.color.b, shapedGlyph.style.color.a}
                : color;
            const int drawX = static_cast<int>(std::round(
                lineX + shapedGlyph.x + shapedGlyph.xOffset + glyph.xoff));
            const int drawY = static_cast<int>(std::round(
                startY + shapedGlyph.y - shapedGlyph.yOffset + glyph.yoff));
            for (int gy = 0; gy < glyph.height; ++gy) {
                const int dstY = drawY + gy;
                if (dstY < 0 || dstY >= targetSurface->h) continue;
                for (int gx = 0; gx < glyph.width; ++gx) {
                    const int dstX = drawX + gx;
                    if (dstX < 0 || dstX >= targetSurface->w) continue;
                    const uint8_t alpha = glyph.bitmap[gy * glyph.width + gx];
                    const uint8_t finalAlpha = static_cast<uint8_t>(
                        static_cast<int>(alpha) * glyphColor.a / 255);
                    if (!finalAlpha) continue;
                    auto* pixel = static_cast<uint8_t*>(targetSurface->pixels) +
                                  dstY * targetSurface->pitch + dstX * 4;
                    const float sourceAlpha = finalAlpha / 255.0f;
                    const float inverse = 1.0f - sourceAlpha;
                    pixel[0] = static_cast<uint8_t>(glyphColor.r * sourceAlpha + pixel[0] * inverse);
                    pixel[1] = static_cast<uint8_t>(glyphColor.g * sourceAlpha + pixel[1] * inverse);
                    pixel[2] = static_cast<uint8_t>(glyphColor.b * sourceAlpha + pixel[2] * inverse);
                    pixel[3] = std::max(pixel[3], finalAlpha);
                }
            }
        }
    }
    if (mustUnlock) SDL_UnlockSurface(targetSurface);
}

std::vector<std::string> FontRenderer::wrapText(const std::string& utf8Text, float fontSize, float maxWidth) {
    std::vector<std::string> result;
    if (utf8Text.empty()) return result;

    if (m_textShaper.isAdvancedBackendActive()) {
        const auto shaped = shapeTextShared(utf8Text, fontSize, maxWidth);
        const auto document = Rowl::Text::parseMarkup(utf8Text);
        for (const auto& line : shaped->lines) {
            std::string logicalLine;
            const size_t end = std::min<std::size_t>(
                document.chars.size(), line.firstScalar + line.scalarCount);
            for (size_t i = line.firstScalar; i < end; ++i)
                logicalLine += document.chars[i].text;
            result.push_back(std::move(logicalLine));
        }
        return result;
    }

    const std::string plainText = Rowl::Text::stripMarkup(utf8Text);

    if (maxWidth <= 0.0f) {
        result.push_back(plainText);
        return result;
    }

    int pixelHeight = static_cast<int>(std::round(fontSize));
    if (pixelHeight < 8) pixelHeight = 8;

    // First split into paragraphs by newline
    std::vector<std::string> paragraphs;
    std::string currentPara;
    for (char c : plainText) {
        if (c == '\n') {
            paragraphs.push_back(currentPara);
            currentPara.clear();
        } else {
            currentPara += c;
        }
    }
    paragraphs.push_back(currentPara);

    for (const auto& para : paragraphs) {
        if (para.empty()) {
            result.push_back("");
            continue;
        }

        // Tokenize into words
        std::vector<std::string> words;
        std::string word;
        for (char c : para) {
            if (c == ' ') {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
                words.push_back(" ");
            } else {
                word += c;
            }
        }
        if (!word.empty()) words.push_back(word);

        std::string currentLine;
        float currentLineWidth = 0.0f;

        for (const auto& w : words) {
            float wordWidth = measureTextWidth(w, fontSize);

            if (currentLineWidth + wordWidth <= maxWidth || currentLine.empty()) {
                currentLine += w;
                currentLineWidth += wordWidth;
            } else {
                if (!currentLine.empty()) {
                    // Trim trailing space from line
                    while (!currentLine.empty() && currentLine.back() == ' ') currentLine.pop_back();
                    result.push_back(currentLine);
                }
                if (w != " ") {
                    currentLine = w;
                    currentLineWidth = wordWidth;
                } else {
                    currentLine.clear();
                    currentLineWidth = 0.0f;
                }
            }
        }

        if (!currentLine.empty()) {
            while (!currentLine.empty() && currentLine.back() == ' ') currentLine.pop_back();
            result.push_back(currentLine);
        }
    }

    return result;
}

void FontRenderer::renderText(
    SDL_Surface* targetSurface,
    const std::string& utf8Text,
    float startX, float startY,
    float fontSize,
    SDL_Color color,
    float maxWidth,
    float maxHeight,
    const std::string& alignment,
    size_t maxVisibleRevealUnits
) {
    if (!m_loaded || !targetSurface || utf8Text.empty() || maxVisibleRevealUnits == 0) return;

    if (m_textShaper.isAdvancedBackendActive()) {
        const auto shaped = shapeTextShared(utf8Text, fontSize, maxWidth);
        renderShapedText(targetSurface, *shaped, startX, startY, fontSize, color,
                         maxWidth, maxHeight, alignment, maxVisibleRevealUnits);
        return;
    }

    const auto fallback = shapeTextShared(utf8Text, fontSize, maxWidth);
    size_t visibleScalars = 0;
    const size_t visibleUnits = std::min(
        maxVisibleRevealUnits, fallback->revealUnits.size());
    for (size_t i = 0; i < visibleUnits; ++i)
        visibleScalars += fallback->revealUnits[i].scalarCount;
    const auto document = Rowl::Text::parseMarkup(utf8Text);
    std::string visibleText;
    for (size_t i = 0; i < std::min(visibleScalars, document.chars.size()); ++i)
        visibleText += document.chars[i].text;

    bool mustUnlock = false;
    if (SDL_MUSTLOCK(targetSurface)) {
        if (!SDL_LockSurface(targetSurface)) return;
        mustUnlock = true;
    }

    int pixelHeight = static_cast<int>(std::round(fontSize));
    if (pixelHeight < 8) pixelHeight = 8;

    auto* info = static_cast<stbtt_fontinfo*>(m_fontInfo);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &lineGap);
    float vScale = stbtt_ScaleForPixelHeight(info, static_cast<float>(pixelHeight));
    float baselineOffset = static_cast<float>(ascent) * vScale;
    float lineHeight = static_cast<float>(ascent - descent + lineGap) * vScale * 1.15f;

    auto lines = wrapText(visibleText, fontSize, maxWidth);
    float currentY = startY;
    size_t codepointsDrawn = 0;

    for (const auto& line : lines) {
        if (maxHeight > 0.0f && (currentY - startY + lineHeight > maxHeight)) {
            break; // Do not overflow box height
        }
        if (codepointsDrawn >= maxVisibleRevealUnits) {
            break;
        }

        float lineX = startX;
        if (alignment == "Center" && maxWidth > 0.0f) {
            float lineWidth = measureTextWidth(line, fontSize);
            lineX = startX + (maxWidth - lineWidth) / 2.0f;
        } else if (alignment == "Right" && maxWidth > 0.0f) {
            float lineWidth = measureTextWidth(line, fontSize);
            lineX = startX + maxWidth - lineWidth;
        }

        float cursorX = lineX;
        size_t byteIdx = 0;

        while (byteIdx < line.length() && codepointsDrawn < maxVisibleRevealUnits) {
            uint32_t cp = getNextCodepoint(line, byteIdx);
            codepointsDrawn++;

            const Glyph* g = getGlyph(cp, pixelHeight);
            if (!g) continue;

            if (g->width > 0 && g->height > 0 && !g->bitmap.empty()) {
                int drawX = static_cast<int>(std::round(cursorX + g->xoff));
                int drawY = static_cast<int>(std::round(currentY + baselineOffset + g->yoff));

                // Direct alpha blending onto RGBA32 surface
                for (int gy = 0; gy < g->height; ++gy) {
                    int dstY = drawY + gy;
                    if (dstY < 0 || dstY >= targetSurface->h) continue;

                    for (int gx = 0; gx < g->width; ++gx) {
                        int dstX = drawX + gx;
                        if (dstX < 0 || dstX >= targetSurface->w) continue;

                        uint8_t alpha = g->bitmap[gy * g->width + gx];
                        if (alpha == 0) continue;

                        uint8_t finalAlpha = static_cast<uint8_t>((static_cast<int>(alpha) * static_cast<int>(color.a)) / 255);
                        if (finalAlpha == 0) continue;

                        uint8_t* pixel = static_cast<uint8_t*>(targetSurface->pixels) + dstY * targetSurface->pitch + dstX * 4;
                        float srcA = finalAlpha / 255.0f;
                        float invA = 1.0f - srcA;

                        // RGBA32 blending
                        pixel[0] = static_cast<uint8_t>(color.r * srcA + pixel[0] * invA);
                        pixel[1] = static_cast<uint8_t>(color.g * srcA + pixel[1] * invA);
                        pixel[2] = static_cast<uint8_t>(color.b * srcA + pixel[2] * invA);
                        pixel[3] = std::max(pixel[3], finalAlpha);
                    }
                }
            }

            cursorX += static_cast<float>(g->advance);
        }

        currentY += lineHeight;
    }

    if (mustUnlock) {
        SDL_UnlockSurface(targetSurface);
    }
}

} // namespace Rowl::Render
