#include "rowl/text/text_shaper.hpp"
#include "rowl/text/utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#if ROWL_TEXT_SHAPING_AVAILABLE
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#include <fribidi.h>
#include <graphemebreak.h>
#include <linebreak.h>
#endif

namespace Rowl::Text {
namespace {

struct Scalar {
    uint32_t codepoint = 0;
    MarkupChar style{};
};

uint32_t decodeScalar(std::string_view text) noexcept {
    // A3-tur4 (metin turu): paylasimli strict decoder. Markup yolu zaten
    // gecerli UTF-8 (FFFD-kodlu) verir; ham-girdi yollari (C API, yedek)
    // artik cop codepoint degil U+FFFD uretir.
    if (text.empty()) return 0xFFFDu;
    return decodeUtf8Scalar(text.data(), text.data() + text.size()).codepoint;
}

bool fallbackExtendsGrapheme(uint32_t cp) noexcept {
    return cp == 0x200Du || (cp >= 0x0300u && cp <= 0x036Fu) ||
           (cp >= 0x1AB0u && cp <= 0x1AFFu) ||
           (cp >= 0x1DC0u && cp <= 0x1DFFu) ||
           (cp >= 0x20D0u && cp <= 0x20FFu) ||
           (cp >= 0xFE00u && cp <= 0xFE0Fu) ||
           (cp >= 0xFE20u && cp <= 0xFE2Fu) ||
           (cp >= 0x1F3FBu && cp <= 0x1F3FFu) ||
           (cp >= 0xE0100u && cp <= 0xE01EFu);
}

struct DisjointSet {
    explicit DisjointSet(std::size_t size) : parent(size) {
        std::iota(parent.begin(), parent.end(), 0u);
    }
    uint32_t find(uint32_t value) {
        if (parent[value] != value) parent[value] = find(parent[value]);
        return parent[value];
    }
    void join(uint32_t left, uint32_t right) {
        left = find(left);
        right = find(right);
        if (left != right) parent[right] = left;
    }
    std::vector<uint32_t> parent;
};

std::vector<Scalar> makeScalars(const MarkupDocument& document) {
    std::vector<Scalar> result;
    result.reserve(document.chars.size());
    for (const auto& ch : document.chars) {
        result.push_back({decodeScalar(ch.text), ch});
    }
    return result;
}

float sanitizedFontSize(float value) noexcept {
    return std::clamp(std::isfinite(value) ? value : 24.0f,
                      kMinFontSize, kMaxFontSize);
}

} // namespace

struct TextShaper::Impl {
    std::vector<uint8_t> fontBytes;
#if ROWL_TEXT_SHAPING_AVAILABLE
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    hb_font_t* font = nullptr;

    Impl() { FT_Init_FreeType(&library); }
    ~Impl() {
        if (font) hb_font_destroy(font);
        if (face) FT_Done_Face(face);
        if (library) FT_Done_FreeType(library);
    }
#else
    Impl() = default;
    ~Impl() = default;
#endif
};

TextShaper::TextShaper() : m_impl(std::make_unique<Impl>()) {}
TextShaper::~TextShaper() = default;
TextShaper::TextShaper(TextShaper&&) noexcept = default;
TextShaper& TextShaper::operator=(TextShaper&&) noexcept = default;

bool TextShaper::isAdvancedBackendCompiled() noexcept {
#if ROWL_TEXT_SHAPING_AVAILABLE
    return true;
#else
    return false;
#endif
}

bool TextShaper::isAdvancedBackendActive() const noexcept {
#if ROWL_TEXT_SHAPING_AVAILABLE
    return m_impl && m_impl->font != nullptr;
#else
    return false;
#endif
}

bool TextShaper::loadFontFromMemory(const uint8_t* data,
                                    std::size_t size) noexcept {
    if (!m_impl || !data || size == 0) return false;
#if ROWL_TEXT_SHAPING_AVAILABLE
    try {
        // #57/#41-kalinti (stage-then-commit): yeni yuz once yerel baytta
        // denenir; canli font/face yalniz yeni yuz gecerliyken sokulur.
        // Eski kod canliyi once yikip sonra deniyordu — basarisiz reload
        // onceki iyi backend'i dusuruyordu.
        if (size > static_cast<std::size_t>(
                std::numeric_limits<FT_Long>::max())) {
            return false;
        }
        std::vector<uint8_t> staged(data, data + size);
        FT_Face stagedFace = nullptr;
        if (!m_impl->library || FT_New_Memory_Face(
                m_impl->library, staged.data(),
                static_cast<FT_Long>(staged.size()), 0,
                &stagedFace) != 0) {
            return false;
        }
        hb_font_t* stagedFont = hb_ft_font_create_referenced(stagedFace);
        if (!stagedFont) {
            FT_Done_Face(stagedFace);
            return false;
        }
        // Commit: vektor move tampon sahipligini devreder (data pointer
        // korunur), yuz yeni sahibin baytini gostermeye devam eder.
        if (m_impl->font) {
            hb_font_destroy(m_impl->font);
            m_impl->font = nullptr;
        }
        if (m_impl->face) {
            FT_Done_Face(m_impl->face);
            m_impl->face = nullptr;
        }
        m_impl->fontBytes = std::move(staged);
        m_impl->face = stagedFace;
        m_impl->font = stagedFont;
        return true;
    } catch (...) {
        return false;
    }
#else
    (void)data;
    (void)size;
    return false;
#endif
}

#if ROWL_TEXT_SHAPING_AVAILABLE
namespace {

void ensureUnibreakInitialized() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        init_graphemebreak();
        init_linebreak();
    });
}

