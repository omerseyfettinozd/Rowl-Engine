/**
 * c_api_ui_theme.cpp — T1 player-interface theme C API (thin wrappers).
 *
 * The theme lives in state/ui_theme.cpp (parser) and core/engine_ui_theme.cpp
 * (loading, precedence, window hand-over). Owner-thread API with the same
 * claim-or-reject discipline as the extras family; before RowlEngine_Init every
 * call is a STATE_ERROR.
 */
#include "c_api_internal.hpp"

namespace {

template <typename Fn>
RowlEngine_ResultCode withUiTheme(RowlEngineHandle handle, const char* op, Fn&& body) {
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, op);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        if (!engine->isInitialized()) return ROWL_RESULT_STATE_ERROR;
        return body(*engine);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

}  // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_GetUiThemeJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return withUiTheme(handle, "get_ui_theme_json", [&](Rowl::Core::Engine& engine) {
        return copyUtf8ToCaller(engine.getUiThemeJson(), buffer, bufferSize, outRequiredSize);
    });
}

RowlEngine_ResultCode RowlEngine_ReloadUiTheme(RowlEngineHandle handle) {
    return withUiTheme(handle, "reload_ui_theme", [&](Rowl::Core::Engine& engine) {
        engine.loadUiTheme();
        return ROWL_RESULT_OK;
    });
}

RowlEngine_ResultCode RowlEngine_SetUiThemeJson(RowlEngineHandle handle, const char* themeJson) {
    return withUiTheme(handle, "set_ui_theme_json", [&](Rowl::Core::Engine& engine) {
        if (themeJson == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
        return engine.setUiThemeText(themeJson) ? ROWL_RESULT_OK : ROWL_RESULT_VALIDATION_ERROR;
    });
}

} // extern "C"
