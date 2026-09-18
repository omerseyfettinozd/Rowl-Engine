/**
 * c_api_provenance.cpp
 *
 * Faz 5 Dilim 5 — converter provenance C API yüzeyi
 * (ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE = 131072). Eklemeli; eski
 * giriş noktalarına dokunulmaz. engine.cpp / window.cpp büyümez: burası
 * yalnızca ABI sınırıdır (handle doğrulama + giriş taşıyıcısı +
 * boyut-sorgu/çağıran-tamponu + istisna yutma).
 *
 * Durumsuzdur: sidecar baytları aktif VFS'ten okunur
 * (`<asset>.rowlconv.json`), doğrulanır (JSON + zorunlu anahtarlar) ve
 * ham haliyle çağırana kopyalanır. Kayıt/bellek durumu tutulmaz.
 *
 * Fail-closed: null-handle -> INVALID_HANDLE; sidecar yokluğu normal durum
 * (dönüştürücü-üretimi-olmayan asset) -> FILE_NOT_FOUND; bozuk sidecar ->
 * PARSE_ERROR; aşırı büyük giriş -> INVALID_ARGUMENT.
 */

#include "c_api_internal.hpp"
#include "rowl/vfs/vfs.hpp"

#include <cstring>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

/// C API giriş taşıyıcısı (byte), Dilim 1-4 deseni: 256 KiB + 1 cap.
constexpr std::size_t kProvenanceInputLimitBytes = 262144;
/// Sidecar üst sınırı: araç çıktısı birkaç yüz bayttır; 64 KiB fazlasıyla yeter.
constexpr std::size_t kProvenanceSidecarLimitBytes = 65536;

RowlEngine_ResultCode checkSizedInput(const char* input, std::string_view& out) noexcept {
    if (input == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    const void* terminator = std::memchr(input, '\0', kProvenanceInputLimitBytes + 1u);
    if (terminator == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    out = std::string_view(input, static_cast<const char*>(terminator) - input);
    return ROWL_RESULT_OK;
}

bool sidecarSchemaOk(const std::string& text) {
    try {
        const auto json = nlohmann::json::parse(text);
        if (!json.is_object()) return false;
        for (const char* key : {"source_sha256", "converter_name", "converter_version",
                                "settings", "output_sha256", "created_by"}) {
            if (!json.contains(key)) return false;
        }
        if (!json["settings"].is_object()) return false;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_GetAssetProvenanceJson(RowlEngineHandle handle,
                                                       const char* assetPathUtf8, char* buffer,
                                                       uint32_t bufferSize,
                                                       uint32_t* outRequiredSize) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu).
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu).
    if (!toEngineChecked(handle)) return ROWL_RESULT_INVALID_HANDLE;
    std::string_view pathView;
    if (checkSizedInput(assetPathUtf8, pathView) != ROWL_RESULT_OK) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    if (pathView.empty()) return ROWL_RESULT_INVALID_ARGUMENT;
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        auto* vfs = engine->getVfs();
        if (!vfs) return ROWL_RESULT_UNKNOWN_ERROR;
        const std::string sidecarPath = std::string(pathView) + ".rowlconv.json";
        if (!vfs->exists(sidecarPath)) return ROWL_RESULT_FILE_NOT_FOUND;
        const std::vector<uint8_t> bytes = vfs->readBytes(sidecarPath);
        if (bytes.empty() || bytes.size() > kProvenanceSidecarLimitBytes) {
            return ROWL_RESULT_PARSE_ERROR;
        }
        const std::string text(bytes.begin(), bytes.end());
        if (!sidecarSchemaOk(text)) return ROWL_RESULT_PARSE_ERROR;
        return copyUtf8ToCaller(text, buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