/// Reports whether a BCP 47 tag wants a right-to-left paragraph base
/// direction (#95). Script subtags win when present (Arab/Hebr/Thaa/Syrc…);
/// otherwise the primary-language RTL set decides. Empty/unknown tags
/// return false so those paragraphs keep the legacy FRIBIDI_PAR_ON path.
bool isRtlLanguageTag(std::string_view tag) {
    if (tag.empty()) return false;
    std::string lower(tag);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    std::replace(lower.begin(), lower.end(), '_', '-');
    if (lower.find("arab") != std::string::npos ||
        lower.find("hebr") != std::string::npos ||
        lower.find("thaa") != std::string::npos ||
        lower.find("syrc") != std::string::npos ||
        lower.find("samr") != std::string::npos ||
        lower.find("mand") != std::string::npos ||
        lower.find("nkoo") != std::string::npos) {
        return true;
    }
    const std::string primary =
        lower.substr(0, lower.find('-') == std::string::npos
                            ? lower.size()
                            : lower.find('-'));
    static const char* const kRtlLanguages[] = {
        "ar", "fa", "ur", "he", "yi", "ps", "sd", "ug",
        "dv", "ckb", "arc",
    };
    // NOTE: Hausa ("ha") is Latin-script by default, so bare "ha" stays
    // LTR here; Ajami Hausa must tag the script ("ha-Arab") and is then
    // caught by the script check above.
    return std::find(std::begin(kRtlLanguages), std::end(kRtlLanguages),
                     primary) != std::end(kRtlLanguages);
}

struct RawGlyph {
    uint32_t glyph = 0;
    uint32_t scalar = 0;
    float advance = 0.0f;
    float xOffset = 0.0f;
    float yOffset = 0.0f;
};

