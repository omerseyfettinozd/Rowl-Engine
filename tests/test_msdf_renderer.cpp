/**
 * test_msdf_renderer.cpp — MSDF atlas sampling and glyph metrics.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_msdf_renderer() {
    TEST_SECTION("MSDF Atlas Sampling & Metrics");
    Rowl::Render::MsdfRenderer renderer;
    if (!renderer.loadAtlasMetadata(R"({"pixel_range":4,"atlas_width":2,"atlas_height":2,"glyphs":[{"unicode":65,"advance":0.6}]})")) {
        std::cerr << "MSDF metadata load failed" << std::endl;
        exit(1);
    }
    std::vector<uint8_t> pixels = {
        255, 255, 255, 255, 0, 0, 0, 255,
        128, 128, 128, 255, 64, 64, 64, 255
    };
    if (!renderer.loadAtlasPixels(std::move(pixels), 2, 2) || !renderer.isLoaded() ||
        renderer.sampleOpacity(0.0f, 0.0f) < 0.99f || renderer.sampleOpacity(1.0f, 0.0f) > 0.01f ||
        std::abs(renderer.measureTextWidth("AA", 20.0f) - 24.0f) > 0.01f) {
        std::cerr << "MSDF atlas sampling or metrics mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("MSDF RGB Median Sampling and UTF-8 Glyph Metrics");

    std::ifstream generatedAtlas("Assets/fonts/msdf/default.json");
    const std::string generatedMetadata((std::istreambuf_iterator<char>(generatedAtlas)), {});
    Rowl::Render::MsdfRenderer generated;
    if (generatedMetadata.empty() || !generated.loadAtlasMetadata(generatedMetadata) ||
        !generated.findGlyph('A') || generated.getAtlasWidth() != 1024.0f ||
        generated.getPixelRange() != 4.0f) {
        std::cerr << "Generated MSDF atlas metadata is not runtime-compatible" << std::endl;
        exit(1);
    }
    TEST_PASS("Generated MSDF Atlas Metadata and Glyph Lookup");
}
