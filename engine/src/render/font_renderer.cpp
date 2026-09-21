#include "rowl/render/font_renderer.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/text/utf8.hpp"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cmath>
#include <algorithm>

#define STB_TRUETYPE_IMPLEMENTATION
#include "thirdparty/stb_truetype.h"

namespace Rowl::Render {

namespace {

// Single blended glyph texel onto an RGBA32 surface. Bounds-checked so
// outline stamps can overshoot glyph boxes safely.
void blendTexel(SDL_Surface* target, int x, int y, SDL_Color color, uint8_t coverage) {
    if (!target || coverage == 0 || x < 0 || y < 0 || x >= target->w || y >= target->h)
        return;
    const uint8_t finalAlpha = static_cast<uint8_t>(
        static_cast<int>(coverage) * color.a / 255);
    if (!finalAlpha) return;
    auto* pixel = static_cast<uint8_t*>(target->pixels) +
                  y * target->pitch + x * 4;
    const float sourceAlpha = finalAlpha / 255.0f;
    const float inverse = 1.0f - sourceAlpha;
    pixel[0] = static_cast<uint8_t>(color.r * sourceAlpha + pixel[0] * inverse);
    pixel[1] = static_cast<uint8_t>(color.g * sourceAlpha + pixel[1] * inverse);
    pixel[2] = static_cast<uint8_t>(color.b * sourceAlpha + pixel[2] * inverse);
    pixel[3] = std::max(pixel[3], finalAlpha);
}

constexpr SDL_Color kContrastOutline{0, 0, 0, 255};

// A3-tur7 (font-harden): glif-önbellek tavanı — anahtar (boyut×codepoint)
// hikaye-metninden sürülebilir, sınırsız büyümeye karşı en-eskiden-düşür.
// 2048, meşru kullanımı (birkaç boyut × geniş Unicode aralığı) kapsamaz-dışı
// bırakmayacak kadar yüksek, hostile-büyümeyi kesecek kadar düşüktür.
constexpr size_t kMaxGlyphCacheEntries = 2048;
// A3-tur7: font-dosya tavanı — tellg -1 (hata) + devasa resize'a karşı.
// 64MB, mevcut en-ağır CJK fontların (~20MB) çok üstünde güvenli-pay bırakır.
constexpr uint64_t kMaxFontFileBytes = 64ULL * 1024ULL * 1024ULL;

} // namespace

FontRenderer::FontRenderer() {
    m_fontInfo = new stbtt_fontinfo();
}

void FontRenderer::setTextScale(float scale) {
    const float clamped = std::isfinite(scale) ? std::clamp(scale, 1.0f, 2.0f) : 1.0f;
    if (clamped != m_textScale) {
        m_textScale = clamped;
        m_shapeCache.clear();
    }
}

void FontRenderer::setHighContrast(bool enabled) {
    m_highContrast = enabled;
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
    // A3-tur7: tellg hatası (-1) devasa-resize'a dönüşmesin; dev-dosya kapıda.
    if (size < 0 || static_cast<uint64_t>(size) > kMaxFontFileBytes) {
        ROWL_LOG_WARN("Font file has invalid or excessive size: " + fontPath);
        return false;
    }
    file.seekg(0, std::ios::beg);

    // #57 (stage-then-commit): dosya once yerel tampona okunur; canli
    // m_fontBuffer'a read+parse basarisi dogrulanmadan dokunulmaz. Eski
    // kod resize'i canli tamponda yapip read-fail'de erken donuyordu:
    // realloc tamponu tasimis, info->data (non-owning alias) sarkan
    // kalmis, m_loaded true kaldigindan getGlyph sarkan pointer'i
    // deref ediyordu (UAF).
    std::vector<uint8_t> staged(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(staged.data()), size)) {
        ROWL_LOG_ERROR("Failed to read font file data: " + fontPath);
        return false;
    }

    return loadFontFromMemory(staged.data(), staged.size());
}

bool FontRenderer::loadFontFromPath(const std::filesystem::path& fontPath) {
    // A3-tur4 (metin turu): error_code yoklamasi — varlik/kosul throw
    // uretmez (initFontRenderer sistem-aday dongusu kare-disi ama
    // firtina-gurultusuz olmali); acim Windows'ta wide (Unicode yol),
    // POSIX'te dar UTF-8 bayt (bozulmadan gecer).
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fontPath, ec) || ec) {
        ROWL_LOG_WARN("Could not open font file: " + fontPath.string());
        return false;
    }
#if defined(_WIN32)
    std::ifstream file(fontPath.wstring(), std::ios::binary | std::ios::ate);