std::vector<RawGlyph> shapeVisualLine(hb_font_t* font,
                                      const std::vector<Scalar>& scalars,
                                      uint32_t begin, uint32_t end,
                                      float pixelSize,
                                      std::string_view language) {
    std::vector<RawGlyph> result;
    if (!font || begin >= end) return result;

    std::vector<FriBidiChar> codepoints(end - begin);
    for (uint32_t i = begin; i < end; ++i)
        codepoints[i - begin] = static_cast<FriBidiChar>(scalars[i].codepoint);
    // #95: the old unconditional PAR_ON let FriBidi infer the base
    // direction from content, so a leading number/punctuation could pin an
    // Arabic/Hebrew paragraph LTR. An explicit RTL tag now anchors the
    // base direction; anything else keeps the legacy ON path.
    FriBidiParType baseDirection =
        isRtlLanguageTag(language) ? FRIBIDI_PAR_RTL : FRIBIDI_PAR_ON;
    std::vector<FriBidiStrIndex> logicalToVisual(codepoints.size());
    std::vector<FriBidiLevel> levels(codepoints.size());
    if (!fribidi_log2vis(codepoints.data(), static_cast<FriBidiStrIndex>(codepoints.size()),
                         &baseDirection, nullptr, logicalToVisual.data(), nullptr,
                         levels.data())) {
        std::fill(levels.begin(), levels.end(), 0);
        std::iota(logicalToVisual.begin(), logicalToVisual.end(), 0);
    }

    struct Run { uint32_t begin; uint32_t end; uint32_t visual; bool rtl; };
    std::vector<Run> runs;
    for (uint32_t cursor = begin; cursor < end;) {
        const bool rtl = (levels[cursor - begin] & 1u) != 0;
        uint32_t runEnd = cursor + 1;
        while (runEnd < end &&
               levels[runEnd - begin] == levels[cursor - begin]) ++runEnd;
        uint32_t visual = std::numeric_limits<uint32_t>::max();
        for (uint32_t i = cursor; i < runEnd; ++i)
            visual = std::min(visual, static_cast<uint32_t>(logicalToVisual[i - begin]));
        runs.push_back({cursor, runEnd, visual, rtl});
        cursor = runEnd;
    }
    std::sort(runs.begin(), runs.end(), [](const Run& a, const Run& b) {
        return a.visual < b.visual;
    });

    const int scale = std::max(1, static_cast<int>(std::lround(pixelSize * 64.0f)));
    hb_font_set_scale(font, scale, scale);
    std::vector<hb_codepoint_t> all(scalars.size());
    for (std::size_t i = 0; i < scalars.size(); ++i) all[i] = scalars[i].codepoint;

    for (const Run& run : runs) {
        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
        hb_buffer_add_codepoints(buffer, all.data(), static_cast<int>(all.size()),
                                 run.begin, run.end - run.begin);
        hb_buffer_set_direction(buffer, run.rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        if (!language.empty())
            hb_buffer_set_language(buffer,
                hb_language_from_string(language.data(), static_cast<int>(language.size())));
        hb_buffer_guess_segment_properties(buffer);
        hb_shape(font, buffer, nullptr, 0);
        unsigned count = 0;
        const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
        for (unsigned i = 0; i < count; ++i) {
            result.push_back({infos[i].codepoint, infos[i].cluster,
                              positions[i].x_advance / 64.0f,
                              positions[i].x_offset / 64.0f,
                              positions[i].y_offset / 64.0f});
        }
        hb_buffer_destroy(buffer);
    }
    return result;
}

} // namespace
#endif

