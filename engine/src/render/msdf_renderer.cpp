#include "rowl/render/msdf_renderer.hpp"
#include "rowl/core/logger.hpp"
#include <algorithm>

namespace Rowl::Render {

MsdfRenderer::MsdfRenderer() = default;
MsdfRenderer::~MsdfRenderer() = default;

bool MsdfRenderer::loadAtlasMetadata(const std::string& jsonMetadata) {
    ROWL_LOG_INFO("Loading MSDF Font Atlas Metadata...");
    // Mock parser for atlas metadata
    m_pixelRange = 4.0f;
    m_atlasWidth = 512.0f;
    m_atlasHeight = 512.0f;
    m_loaded = true;
    ROWL_LOG_INFO("MSDF Font Atlas loaded successfully. Pixel Range: " + std::to_string(m_pixelRange));
    return true;
}

float MsdfRenderer::calculateMedianDistance(float r, float g, float b) {
    return std::max(std::min(r, g), std::min(std::max(r, g), b));
}

} // namespace Rowl::Render
