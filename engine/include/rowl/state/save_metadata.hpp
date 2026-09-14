#pragma once

#include <cstdint>
#include <string>

namespace Rowl::State {

/// Downscaled PNG thumbnail produced from an RGBA framebuffer.
struct ThumbnailResult {
    /// Complete PNG file bytes (empty when no pixels were available).
    std::string png;
    uint32_t width = 0;
    uint32_t height = 0;
};

/// Maximum thumbnail width in pixels; height follows the aspect ratio.
inline constexpr uint32_t kThumbnailMaxWidth = 320;

/// Downscales RGBA input to kThumbnailMaxWidth and encodes a plain PNG
/// (8-bit RGBA, stored deflate blocks — dependency-free and deterministic).
/// Returns empty bytes for null/degenerate input instead of failing.
ThumbnailResult encodeThumbnailPng(
    const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t pitchBytes);

/// Standard base64 (padded) encoding for embedding PNG bytes in slot JSON.
std::string base64Encode(const uint8_t* data, uint32_t size);

/// Decodes base64 produced by base64Encode; returns false on malformed
/// input instead of a half buffer.
bool base64Decode(const std::string& text, std::string& outBytes);

/// Current UTC time as ISO-8601 (`YYYY-MM-DDTHH:MM:SSZ`).
std::string iso8601UtcNow();

/// Truncates display text to maxBytes at a UTF-8 boundary (no half runes).
std::string truncateSummary(const std::string& text, uint32_t maxBytes = 320);

} // namespace Rowl::State