ShapedText TextShaper::shapeMarkup(std::string_view markup,
                                   const ShapeOptions& requested) const noexcept {
    ShapedText out;
    try {
        const MarkupDocument document = parseMarkup(markup);
        out.plainText = document.plainText;
        out.diagnostics = document.diagnostics;
        out.trailingPause = document.trailingPause;
        const std::vector<Scalar> scalars = makeScalars(document);
        if (scalars.empty()) return out;

        const float fontSize = sanitizedFontSize(requested.fontSize);
        const float lineHeight = fontSize * std::max(0.5f, requested.lineHeightMultiplier);
        DisjointSet groups(scalars.size());

#if ROWL_TEXT_SHAPING_AVAILABLE
        if (isAdvancedBackendActive()) {
            ensureUnibreakInitialized();
            // Pin the FreeType pixel size before any hb_shape call. hb-ft
            // resolves advances through FT_Get_Advance without NO_SCALE, so
            // with no size set the advances fold to zero on HarfBuzz <= 8.x
            // (CI: 8.3.0; newer HarfBuzz is size-independent, unaffected).
            // hb_ft_font_changed clears the glyph-keyed advance cache, which
            // would otherwise serve the previous call's pixel size.
            if (m_impl->face != nullptr) {
                const auto pixels = static_cast<FT_UInt>(
                    std::max(1.0f, std::round(fontSize)));
                if (FT_Set_Pixel_Sizes(m_impl->face, 0, pixels) == 0)
                    hb_ft_font_changed(m_impl->font);
            }
            std::vector<FriBidiChar> cps(scalars.size());
            for (std::size_t i = 0; i < scalars.size(); ++i) cps[i] = scalars[i].codepoint;
            std::vector<char> graphemeBreaks(scalars.size(), GRAPHEMEBREAK_BREAK);
            set_graphemebreaks_utf32(cps.data(), cps.size(),
                                     requested.language.empty() ? nullptr : requested.language.c_str(),
                                     graphemeBreaks.data());
            for (uint32_t i = 1; i < scalars.size(); ++i) {
                // libunibreak annotates the boundary after each input scalar.
                if (graphemeBreaks[i - 1] != GRAPHEMEBREAK_BREAK &&
                    scalars[i - 1].codepoint != '\n') groups.join(i - 1, i);
            }

            // HarfBuzz clusters can be wider than a grapheme (for example an
            // fi ligature). Merge their logical scalar spans into one reveal.
            for (uint32_t begin = 0; begin < scalars.size();) {
                uint32_t end = begin;
                while (end < scalars.size() && scalars[end].codepoint != '\n') ++end;
                const auto probe = shapeVisualLine(m_impl->font, scalars, begin, end,
                                                   fontSize, requested.language);
                std::vector<uint32_t> starts;
                for (const auto& glyph : probe)
                    if (glyph.scalar >= begin && glyph.scalar < end) starts.push_back(glyph.scalar);
                starts.push_back(end);
                std::sort(starts.begin(), starts.end());
                starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
                for (std::size_t i = 1; i < starts.size(); ++i)
                    for (uint32_t s = starts[i - 1] + 1; s < starts[i]; ++s)
                        groups.join(starts[i - 1], s);
                begin = end < scalars.size() ? end + 1 : end;
            }
            out.backend = ShapingBackend::HarfBuzzFriBidi;
        } else
#endif
        {
            for (uint32_t i = 1; i < scalars.size(); ++i) {
                if ((fallbackExtendsGrapheme(scalars[i].codepoint) ||
                     scalars[i - 1].codepoint == 0x200Du) &&
                    scalars[i - 1].codepoint != '\n') groups.join(i - 1, i);
            }
        }

        std::vector<uint32_t> scalarReveal(scalars.size());
        std::unordered_map<uint32_t, uint32_t> rootToReveal;
        for (uint32_t i = 0; i < scalars.size(); ++i) {
            const uint32_t root = groups.find(i);
            auto [it, inserted] = rootToReveal.emplace(root,
                static_cast<uint32_t>(out.revealUnits.size()));
            if (inserted) {
                RevealUnit unit;
                unit.firstScalar = i;
                unit.scalarCount = 1;
                unit.speed = std::max(kMinSpeedMultiplier, scalars[i].style.speed);
                unit.representativeCodepoint = scalars[i].codepoint;
                out.revealUnits.push_back(unit);
            } else {
                ++out.revealUnits[it->second].scalarCount;
            }
            out.revealUnits[it->second].pauseBefore += scalars[i].style.pauseBefore;
            scalarReveal[i] = it->second;
        }

#if ROWL_TEXT_SHAPING_AVAILABLE
        if (out.backend == ShapingBackend::HarfBuzzFriBidi) {
            std::vector<FriBidiChar> cps(scalars.size());
            for (std::size_t i = 0; i < scalars.size(); ++i) cps[i] = scalars[i].codepoint;
            std::vector<char> breaks(scalars.size(), LINEBREAK_NOBREAK);
            set_linebreaks_utf32(cps.data(), cps.size(),
                                 requested.language.empty() ? nullptr : requested.language.c_str(),
                                 breaks.data());

            uint32_t paragraphBegin = 0;
            uint32_t lineIndex = 0;
            while (paragraphBegin <= scalars.size()) {
                uint32_t paragraphEnd = paragraphBegin;
                while (paragraphEnd < scalars.size() &&
                       scalars[paragraphEnd].codepoint != '\n') ++paragraphEnd;

                std::vector<std::pair<uint32_t, uint32_t>> lineRanges;
                if (paragraphBegin == paragraphEnd) {
                    lineRanges.emplace_back(paragraphBegin, paragraphEnd);
                } else if (!(requested.maxWidth > 0.0f)) {
                    lineRanges.emplace_back(paragraphBegin, paragraphEnd);
                } else {
                    const auto probe = shapeVisualLine(m_impl->font, scalars,
                        paragraphBegin, paragraphEnd, fontSize, requested.language);
                    std::vector<float> scalarWidths(scalars.size(), 0.0f);
                    for (const auto& glyph : probe)
                        if (glyph.scalar < scalarWidths.size())
                            scalarWidths[glyph.scalar] += std::abs(glyph.advance);
                    uint32_t lineBegin = paragraphBegin;
                    while (lineBegin < paragraphEnd) {
                        float width = 0.0f;
                        uint32_t lastAllowed = lineBegin;
                        uint32_t cursor = lineBegin;
                        for (; cursor < paragraphEnd; ++cursor) {
                            width += scalarWidths[cursor];
                            if (breaks[cursor] == LINEBREAK_ALLOWBREAK ||
                                breaks[cursor] == LINEBREAK_MUSTBREAK)
                                lastAllowed = cursor + 1;
                            if (width > requested.maxWidth && cursor > lineBegin) break;
                        }
                        uint32_t lineEnd = paragraphEnd;
                        if (cursor < paragraphEnd) {
                            lineEnd = lastAllowed > lineBegin ? lastAllowed : cursor;
                            while (lineEnd < paragraphEnd && lineEnd > lineBegin &&
                                   groups.find(lineEnd - 1) == groups.find(lineEnd)) --lineEnd;
                            if (lineEnd == lineBegin) lineEnd = cursor + 1;
                        }
                        lineRanges.emplace_back(lineBegin, lineEnd);
                        lineBegin = lineEnd;
                    }
                }

                for (const auto [lineBegin, lineEnd] : lineRanges) {
                    ShapedLine line;
                    line.firstGlyph = static_cast<uint32_t>(out.glyphs.size());
                    line.firstScalar = lineBegin;
                    line.scalarCount = lineEnd - lineBegin;
                    line.baseline = (lineIndex + 1) * lineHeight - fontSize * 0.15f;
                    const auto raw = shapeVisualLine(m_impl->font, scalars, lineBegin,
                                                     lineEnd, fontSize, requested.language);
                    float penX = 0.0f;
                    for (const auto& source : raw) {
                        const uint32_t scalar = std::min<uint32_t>(source.scalar,
                            static_cast<uint32_t>(scalars.size() - 1));
                        const MarkupChar& style = scalars[groups.find(scalar)].style;
                        const float styleScale = style.hasSize
                            ? sanitizedFontSize(style.size) / fontSize : 1.0f;
                        ShapedGlyph glyph;
                        glyph.glyphIndex = source.glyph;
                        glyph.logicalScalar = scalar;
                        glyph.revealIndex = scalarReveal[scalar];
                        glyph.lineIndex = lineIndex;
                        glyph.x = penX;
                        glyph.y = line.baseline;
                        glyph.xAdvance = source.advance * styleScale;
                        glyph.xOffset = source.xOffset * styleScale;
                        glyph.yOffset = source.yOffset * styleScale;
                        glyph.style = style;
                        out.glyphs.push_back(std::move(glyph));
                        penX += source.advance * styleScale;
                    }
                    line.glyphCount = static_cast<uint32_t>(out.glyphs.size()) - line.firstGlyph;
                    line.width = std::abs(penX);
                    out.width = std::max(out.width, line.width);
                    out.lines.push_back(line);
                    ++lineIndex;
                }
                if (paragraphEnd == scalars.size()) break;
                paragraphBegin = paragraphEnd + 1;
            }
            out.height = out.lines.size() * lineHeight;
            return out;
        }
#endif

        // Minimal layout metadata. FontRenderer keeps stb metrics and
        // rasterization authoritative when the advanced backend is inactive.
        ShapedLine line;
        line.baseline = lineHeight - fontSize * 0.15f;
        out.lines.push_back(line);
        out.height = lineHeight;
        return out;
    } catch (...) {
        return out;
    }
}

