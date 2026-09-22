/**
 * c_api_embed_guards.cpp
 *
 * D01 (#135): checked embedding girişlerinin tanımları. Sözleşme için
 * rowl/c_api_embed.hpp'e bakın. Public contract c_api.h + c_api_embed.hpp;
 * paylaşılan guard'lar c_api_internal.hpp'ten gelir. engine.cpp /
 * window.cpp'a satır eklenmez (fail-closed yalnızca bu TU'da).
 */

#include "c_api_internal.hpp"
#include "rowl/c_api_embed.hpp"

extern "C" {

RowlEngine_ResultCode RowlEngine_SetExternalWindowHandleChecked(
    RowlEngineHandle handle,
    void* nativeWindowHandle,
    uint32_t width,
    uint32_t height) {
    static constexpr const char* kOp = "set_external_window_handle";
    // Ölü handle sessiz INVALID_HANDLE (damgalanacak motor yok).
    // Yabancı thread loud WRONG_THREAD (Init/Shutdown/Step tier'i).
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, kOp);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;

    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return ROWL_RESULT_INVALID_HANDLE;
        auto* ctx = checked->getContext();
        if (nativeWindowHandle == nullptr) {
            if (ctx) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                              "External window handle pointer is null; failing closed",
                              kOp, "");
            }
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        if (width == 0 || height == 0) {
            if (ctx) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                              "External window dimensions must be nonzero; failing closed",
                              kOp, "");
            }
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        if (checked->isInitialized()) {
            if (ctx) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::StateError,
                              "SetExternalWindowHandle must be called BEFORE RowlEngine_Init; "
                              "post-Init call rejected without touching the live window",
                              kOp, "");
            }
            return ROWL_RESULT_STATE_ERROR;
        }
        checked->setExternalWindowHandle(nativeWindowHandle, width, height);
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_ResizeViewportChecked(
    RowlEngineHandle handle,
    uint32_t newWidth,
    uint32_t newHeight) {
    static constexpr const char* kOp = "resize_viewport";
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, kOp);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;

    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return ROWL_RESULT_INVALID_HANDLE;
        // D1 (#110) disiplini: pre-Init sinyalli no-op (legacy void ile
        // aynı damga; test_lifecycle_init_guards.cpp:164 kilidi korunur).
        if (!requireEngineInitialized(checked, kOp)) return ROWL_RESULT_STATE_ERROR;
        auto* win = checked->getWindow();
        if (win) win->resizeViewport(newWidth, newHeight);
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

}  // extern "C"
