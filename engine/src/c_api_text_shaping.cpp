#include "c_api_internal.hpp"
#include "rowl/text/markup_parser.hpp"
#include "rowl/text/text_shaper.hpp"

#include <cmath>
#include <cstring>

namespace {

constexpr std::size_t kMaxFontBytes = 32u * 1024u * 1024u;

bool boundedString(const char* value, std::size_t limit,
                   std::string_view& out) noexcept {
    if (!value) return false;
    const void* terminator = std::memchr(value, '\0', limit + 1u);
    if (!terminator) return false;
    out = {value, static_cast<std::size_t>(
        static_cast<const char*>(terminator) - value)};
    return true;
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_ShapeMarkup(
    const char* markupUtf8, const uint8_t* fontData, uint32_t fontDataSize,
    float fontSize, float maxWidth, const char* languageUtf8,
    char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    if (!outRequiredSize || (!buffer && bufferSize != 0) ||
        !fontData || fontDataSize == 0 || fontDataSize > kMaxFontBytes ||
        !std::isfinite(fontSize) || fontSize < Rowl::Text::kMinFontSize ||
        fontSize > Rowl::Text::kMaxFontSize || !std::isfinite(maxWidth)) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        std::string_view markup;
        if (!boundedString(markupUtf8, Rowl::Text::kMaxMarkupBytes, markup))
            return ROWL_RESULT_INVALID_ARGUMENT;
        std::string_view language;
        if (languageUtf8 && !boundedString(languageUtf8, 128u, language))
            return ROWL_RESULT_INVALID_ARGUMENT;

        Rowl::Text::TextShaper shaper;
        if (Rowl::Text::TextShaper::isAdvancedBackendCompiled() &&
            !shaper.loadFontFromMemory(fontData, fontDataSize))
            return ROWL_RESULT_VALIDATION_ERROR;
        Rowl::Text::ShapeOptions options;
        options.fontSize = fontSize;
        options.maxWidth = maxWidth;
        options.language.assign(language);
        const auto shaped = shaper.shapeMarkup(markup, options);
        return copyUtf8ToCaller(Rowl::Text::shapedTextToJson(shaped), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
