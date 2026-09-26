/**
 * c_api_e2a_repertoire.cpp
 *
 * E2a — gecis-repertuvari (dissolve/push/iris) kesfi + sprite-sheet
 * gozlemlenebilirligi (ROWL_ENGINE_CAPABILITY_TRANSITION_REPERTOIRE = 524288,
 * ROWL_ENGINE_CAPABILITY_SPRITE_SHEET = 1048576). Yalniz-eklemeli; eski giris
 * noktalarina dokunulmaz. engine.cpp / window.cpp buyumez: tur listesi
 * TransitionManager::supportedKindsJson'dadir, sheet durumu
 * Engine::spriteSheetStateJson'dadir; burasi yalnizca ABI siniridir (handle
 * dogrulama + boyut-sorgu/cagiran-tamponu + istisna yutma).
 *
 * Fail-closed: handle-free liste saf yardimcidir (ParseMarkup emsali);
 * state sorgusu olu-handle'da INVALID_HANDLE, init-siz handle'da StateError
 * damgalar + "[]" kopyalar (D1/B1b paritesi).
 */

#include "c_api_internal.hpp"
#include "rowl/render/transition_manager.hpp"

extern "C" {

RowlEngine_ResultCode RowlEngine_GetSupportedTransitionKindsJson(
    char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        return copyUtf8ToCaller(
            Rowl::Render::TransitionManager::supportedKindsJson(),
            buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetSpriteSheetStateJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return ROWL_RESULT_INVALID_HANDLE;
        if (!requireEngineInitialized(checked, "get_sprite_sheet_state")) {
            return copyUtf8ToCaller("[]", buffer, bufferSize, outRequiredSize);
        }
        return copyUtf8ToCaller(checked->spriteSheetStateJson(),
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
