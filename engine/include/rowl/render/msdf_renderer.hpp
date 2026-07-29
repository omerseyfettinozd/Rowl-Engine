#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

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
    float calculateMedianDistance(float r, float g, float b);

    bool isLoaded() const { return m_loaded; }
    float getPixelRange() const { return m_pixelRange; }

private:
    float m_pixelRange = 4.0f;
    float m_atlasWidth = 512.0f;
    float m_atlasHeight = 512.0f;
    std::unordered_map<uint32_t, MsdfGlyphMetrics> m_glyphs;
    bool m_loaded = false;
};

} // namespace Rowl::Render
