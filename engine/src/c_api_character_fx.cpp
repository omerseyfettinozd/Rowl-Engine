/**
 * c_api_character_fx.cpp
 *
 * E2b — karakter-tween + konusan-vurgusu + dudak-senkronu + expression-harmani
 * C API yuzeyi (ROWL_ENGINE_CAPABILITY_CHARACTER_FX = 524288). Eklemeli; eski
 * giris noktalarina, window.cpp'ye ve editor'e (C3) dokunulmaz; C0 kilit
 * dosyalari (DialogService/LivePreview karakterizasyonu) disiplinin disinda
 * birakilir.
 *
 * Durum Engine'in kendisindedir (saf uye; aux-map ikizi YOK — D13/D3 aux
 * kilitlerine girilmez). Guard disiplini D3 emsali: olu -> INVALID_HANDLE,
 * yabanci-thread -> WRONG_THREAD damgasi + red, sahipsiz/canli-mine -> devam.
 * Tum girisler noexcept sinirinda; istisna yutulur (UNKNOWN_ERROR).
 */

#include "c_api_internal.hpp"

#include <string>

namespace {

// D3 (#150/#157) emsali — aux-map'siz surum: erase yok (Engine-durumu
// lifecycle TU'suna aittir), yalnizca siniflandirma + damga.
inline bool guardFxRead(RowlEngineHandle handle, const char* op,
                        RowlEngine_ResultCode& out) noexcept {
    switch (classifyHandle(handle)) {
        case HandleStanding::Dead:
            out = ROWL_RESULT_INVALID_HANDLE;
            return false;
        case HandleStanding::Foreign:
            stampWrongThread(handle, op);
            out = ROWL_RESULT_WRONG_THREAD;
            return false;
        case HandleStanding::Mine:
            return true;
    }
    out = ROWL_RESULT_UNKNOWN_ERROR;
    return false;
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_CharacterTweenTo(
    RowlEngineHandle handle, int characterIndex,
    float x, float y, float w, float h, float opacity,
    float durationSeconds, int easing) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "character_tween_to", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        std::string error;
        if (!engine->startCharacterTween(characterIndex, x, y, w, h, opacity,
                                         durationSeconds, easing, error)) {
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_CancelCharacterTween(
    RowlEngineHandle handle, int characterIndex) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "cancel_character_tween", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        std::string error;
        if (!engine->cancelCharacterTween(characterIndex, error)) {
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_IsCharacterTweenActive(RowlEngineHandle handle) {
    // int-tasiyici emsali (IsCharacterSlotVisible): olu/yabanci 0 dondurur,
    // yabanci damgayla reddedilir.
    if (classifyHandle(handle) == HandleStanding::Dead) return 0;
    if (!isLiveHandle(handle)) {
        stampWrongThread(handle, "is_character_tween_active");
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto engine = toEngineChecked(handle);
        return (engine && engine->isCharacterTweenActive()) ? 1 : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_SetSpeakerFocus(
    RowlEngineHandle handle, int focusedIndex, float dimOpacity) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "set_speaker_focus", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        std::string error;
        if (!engine->setSpeakerFocus(focusedIndex, dimOpacity, error)) {
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetSpeakerFocus(
    RowlEngineHandle handle, int* outFocusedIndex, float* outDimOpacity) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "get_speaker_focus", guard)) return guard;
    if (outFocusedIndex == nullptr || outDimOpacity == nullptr) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        int index = -1;
        float dim = 1.0f;
        engine->getSpeakerFocus(index, dim);
        *outFocusedIndex = index;
        *outDimOpacity = dim;
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_SetLipSyncEnabled(
    RowlEngineHandle handle, int enabled) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "set_lip_sync_enabled", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        engine->setLipSyncEnabled(enabled != 0);
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_IsLipSyncEnabled(RowlEngineHandle handle) {
    if (classifyHandle(handle) == HandleStanding::Dead) return 0;
    if (!isLiveHandle(handle)) {
        stampWrongThread(handle, "is_lip_sync_enabled");
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto engine = toEngineChecked(handle);
        return (engine && engine->isLipSyncEnabled()) ? 1 : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_BeginCharacterExpressionBlend(
    RowlEngineHandle handle, float durationSeconds) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "begin_character_expression_blend", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        std::string error;
        if (!engine->beginExpressionBlend(durationSeconds, error)) {
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetCharacterFxSnapshotJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    RowlEngine_ResultCode guard = ROWL_RESULT_OK;
    if (!guardFxRead(handle, "get_character_fx_snapshot", guard)) return guard;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->characterFxSnapshotJson(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
