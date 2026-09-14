/**
 * save_metadata.cpp
 *
 * Faz 2 Dilim 4 save-slot display helpers: dependency-free thumbnail PNG
 * encoding (stored deflate blocks), base64, ISO-8601 UTC clock and UTF-8
 * safe summary truncation. None of this touches simulation state.
 */

#include "rowl/state/save_metadata.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <vector>

namespace Rowl::State {
namespace {

uint32_t crcTableEntry(uint32_t index) {
    uint32_t entry = index;
    for (int bit = 0; bit < 8; ++bit) {
        entry = (entry & 1) ? (0xEDB88320u ^ (entry >> 1)) : (entry >> 1);
    }
    return entry;
}

const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> built{};
        for (uint32_t i = 0; i < 256; ++i) built[i] = crcTableEntry(i);
        return built;
    }();
    return table;
}

uint32_t crc32(const uint8_t* data, uint32_t size, uint32_t seed = 0xFFFFFFFFu) {
    uint32_t crc = seed;
    for (uint32_t i = 0; i < size; ++i) {
        crc = crcTable()[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void appendU32(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void appendChunk(std::string& out, const char* type, const uint8_t* data, uint32_t size) {
    appendU32(out, size);
    const uint32_t typeOffset = static_cast<uint32_t>(out.size());
    out.append(type, 4);
    if (size > 0 && data) {
        out.append(reinterpret_cast<const char*>(data), size);
    }
    const uint32_t crc = crc32(
        reinterpret_cast<const uint8_t*>(out.data() + typeOffset), size + 4);
    appendU32(out, crc);
}

// Minimal zlib stream around stored (uncompressed) deflate blocks. Valid
// per RFC 1950/1951 and accepted by every PNG decoder; chosen over a full
// compressor to keep thumbnails dependency-free and deterministic.
void appendStoredZlib(std::string& out, const uint8_t* data, uint32_t size) {
    out.push_back(static_cast<char>(0x78));
    out.push_back(static_cast<char>(0x01));
    uint32_t adlerA = 1, adlerB = 0;
    uint32_t offset = 0;
    uint32_t remaining = size;
    do {
        const uint32_t block = std::min<uint32_t>(remaining, 65535);
        const bool last = (block == remaining);
        out.push_back(static_cast<char>(last ? 0x01 : 0x00));
        out.push_back(static_cast<char>(block & 0xFF));
        out.push_back(static_cast<char>((block >> 8) & 0xFF));
        out.push_back(static_cast<char>((~block) & 0xFF));
        out.push_back(static_cast<char>(((~block) >> 8) & 0xFF));
        for (uint32_t i = 0; i < block; ++i) {
            const uint8_t byte = data[offset + i];
            out.push_back(static_cast<char>(byte));
            adlerA += byte;
            if (adlerA >= 65521) adlerA -= 65521;
            adlerB += adlerA;
            if (adlerB >= 65521) adlerB %= 65521;
        }
        offset += block;
        remaining -= block;
    } while (remaining > 0);
    appendU32(out, (adlerB << 16) | adlerA);
}

} // namespace

std::string base64Encode(const uint8_t* data, uint32_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    uint32_t i = 0;
    while (i < size) {
        const uint32_t octet0 = data[i++];
        const uint32_t octet1 = i < size ? data[i++] : 0;
        const uint32_t octet2 = i < size ? data[i++] : 0;
        const uint32_t triple = (octet0 << 16) | (octet1 << 8) | octet2;
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
    }
    const uint32_t remainder = size % 3;
    if (remainder == 1) {
        out[out.size() - 1] = '=';
        out[out.size() - 2] = '=';
    } else if (remainder == 2) {
        out[out.size() - 1] = '=';
    }
    return out;
}

bool base64Decode(const std::string& text, std::string& outBytes) {
    static constexpr signed char kInverse[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    outBytes.clear();
    if (text.size() % 4 != 0) return false;
    outBytes.reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        int sextets[4];
        int padding = 0;
        for (int k = 0; k < 4; ++k) {
            const unsigned char ch = static_cast<unsigned char>(text[i + k]);
            const int value = kInverse[ch];
            if (value == -1) return false;
            if (value == -2) {
                // Padding is only legal in the final quantum.
                if (i + 4 != text.size() || k < 2) return false;
                sextets[k] = 0;
                ++padding;
            } else {
                if (padding > 0) return false;
                sextets[k] = value;
            }
        }
        const uint32_t triple = (static_cast<uint32_t>(sextets[0]) << 18) |
                                (static_cast<uint32_t>(sextets[1]) << 12) |
                                (static_cast<uint32_t>(sextets[2]) << 6) |
                                static_cast<uint32_t>(sextets[3]);
        outBytes.push_back(static_cast<char>((triple >> 16) & 0xFF));
        if (padding < 2) outBytes.push_back(static_cast<char>((triple >> 8) & 0xFF));
        if (padding < 1) outBytes.push_back(static_cast<char>(triple & 0xFF));
    }
    return true;
}

std::string iso8601UtcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm fields{};
#if defined(_WIN32)
    gmtime_s(&fields, &now);
#else
    gmtime_r(&now, &fields);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &fields);
    return std::string(buffer);
}

std::string truncateSummary(const std::string& text, uint32_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    uint32_t cut = maxBytes;
    // Step back over UTF-8 continuation bytes so no rune is split.
    while (cut > 0 && (static_cast<uint8_t>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
}

ThumbnailResult encodeThumbnailPng(
    const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t pitchBytes) {
    if (!rgba || width == 0 || height == 0 || pitchBytes < width * 4) {
        return {};
    }
    const uint32_t outWidth = std::min(width, kThumbnailMaxWidth);
    const uint32_t outHeight = std::max<uint32_t>(
        1, static_cast<uint32_t>(
               (static_cast<uint64_t>(height) * outWidth) / width));
    // Box-average downscale straight from the pitched source rows.
    std::vector<uint8_t> small(static_cast<size_t>(outWidth) * outHeight * 4, 0);
    for (uint32_t y = 0; y < outHeight; ++y) {
        const uint32_t srcY0 = (y * height) / outHeight;
        const uint32_t srcY1 = ((y + 1) * height) / outHeight;
        for (uint32_t x = 0; x < outWidth; ++x) {
            const uint32_t srcX0 = (x * width) / outWidth;
            const uint32_t srcX1 = ((x + 1) * width) / outWidth;
            uint32_t sum[4] = {0, 0, 0, 0};
            uint32_t count = 0;
            for (uint32_t sy = srcY0; sy < srcY1; ++sy) {
                const uint8_t* row = rgba + static_cast<size_t>(sy) * pitchBytes;
                for (uint32_t sx = srcX0; sx < srcX1; ++sx) {
                    for (int c = 0; c < 4; ++c) sum[c] += row[sx * 4 + c];
                    ++count;
                }
            }
            uint8_t* dst = &small[(static_cast<size_t>(y) * outWidth + x) * 4];
            for (int c = 0; c < 4; ++c) {
                dst[c] = count ? static_cast<uint8_t>(sum[c] / count) : 0;
            }
        }
    }

    // Scanlines with filter byte 0, then a single stored-block zlib stream.
    std::string raw;
    raw.reserve(static_cast<size_t>(outHeight) * (static_cast<size_t>(outWidth) * 4 + 1));
    for (uint32_t y = 0; y < outHeight; ++y) {
        raw.push_back(0);
        raw.append(
            reinterpret_cast<const char*>(&small[static_cast<size_t>(y) * outWidth * 4]),
            outWidth * 4);
    }
    std::string idat;
    appendStoredZlib(
        idat, reinterpret_cast<const uint8_t*>(raw.data()),
        static_cast<uint32_t>(raw.size()));

    std::string png("\x89PNG\r\n\x1a\n", 8);
    uint8_t ihdr[13];
    ihdr[0] = static_cast<uint8_t>((outWidth >> 24) & 0xFF);
    ihdr[1] = static_cast<uint8_t>((outWidth >> 16) & 0xFF);
    ihdr[2] = static_cast<uint8_t>((outWidth >> 8) & 0xFF);
    ihdr[3] = static_cast<uint8_t>(outWidth & 0xFF);
    ihdr[4] = static_cast<uint8_t>((outHeight >> 24) & 0xFF);
    ihdr[5] = static_cast<uint8_t>((outHeight >> 16) & 0xFF);
    ihdr[6] = static_cast<uint8_t>((outHeight >> 8) & 0xFF);
    ihdr[7] = static_cast<uint8_t>(outHeight & 0xFF);
    ihdr[8] = 8; // bit depth
    ihdr[9] = 6; // color type: RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace
    appendChunk(png, "IHDR", ihdr, sizeof(ihdr));
    appendChunk(
        png, "IDAT", reinterpret_cast<const uint8_t*>(idat.data()),
        static_cast<uint32_t>(idat.size()));
    appendChunk(png, "IEND", nullptr, 0);

    ThumbnailResult result;
    result.png = std::move(png);
    result.width = outWidth;
    result.height = outHeight;
    return result;
}

} // namespace Rowl::State
