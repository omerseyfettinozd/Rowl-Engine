/**
 * rowl/text/utf8.hpp
 *
 * Tek paylasimli strict UTF-8 skaler cozucu (A3-tur4, metin turu).
 *
 * Motordaki uc lax decoder'in (FontRenderer::getNextCodepoint,
 * TextShaper::decodeScalar, MsdfRenderer::measureTextWidth ici dongu)
 * ortak dogruluk noktasidir: continuation/overlong/surrogate/aralik
 * denetimi tek noktada, gecersiz girdi HER YERDE U+FFFD olur (ham lead
 * bayti artik sizamaz).
 *
 * Sozlesme: gecersiz dizide codepoint U+FFFD + length 1 (tuketim 1 bayt,
 * cagiran kaldigi yerden devam eder); sonda kesik dizide kalanin tamami
 * tuketilir (devami gelemez). Gecerli girdide (codepoint, uzunluk) tamdir.
 * markup_parser kendi decoder'inda kalir (tani-ofset/tuketim semantigi
 * testlerle kilitli; davranis degisikligi YOK).
 *
 * Garantiler: throw yok, tahsis yok, locale bagimliligi yok.
 * Yalnizca baslik (header-only); C ABI degisikligi yoktur.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Rowl::Text {

/// U+FFFD: gecersiz/kesik dizilerin yerine konan skaler.
constexpr uint32_t kUtf8ReplacementCodepoint = 0xFFFDu;

struct Utf8Scalar {
    uint32_t codepoint = kUtf8ReplacementCodepoint;
    std::size_t length = 1;  // Tukettigi kaynak bayt sayisi (>= 1).
    bool valid = true;
};

/// [first, last) araliginin basindaki tek UTF-8 dizisini cozer.
inline Utf8Scalar decodeUtf8Scalar(const char* first, const char* last) noexcept {
    Utf8Scalar out;
    if (first == nullptr || first >= last) {
        out.valid = false;
        out.length = 0;
        return out;
    }
    const auto lead = static_cast<uint8_t>(*first);
    if (lead < 0x80u) {
        out.codepoint = lead;
        out.length = 1;
        return out;
    }
    std::size_t want = 0;
    uint32_t min = 0;
    uint32_t value = 0;
    if ((lead & 0xE0u) == 0xC0u) {
        want = 2;
        min = 0x80u;
        value = lead & 0x1Fu;
    } else if ((lead & 0xF0u) == 0xE0u) {
        want = 3;
        min = 0x800u;
        value = lead & 0x0Fu;
    } else if ((lead & 0xF8u) == 0xF0u) {
        want = 4;
        min = 0x10000u;
        value = lead & 0x07u;
    } else {
        // Basibos continuation (0x80..0xBF) veya 0xF8+ oneki: 1 bayt tuket.
        out.valid = false;
        return out;
    }
    const auto available =
        static_cast<std::size_t>(last - first);
    if (available < want) {
        // Sonda kesik: devami gelemez, kalani tek FFFD olarak tuket.
        out.valid = false;
        out.length = available;
        return out;
    }
    for (std::size_t k = 1; k < want; ++k) {
        const auto cont = static_cast<uint8_t>(first[k]);
        if ((cont & 0xC0u) != 0x80u) {
            out.valid = false;
            out.length = 1;
            return out;
        }
        value = (value << 6u) | (cont & 0x3Fu);
    }
    if (value < min || value > 0x10FFFFu ||
        (value >= 0xD800u && value <= 0xDFFFu)) {
        // Overlong / aralik-disi / surrogate: 1 bayt tuket.
        out.valid = false;
        out.length = 1;
        return out;
    }
    out.codepoint = value;
    out.length = want;
    return out;
}

/// Metin-baslangicli kolaylik: bos metin (0, gecersiz) doner.
inline Utf8Scalar decodeUtf8Scalar(std::string_view text) noexcept {
    if (text.empty()) {
        Utf8Scalar out;
        out.valid = false;
        out.length = 0;
        return out;
    }
    return decodeUtf8Scalar(text.data(), text.data() + text.size());
}

}  // namespace Rowl::Text
