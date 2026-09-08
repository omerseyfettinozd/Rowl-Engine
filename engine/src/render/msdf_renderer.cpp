#include "rowl/render/msdf_renderer.hpp"
#include "rowl/core/logger.hpp"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace Rowl::Render {

MsdfRenderer::MsdfRenderer() = default;
MsdfRenderer::~MsdfRenderer() = default;

bool MsdfRenderer::loadAtlasMetadata(const std::string& jsonMetadata) {
    ROWL_LOG_INFO("Loading MSDF Font Atlas Metadata...");

    try {
        auto json = nlohmann::json::parse(jsonMetadata);

        m_glyphs.clear();
        const auto& atlas = json.contains("atlas") && json["atlas"].is_object() ? json["atlas"] : json;
        m_pixelRange = atlas.value("distanceRange", json.value("pixel_range", 4.0f));
        m_atlasWidth = atlas.value("width", json.value("atlas_width", 512.0f));
        m_atlasHeight = atlas.value("height", json.value("atlas_height", 512.0f));

        if (json.contains("glyphs") && json["glyphs"].is_array()) {
            for (const auto& glyphJson : json["glyphs"]) {
                MsdfGlyphMetrics glyph;
                glyph.unicode = glyphJson.value("unicode", 0u);
                glyph.advance = glyphJson.value("advance", 0.0f);
                const auto& plane = glyphJson.contains("planeBounds") ? glyphJson["planeBounds"] : glyphJson;
                const auto& bounds = glyphJson.contains("atlasBounds") ? glyphJson["atlasBounds"] : glyphJson;
                glyph.planeLeft = plane.value("left", glyphJson.value("plane_left", 0.0f));
                glyph.planeBottom = plane.value("bottom", glyphJson.value("plane_bottom", 0.0f));
                glyph.planeRight = plane.value("right", glyphJson.value("plane_right", 0.0f));
                glyph.planeTop = plane.value("top", glyphJson.value("plane_top", 0.0f));
                glyph.atlasLeft = bounds.value("left", glyphJson.value("atlas_left", 0.0f));
                glyph.atlasBottom = bounds.value("bottom", glyphJson.value("atlas_bottom", 0.0f));
                glyph.atlasRight = bounds.value("right", glyphJson.value("atlas_right", 0.0f));
                glyph.atlasTop = bounds.value("top", glyphJson.value("atlas_top", 0.0f));

                if (glyph.unicode != 0) {
                    m_glyphs[glyph.unicode] = glyph;
                }
            }
        }

        if (!std::isfinite(m_pixelRange) || m_pixelRange <= 0.0f ||
            !std::isfinite(m_atlasWidth) || !std::isfinite(m_atlasHeight) ||
            m_atlasWidth <= 0.0f || m_atlasHeight <= 0.0f) {
            ROWL_LOG_ERROR("MSDF atlas metadata contains invalid dimensions");
            return false;
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

bool MsdfRenderer::loadAtlasPixels(std::vector<uint8_t> rgbaPixels, uint32_t width, uint32_t height) {
    constexpr uint64_t kMaxAtlasPixels = 16ULL * 1024ULL * 1024ULL;
    if (width == 0 || height == 0 || static_cast<uint64_t>(width) * height > kMaxAtlasPixels ||
        rgbaPixels.size() != static_cast<size_t>(width) * height * 4) {
        ROWL_LOG_ERROR("MSDF atlas pixel buffer has invalid dimensions");
        return false;
    }
    m_atlasPixels = std::move(rgbaPixels);
    m_pixelWidth = width;
    m_pixelHeight = height;
    return true;
}

float MsdfRenderer::calculateMedianDistance(float r, float g, float b) const {
    return std::max(std::min(r, g), std::min(std::max(r, g), b));
}

float MsdfRenderer::sampleOpacity(float normalizedX, float normalizedY, float screenPixelRange) const {
    if (!isLoaded() || !std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        !std::isfinite(screenPixelRange)) return 0.0f;
    const auto x = static_cast<uint32_t>(std::clamp(normalizedX, 0.0f, 1.0f) * static_cast<float>(m_pixelWidth - 1));
    const auto y = static_cast<uint32_t>(std::clamp(normalizedY, 0.0f, 1.0f) * static_cast<float>(m_pixelHeight - 1));
    const size_t offset = (static_cast<size_t>(y) * m_pixelWidth + x) * 4;
    const float distance = calculateMedianDistance(m_atlasPixels[offset] / 255.0f,
                                                    m_atlasPixels[offset + 1] / 255.0f,
                                                    m_atlasPixels[offset + 2] / 255.0f);
    const float range = std::max(0.001f, m_pixelRange * std::max(0.001f, screenPixelRange) / 16.0f);
    return std::clamp((distance - 0.5f) / range + 0.5f, 0.0f, 1.0f);
}

float MsdfRenderer::measureTextWidth(const std::string& utf8Text, float pixelHeight) const {
    if (!m_loaded || pixelHeight <= 0.0f || !std::isfinite(pixelHeight)) return 0.0f;
    float width = 0.0f;
    for (size_t index = 0; index < utf8Text.size();) {
        const auto first = static_cast<uint8_t>(utf8Text[index++]);
        uint32_t codepoint = first;
        int remaining = first < 0x80 ? 0 : (first & 0xE0) == 0xC0 ? 1 : (first & 0xF0) == 0xE0 ? 2 : 3;
        if (remaining > 0 && index + static_cast<size_t>(remaining) <= utf8Text.size()) {
            codepoint = first & ((1u << (7 - remaining - 1)) - 1);
            for (int i = 0; i < remaining; ++i) codepoint = (codepoint << 6) | (static_cast<uint8_t>(utf8Text[index++]) & 0x3Fu);
        }
        if (const auto glyph = m_glyphs.find(codepoint); glyph != m_glyphs.end()) width += glyph->second.advance * pixelHeight;
    }
    return width;
}

} // namespace Rowl::Render