bool TextShaper::rasterizeGlyph(uint32_t glyphIndex, float pixelSize,
                                RasterizedShapedGlyph& out) const noexcept {
    out = {};
#if ROWL_TEXT_SHAPING_AVAILABLE
    if (!isAdvancedBackendActive()) return false;
    const auto size = static_cast<FT_UInt>(std::max(1.0f, std::round(pixelSize)));
    if (FT_Set_Pixel_Sizes(m_impl->face, 0, size) != 0 ||
        FT_Load_Glyph(m_impl->face, glyphIndex, FT_LOAD_DEFAULT) != 0 ||
        FT_Render_Glyph(m_impl->face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        return false;
    const FT_Bitmap& bitmap = m_impl->face->glyph->bitmap;
    out.width = static_cast<int>(bitmap.width);
    out.height = static_cast<int>(bitmap.rows);
    out.bearingX = m_impl->face->glyph->bitmap_left;
    out.bearingY = m_impl->face->glyph->bitmap_top;
    out.bitmap.resize(static_cast<std::size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; ++y) {
        const int row = bitmap.pitch >= 0 ? y : out.height - 1 - y;
        const uint8_t* source = bitmap.buffer + row * std::abs(bitmap.pitch);
        std::copy_n(source, out.width, out.bitmap.data() + y * out.width);
    }
    return true;
#else
    (void)glyphIndex;
    (void)pixelSize;
    return false;
#endif
}

RevealState evaluateReveal(const ShapedText& shaped, double elapsedSeconds,
                           double baseMillisecondsPerUnit) noexcept {
    RevealState state;
    const double baseSeconds = std::max(0.0, baseMillisecondsPerUnit) / 1000.0;
    double cursor = 0.0;
    const double elapsed = std::max(0.0, elapsedSeconds);
    for (const auto& unit : shaped.revealUnits) {
        cursor += std::max(0.0f, unit.pauseBefore);
        cursor += baseSeconds / std::max<double>(kMinSpeedMultiplier, unit.speed);
        if (elapsed + 1e-9 < cursor) break;
        ++state.visibleUnits;
    }
    cursor += std::max(0.0, shaped.trailingPause);
    state.totalSeconds = cursor;
    state.complete = state.visibleUnits == shaped.revealUnits.size() &&
                     elapsed + 1e-9 >= cursor;
    return state;
}

std::string shapedTextToJson(const ShapedText& shaped) noexcept {
    try {
        nlohmann::json root;
        root["backend"] = shaped.backend == ShapingBackend::HarfBuzzFriBidi
            ? "harfbuzz_freetype_fribidi_unibreak" : "stb_fallback";
        root["plain_text"] = shaped.plainText;
        root["width"] = shaped.width;
        root["height"] = shaped.height;
        root["reveal_count"] = shaped.revealUnits.size();
        root["trailing_pause"] = shaped.trailingPause;
        root["glyphs"] = nlohmann::json::array();
        for (const auto& glyph : shaped.glyphs) {
            root["glyphs"].push_back({
                {"glyph_index", glyph.glyphIndex},
                {"logical_scalar", glyph.logicalScalar},
                {"reveal_index", glyph.revealIndex},
                {"line_index", glyph.lineIndex},
                {"x", glyph.x}, {"y", glyph.y},
                {"x_advance", glyph.xAdvance}, {"y_advance", glyph.yAdvance},
                {"x_offset", glyph.xOffset}, {"y_offset", glyph.yOffset}});
        }
        root["lines"] = nlohmann::json::array();
        for (const auto& line : shaped.lines) {
            root["lines"].push_back({{"first_glyph", line.firstGlyph},
                {"glyph_count", line.glyphCount},
                {"first_scalar", line.firstScalar},
                {"scalar_count", line.scalarCount}, {"width", line.width},
                {"baseline", line.baseline}});
        }
        root["reveal_units"] = nlohmann::json::array();
        for (const auto& unit : shaped.revealUnits) {
            root["reveal_units"].push_back({{"first_scalar", unit.firstScalar},
                {"scalar_count", unit.scalarCount},
                {"pause_before", unit.pauseBefore}, {"speed", unit.speed},
                {"representative_codepoint", unit.representativeCodepoint}});
        }
        root["diagnostics"] = nlohmann::json::array();
        for (const auto& diagnostic : shaped.diagnostics) {
            root["diagnostics"].push_back({{"message", diagnostic.message},
                {"offset", diagnostic.sourceOffset},
                {"length", diagnostic.sourceLength}});
        }
        return root.dump();
    } catch (...) {
        return "{}";
    }
}

} // namespace Rowl::Text
