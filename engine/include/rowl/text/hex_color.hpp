/**
 * rowl/text/hex_color.hpp
 *
 * Faz 4.5 Dilim 2 — birlesik (superset) hex renk cozucu.
 *
 * Motordaki tum hex renk cozumleme noktalarinin tek kaynagidir:
 *  - render/window.cpp (dialog/box/speaker/vignette/flash/tint renkleri)
 *  - render/transition_manager.cpp (fade_color gecis rengi)
 *  - text/markup_parser.cpp (<color> etiketi; sozlesme alt kumesini korur)
 *
 * Kapsanan birlesim: istege bagli tek '#' + {3, 4, 6, 8} hex basamak
 * (#RGB, #RGBA, #RRGGBB, #RRGGBBAA; '#'siz bicimler de kabul edilir).
 * Bos, kesik (#12, #12345, #1234567), basamak-disi (#GGG, #FF00GG),
 * cift-kare (##FFF) ve gomulu bosluklu girdiler REDDEDILIR: try raporu
 * false doner, sarmalayi (wrapper) ACIKCA verilen yedegi dondurur ve
 * basarisizligi bildirir. Sessiz cop renk uretilmez (eski
 * stoul/strtoul one-kisim cozumlemelerinin aksine).
 *
 * Garantiler: throw yok, tahsis (allocation) yok, locale bagimliligi yok,
 * basarisizlikta `out` degistirilmez. Yalnizca baslik (header-only);
 * C ABI degisikligi yoktur.
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace Rowl::Text {

/// Sarmalayici giris/cikisi: yedek renk her cagrida acikca verilir,
/// varsayilan yedek yoktur.
struct HexColor {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;

    bool operator==(const HexColor&) const = default;
};

namespace HexDetail {

constexpr bool isTrimSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

constexpr int hexValue(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && isTrimSpace(value.front())) {
        value.remove_prefix(1);
    }
    while (!value.empty() && isTrimSpace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

}  // namespace HexDetail

/**
 * Birlesim cozumleyici: basarida true + doldurulmus `out`
 * (alfa basamagi yoksa a=255); basarisizlikta false ve `out`
 * DEGISTIRILMEZ.
 */
inline bool tryParseHexColor(std::string_view input, HexColor& out) noexcept {
    std::string_view text = HexDetail::trim(input);
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    const std::size_t count = text.size();
    if (count != 3u && count != 4u && count != 6u && count != 8u) {
        return false;
    }
    for (char c : text) {
        if (HexDetail::hexValue(c) < 0) {
            return false;
        }
    }
    HexColor parsed;
    if (count == 3u || count == 4u) {
        // Kisa bicim: her kanal tek basamagin ciftlenmisidir (0xF -> 0xFF).
        const auto expand = [](char c) noexcept -> uint8_t {
            const auto nibble =
                static_cast<uint8_t>(HexDetail::hexValue(c));
            return static_cast<uint8_t>(nibble * 17u);
        };
        parsed.r = expand(text[0]);
        parsed.g = expand(text[1]);
        parsed.b = expand(text[2]);
        parsed.a = (count == 4u) ? expand(text[3]) : 255;
    } else {
        // Uzun bicim: kanal basina iki basamak.
        const auto byteAt = [](char hi, char lo) noexcept -> uint8_t {
            const auto hiVal =
                static_cast<uint8_t>(HexDetail::hexValue(hi));
            const auto loVal =
                static_cast<uint8_t>(HexDetail::hexValue(lo));
            return static_cast<uint8_t>(
                static_cast<uint8_t>(hiVal << 4) | loVal);
        };
        parsed.r = byteAt(text[0], text[1]);
        parsed.g = byteAt(text[2], text[3]);
        parsed.b = byteAt(text[4], text[5]);
        parsed.a = (count == 8u) ? byteAt(text[6], text[7]) : 255;
    }
    out = parsed;
    return true;
}

/// Bayt-referansli ikiz: gecis yoneticisi bicimindeki cagri noktalari icin.
inline bool tryParseHexColor(std::string_view input, uint8_t& r, uint8_t& g,
                             uint8_t& b, uint8_t& a) noexcept {
    HexColor parsed;
    if (!tryParseHexColor(input, parsed)) {
        return false;
    }
    r = parsed.r;
    g = parsed.g;
    b = parsed.b;
    a = parsed.a;
    return true;
}

/**
 * Acik-yedekli sarmalayici: gecerli girdide cozumlenmis renk (ok=true),
 * bozuk girdide `fallback` (ok=false) doner. `ok` bos birakilabilir ama
 * basarisizlik her zaman gozlenebilirdir.
 */
inline HexColor parseHexColor(std::string_view input, HexColor fallback,
                              bool* ok = nullptr) noexcept {
    HexColor parsed;
    if (tryParseHexColor(input, parsed)) {
        if (ok != nullptr) {
            *ok = true;
        }
        return parsed;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return fallback;
}

}  // namespace Rowl::Text