#else
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
#endif
    if (!file.is_open()) {
        ROWL_LOG_WARN("Could not open font file: " + fontPath.string());
        return false;
    }
    std::streamsize size = file.tellg();
    // A3-tur7: tellg-guard (dar-yol emsali — bkz. loadFont).
    if (size < 0 || static_cast<uint64_t>(size) > kMaxFontFileBytes) {
        ROWL_LOG_WARN("Font file has invalid or excessive size: " + fontPath.string());
        return false;
    }
    file.seekg(0, std::ios::beg);

    // #57 (stage-then-commit): loadFont ile ayni gerekce — once yerel
    // tampon, commit yalniz loadFontFromMemory icinde dogrulama sonrasi.
    std::vector<uint8_t> staged(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(staged.data()), size)) {
        ROWL_LOG_ERROR("Failed to read font file data: " + fontPath.string());
        return false;
    }

    return loadFontFromMemory(staged.data(), staged.size());
}

bool FontRenderer::loadFontFromMemory(const uint8_t* data, size_t size) {
    if (!data || size == 0) return false;

    // #57 (stage-then-commit): girdi once yerel tampona kopyalanir; canli
    // m_fontBuffer / m_fontInfo'ya tum dogrulama bitmeden dokunulmaz.
    // Basarisiz reload eski calisan fontu aynen birakir (fail-closed).
    std::vector<uint8_t> staged(data, data + size);

    // Savunma-derinligi: kesik/bozuk girdide stb tablo-taramasi tampon
    // disina tasmasin. sfnt basligi her kapsayicide ayni yerlesimdedir
    // (imza[0..3] + numTables[4..5]); dizin tampona sigmiyorsa parse'e
    // girmeden reddet. (Kalinti: dizin-ici sahte offset'ler stb ic-okumada
    // hâlâ tasmaya zorlayabilir — pre-existing stb zafi, ayrı bulgu adayı.)
    if (staged.size() < 12) return false;
    {
        const uint32_t claimedTables =
            (static_cast<uint32_t>(staged[4]) << 8) | staged[5];
        if (static_cast<uint64_t>(claimedTables) * 16 + 12 > staged.size())
            return false;
    }

    // Asama 1: stb parse — canli info degil, yerel info kullanilir.
    // info->data yereli aliaslar; canli info hic sarkan konuma dusmez.
    stbtt_fontinfo stagedInfo{};
    if (!stbtt_InitFont(&stagedInfo, staged.data(), 0)) {
        // Font collection (.ttc/.ttf with several faces): stb_truetype
        // needs the byte offset of a face, so probe each face in order.
        bool collectionOk = false;
        const int faces = stbtt_GetNumberOfFonts(staged.data());
        for (int face = 0; face < faces && !collectionOk; ++face) {
            const int offset = stbtt_GetFontOffsetForIndex(staged.data(), face);
            if (offset >= 0)
                collectionOk = stbtt_InitFont(&stagedInfo, staged.data(), offset) != 0;
        }
        if (!collectionOk) {
            ROWL_LOG_ERROR("stbtt_InitFont failed to parse font buffer!");
            return false;
        }
    }

    // Asama 2: shaper (ic-dengelemeli: basarisizsa canli state korunur).
    // #41 notu: derlenmis backend + parse-basarisizligi artik sessiz
    // fallback degil, yukleme-basarisizligidir. Derlenmemis backend ise
    // kalici insa-kararidir (stb yolu mesrudur, log'da gorunur).
    const bool advanced = m_textShaper.loadFontFromMemory(
        staged.data(), staged.size());
    if (!advanced && Rowl::Text::TextShaper::isAdvancedBackendCompiled()) {
        ROWL_LOG_ERROR("Advanced text backend rejected the font buffer; "
                       "keeping previously loaded font.");
        return false;
    }

    // Asama 3: commit — tek hamle. Ayni baytlar az once dogrulandi; canli
    // info re-init'i savunma-derinligidir (beklenmedik rette fail-closed).
    m_fontBuffer = std::move(staged);
    auto* info = static_cast<stbtt_fontinfo*>(m_fontInfo);
    if (!stbtt_InitFont(info, m_fontBuffer.data(), 0)) {
        bool collectionOk = false;
        const int faces = stbtt_GetNumberOfFonts(m_fontBuffer.data());
        for (int face = 0; face < faces && !collectionOk; ++face) {
            const int offset = stbtt_GetFontOffsetForIndex(m_fontBuffer.data(), face);
            if (offset >= 0)
                collectionOk = stbtt_InitFont(info, m_fontBuffer.data(), offset) != 0;
        }
        if (!collectionOk) {
            ROWL_LOG_ERROR("stbtt_InitFont failed on validated font buffer!");
            m_glyphCache.clear();
            m_shapedGlyphCache.clear();
            m_shapeCache.clear();
            m_loaded = false;
            return false;
        }
    }

    m_glyphCache.clear();
    m_shapedGlyphCache.clear();
    m_shapeCache.clear();
    m_loaded = true;
    ROWL_LOG_INFO(std::string("✅ TrueType Font Loaded Successfully (") +
        (advanced ? "HarfBuzz/FriBidi" : "stb fallback") + ").");
    return true;
}

