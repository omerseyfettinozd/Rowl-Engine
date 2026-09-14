/**
 * c_api_markup.cpp
 *
 * Faz 3 Dilim 2 — zengin metin C-API yuzeyi: handle gerektirmeyen saf
 * markup yardimcilari (ParseMarkup / StripMarkup). Eklemeli; eski giris
 * noktalarina dokunulmaz. engine.cpp / window.cpp buyumez: tum kurallar
 * Rowl::Text::markup_parser'dadir, burasi yalnizca ABI siniridir
 * (null/boyut sozlesmesi + istisna yutma).
 */

#include "c_api_internal.hpp"
#include "rowl/text/markup_parser.hpp"

#include <cstring>

namespace {

/// C API giris siniri (byte). Uzeri INVALID_ARGUMENT ile reddedilir; cozum
/// state'i kirlenmez, cikti tamponuna yazilmaz.
constexpr std::size_t kMarkupInputLimitBytes = Rowl::Text::kMaxMarkupBytes;

/// markupUtf8 gecerlemesi: null -> INVALID_ARGUMENT; asiri buyuk ->
/// INVALID_ARGUMENT. Gecerliyse gorunum uretir.
RowlEngine_ResultCode checkMarkupInput(const char* markupUtf8,
                                       std::string_view& out) noexcept {
    if (markupUtf8 == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    // NUL aramasinin kendisi de tasiyici siniriyla bounded kalir. Ilk
    // (limit + 1) baytta NUL yoksa girdi ya fazla buyuktur ya da gecersizdir.
    const void* terminator =
        std::memchr(markupUtf8, '\0', kMarkupInputLimitBytes + 1u);
    if (terminator == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    const std::size_t length =
        static_cast<const char*>(terminator) - markupUtf8;
    out = std::string_view(markupUtf8, length);
    return ROWL_RESULT_OK;
}

}  // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_ParseMarkup(const char* markupUtf8,
                                             char* buffer, uint32_t bufferSize,
                                             uint32_t* outRequiredSize) {
    if (outRequiredSize == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    if (buffer == nullptr && bufferSize != 0) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        std::string_view markup;
        const RowlEngine_ResultCode inputCheck =
            checkMarkupInput(markupUtf8, markup);
        if (inputCheck != ROWL_RESULT_OK) return inputCheck;
        const Rowl::Text::MarkupDocument document =
            Rowl::Text::parseMarkup(markup);
        return copyUtf8ToCaller(Rowl::Text::markupDocumentToJson(document),
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_StripMarkup(const char* markupUtf8,
                                             char* buffer, uint32_t bufferSize,
                                             uint32_t* outRequiredSize) {
    if (outRequiredSize == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    if (buffer == nullptr && bufferSize != 0) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        std::string_view markup;
        const RowlEngine_ResultCode inputCheck =
            checkMarkupInput(markupUtf8, markup);
        if (inputCheck != ROWL_RESULT_OK) return inputCheck;
        return copyUtf8ToCaller(Rowl::Text::stripMarkup(markup), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

}  // extern "C"
