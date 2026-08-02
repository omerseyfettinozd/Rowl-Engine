#include "rowl/render/msdf_renderer.hpp"
#include "rowl/core/logger.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Rowl::Render {

MsdfRenderer::MsdfRenderer() = default;
MsdfRenderer::~MsdfRenderer() = default;

bool MsdfRenderer::loadAtlasMetadata(const std::string& jsonMetadata) {
    ROWL_LOG_INFO("Loading MSDF Font Atlas Metadata...");

    try {
        auto json = nlohmann::json::parse(jsonMetadata);

        m_pixelRange = json.value("pixel_range", 4.0f);
        m_atlasWidth = json.value("atlas_width", 512.0f);
        m_atlasHeight = json.value("atlas_height", 512.0f);

        if (json.contains("glyphs") && json["glyphs"].is_array()) {
            for (const auto& glyphJson : json["glyphs"]) {
                MsdfGlyphMetrics glyph;
                glyph.unicode = glyphJson.value("unicode", 0u);
                glyph.advance = glyphJson.value("advance", 0.0f);
                glyph.planeLeft = glyphJson.value("plane_left", 0.0f);
                glyph.planeBottom = glyphJson.value("plane_bottom", 0.0f);
                glyph.planeRight = glyphJson.value("plane_right", 0.0f);
                glyph.planeTop = glyphJson.value("plane_top", 0.0f);
                glyph.atlasLeft = glyphJson.value("atlas_left", 0.0f);
                glyph.atlasBottom = glyphJson.value("atlas_bottom", 0.0f);
                glyph.atlasRight = glyphJson.value("atlas_right", 0.0f);
                glyph.atlasTop = glyphJson.value("atlas_top", 0.0f);

                if (glyph.unicode != 0) {
                    m_glyphs[glyph.unicode] = glyph;
                }
            }
        }

        m_loaded = true;
        ROWL_LOG_INFO("MSDF Font Atlas loaded successfully. Pixel Range: " + std::to_string(m_pixelRange) + ", Glyphs: " + std::to_string(m_glyphs.size()));
        return true;
    } catch (const nlohmann::json::parse_error& e) {
        ROWL_LOG_ERROR("MSDF atlas metadata JSON parse error: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("MSDF atlas metadata load error: " + std::string(e.what()));
        return false;
    }
}

float MsdfRenderer::calculateMedianDistance(float r, float g, float b) {
    return std::max(std::min(r, g), std::min(std::max(r, g), b));
}

} // namespace Rowl::Render