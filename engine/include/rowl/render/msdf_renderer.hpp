#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Rowl::Render {

struct MsdfGlyphMetrics {
    uint32_t unicode;
    float advance;
    float planeLeft, planeBottom, planeRight, planeTop;
    float atlasLeft, atlasBottom, atlasRight, atlasTop;
};

class MsdfRenderer {
public:
    MsdfRenderer();
    ~MsdfRenderer();

    bool loadAtlasMetadata(const std::string& jsonMetadata);
    bool loadAtlasPixels(std::vector<uint8_t> rgbaPixels, uint32_t width, uint32_t height);
    float calculateMedianDistance(float r, float g, float b) const;
    float sampleOpacity(float normalizedX, float normalizedY, float screenPixelRange = 1.0f) const;
    float measureTextWidth(const std::string& utf8Text, float pixelHeight) const;

    bool isLoaded() const { return m_loaded && !m_atlasPixels.empty(); }
    bool hasMetadata() const { return m_loaded; }
    float getPixelRange() const { return m_pixelRange; }
    float getAtlasWidth() const { return m_atlasWidth; }
    float getAtlasHeight() const { return m_atlasHeight; }
    const MsdfGlyphMetrics* findGlyph(uint32_t unicode) const {
        const auto it = m_glyphs.find(unicode);
        return it == m_glyphs.end() ? nullptr : &it->second;
    }
    const std::unordered_map<uint32_t, MsdfGlyphMetrics>& getGlyphs() const { return m_glyphs; }

private:
    float m_pixelRange = 4.0f;
    float m_atlasWidth = 512.0f;
    float m_atlasHeight = 512.0f;
    std::unordered_map<uint32_t, MsdfGlyphMetrics> m_glyphs;
    std::vector<uint8_t> m_atlasPixels;
    uint32_t m_pixelWidth = 0;
    uint32_t m_pixelHeight = 0;
    bool m_loaded = false;
};

} // namespace Rowl::Render