uint32_t FontRenderer::getNextCodepoint(const std::string& str, size_t& byteIndex) {
    // A3-tur4 (metin turu): cozumleme tek paylasimli strict decoder'da
    // (rowl/text/utf8.hpp). Gecerli girdi davranisi ayni; gecersiz/yarim
    // dizi artik ham lead bayti degil U+FFFD doner (onceki sessiz-cop
    // codepoint uretimi kapandi). Imza sabit (testler + stb yedek yolu).
    if (byteIndex >= str.length()) return 0;
    const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
        str.data() + byteIndex, str.data() + str.size());
    byteIndex += decoded.length;
    return decoded.codepoint;
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
        if (m_glyphCache.size() >= kMaxGlyphCacheEntries) {
            m_glyphCache.erase(m_glyphCache.begin());
        }
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

    // A3-tur7: tavan-aşımında en-eskiden-düşür (boşluk-kolu emsali).
    if (m_glyphCache.size() >= kMaxGlyphCacheEntries) {
        m_glyphCache.erase(m_glyphCache.begin());
    }
    m_glyphCache[key] = glyph;
    return &m_glyphCache[key];
}

float FontRenderer::measureTextWidth(const std::string& utf8Text, float fontSize,
                                       const std::string& language) {
    if (!m_loaded || utf8Text.empty()) return 0.0f;

    if (m_textShaper.isAdvancedBackendActive()) {
        return shapeTextShared(utf8Text, fontSize, 0.0f, language)->width;
    }

    const std::string plainText = Rowl::Text::stripMarkup(utf8Text);
    int pixelHeight = static_cast<int>(std::round(effectiveFontSize(fontSize)));
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
                                                float maxWidth,
                                                const std::string& language) const {
    return *shapeTextShared(markup, fontSize, maxWidth, language);
}

std::shared_ptr<const Rowl::Text::ShapedText> FontRenderer::shapeTextShared(
    const std::string& markup, float fontSize, float maxWidth,
    const std::string& language) const {
    for (const auto& cached : m_shapeCache) {
        if (cached.markup == markup && cached.fontSize == fontSize &&
            cached.maxWidth == maxWidth && cached.textScale == m_textScale &&
            cached.language == language)
            return cached.layout;
    }
    Rowl::Text::ShapeOptions options;
    options.fontSize = effectiveFontSize(fontSize);
    options.maxWidth = maxWidth;
    options.language = language;
    auto layout = std::make_shared<Rowl::Text::ShapedText>(
        m_textShaper.shapeMarkup(markup, options));
    // A3-tur7: insert tahsisi patlarsa (OOM) cachesiz-devam — fail-open
    // performans, fail-closed doğruluk (kilit-test deseni tur2'den).
    try {
        if (m_shapeCache.size() >= 16) m_shapeCache.erase(m_shapeCache.begin());
        m_shapeCache.push_back({markup, fontSize, maxWidth, m_textScale, language, layout});
    } catch (...) {
        // Bilinçli-yutma: önbellek lüks, layout zaten hazır — render sürer.
    }
    return layout;
}

void FontRenderer::invalidateShapeCache() {
    // std::bad_alloc güvenliği: clear() wg. shrink gerektirmez; istisna
    // atarsa önbellek eski halinde kalır — render yine doğru sürer.
    try {
        m_shapeCache.clear();
    } catch (...) {
    }
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
            const float glyphSize = effectiveFontSize(shapedGlyph.style.hasSize
                ? shapedGlyph.style.size : fontSize);
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
                // A3-tur7: shaped-glif önbelleği de tavanlı (getGlyph emsali).
                if (m_shapedGlyphCache.size() >= kMaxGlyphCacheEntries) {
                    m_shapedGlyphCache.erase(m_shapedGlyphCache.begin());
                }
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
                for (int gx = 0; gx < glyph.width; ++gx) {
                    const uint8_t alpha = glyph.bitmap[gy * glyph.width + gx];
                    if (!alpha) continue;
                    if (m_highContrast) {
                        // Dark halo first: 8-neighbourhood outline.
                        for (int oy = -1; oy <= 1; ++oy)
                            for (int ox = -1; ox <= 1; ++ox) {
                                if (ox == 0 && oy == 0) continue;
                                blendTexel(targetSurface, drawX + gx + ox,
                                           drawY + gy + oy, kContrastOutline, alpha);
                            }
                    }
                    blendTexel(targetSurface, drawX + gx, drawY + gy,
                               glyphColor, alpha);
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

    int pixelHeight = static_cast<int>(std::round(effectiveFontSize(fontSize)));
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
                    for (int gx = 0; gx < g->width; ++gx) {
                        uint8_t alpha = g->bitmap[gy * g->width + gx];
                        if (alpha == 0) continue;
                        if (m_highContrast) {
                            for (int oy = -1; oy <= 1; ++oy)
                                for (int ox = -1; ox <= 1; ++ox) {
                                    if (ox == 0 && oy == 0) continue;
                                    blendTexel(targetSurface, drawX + gx + ox,
                                               drawY + gy + oy, kContrastOutline, alpha);
                                }
                        }
                        blendTexel(targetSurface, drawX + gx, drawY + gy, color, alpha);
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
