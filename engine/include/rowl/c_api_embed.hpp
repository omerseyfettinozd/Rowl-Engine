/**
 * rowl/c_api_embed.hpp
 *
 * D01 (#135) — checked (ResultCode-dönüşlü) native-window embedding girişleri.
 * Additive: mevcut void C ABI'ye dokunmaz (c_api.h aynen durur).
 *
 * Sözleşme (fail-closed, throw yok):
 *  - Ölü/bilinmeyen handle (null dahil) → ROWL_RESULT_INVALID_HANDLE.
 *    Sessizdir: damgalanacak motor yoktur (threading sözleşmesindeki
 *    "silent" kademesi).
 *  - Canlı handle'a yabancı thread → ROWL_RESULT_WRONG_THREAD + loud
 *    damga (sahiplik-kademesi; Init/Shutdown/Step ile aynı tier — pencere
 *    yüzeyi motor-sahipliği düzeyindedir, 60 Hz hot-path değildir).
 *  - null nativeWindowHandle / sıfır boyut → ROWL_RESULT_INVALID_ARGUMENT
 *    + damga. (DİKKAT: nonzero bogus bir OS handle'ı taşınabilir düzeyde
 *    doğrulanamaz; pre-Init kabul edilir, Init gömme-denemesi başarısız
 *    olursa StateError ile düşer. Host "bogus"u Init-öncesi eler.)
 *  - Init SONRASI SetExternal → ROWL_RESULT_STATE_ERROR + damga, pencereye
 *    dokunulmaz (legacy void de aynı damgayı vurur, Ek D1 #135).
 *  - Pre-Init ResizeViewportChecked → ROWL_RESULT_STATE_ERROR + damga
 *    (legacy void sinyaliyle aynı: test_lifecycle_init_guards.cpp:164).
 *  - Pre-Init SetExternalChecked → depolar + ROWL_RESULT_OK; Init tüketir.
 *
 * Host sıralaması (editor EmbeddedRuntimeBootstrap): worker (owner thread)
 * üzerinden ÖNCE Checked-SetExternal, SONRA Init. Checked != Ok ise Init
 * çağrılmaz.
 */

#pragma once

#include <stdint.h>

#include "rowl/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gömülü pencere handle'ını Init-öncesi kurar; her ret damgalıdır.
 * Başarıda ROWL_RESULT_OK döner (Init bu depoyu tüketir).
 */
ROWL_API RowlEngine_ResultCode RowlEngine_SetExternalWindowHandleChecked(
    RowlEngineHandle handle,
    void* nativeWindowHandle,
    uint32_t width,
    uint32_t height);

/**
 * Gömülü render alanının boyut değişimini bildirir; Init-öncesi
 * STATE_ERROR döner (Init-sonrası forward + OK).
 */
ROWL_API RowlEngine_ResultCode RowlEngine_ResizeViewportChecked(
    RowlEngineHandle handle,
    uint32_t newWidth,
    uint32_t newHeight);

#ifdef __cplusplus
}  // extern "C"
#endif
