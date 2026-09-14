#pragma once

#include "rowl/text/markup_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Rowl::Text {

enum class ShapingBackend : uint8_t {
    StbFallback = 0,
    HarfBuzzFriBidi = 1,
};

struct ShapeOptions {
    float fontSize = 24.0f;
    float maxWidth = 0.0f;
    float lineHeightMultiplier = 1.15f;
    std::string language;
};

struct RevealUnit {
    uint32_t firstScalar = 0;
    uint32_t scalarCount = 0;
    float pauseBefore = 0.0f;
    float speed = 1.0f;
    uint32_t representativeCodepoint = 0;
};

struct ShapedGlyph {
    uint32_t glyphIndex = 0;
    uint32_t logicalScalar = 0;
    uint32_t revealIndex = 0;
    uint32_t lineIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    float xAdvance = 0.0f;
    float yAdvance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
    MarkupChar style{};
};

struct ShapedLine {
    uint32_t firstGlyph = 0;
    uint32_t glyphCount = 0;
    uint32_t firstScalar = 0;
    uint32_t scalarCount = 0;
    float width = 0.0f;
    float baseline = 0.0f;
};

struct ShapedText {
    std::string plainText;
    std::vector<ShapedGlyph> glyphs;
    std::vector<ShapedLine> lines;
    std::vector<RevealUnit> revealUnits;
    std::vector<MarkupDiagnostic> diagnostics;
    ShapingBackend backend = ShapingBackend::StbFallback;
    float width = 0.0f;
    float height = 0.0f;
    double trailingPause = 0.0;
};

struct RasterizedShapedGlyph {
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    std::vector<uint8_t> bitmap;
};

struct RevealState {
    std::size_t visibleUnits = 0;
    bool complete = true;
    double totalSeconds = 0.0;
};

/// Font-backed, deterministic text layout service. Its public header contains
/// no FreeType/HarfBuzz/FriBidi/libunibreak types, keeping those dependencies
/// private to the implementation and optional at build time.
class TextShaper {
public:
    TextShaper();
    ~TextShaper();
    TextShaper(TextShaper&&) noexcept;
    TextShaper& operator=(TextShaper&&) noexcept;
    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;

    bool loadFontFromMemory(const uint8_t* data, std::size_t size) noexcept;
    bool isAdvancedBackendActive() const noexcept;
    static bool isAdvancedBackendCompiled() noexcept;

    ShapedText shapeMarkup(std::string_view markup,
                           const ShapeOptions& options = {}) const noexcept;
    bool rasterizeGlyph(uint32_t glyphIndex, float pixelSize,
                        RasterizedShapedGlyph& out) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Evaluates pause/speed timing over reveal units. Measurement, rendering and
/// this timeline consume the same ShapedText instance.
RevealState evaluateReveal(const ShapedText& shaped, double elapsedSeconds,
                           double baseMillisecondsPerUnit) noexcept;

std::string shapedTextToJson(const ShapedText& shaped) noexcept;

} // namespace Rowl::Text
