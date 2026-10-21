/**
 * c_api_game_identity.cpp — D01a game identity C API (thin wrappers).
 *
 * The resolution logic lives in platform/game_identity.cpp and runs once at
 * project mount (SetProjectDirectory). Same ABI discipline as the other
 * c_api_* units: live-handle guard, invokeNoexcept boundary, never throws.
 */
#include "c_api_internal.hpp"

namespace {

RowlEngine_ResultCode copyIdentityText(RowlEngineHandle handle, char* buffer,
                                       uint32_t bufferSize, uint32_t* outRequiredSize,
                                       bool diagnostic) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto& identity = engine->getGameIdentity();
        return copyUtf8ToCaller(diagnostic ? identity.diagnostic : identity.id, buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_GetGameIdUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    return copyIdentityText(handle, buffer, bufferSize, outRequiredSize, false);
}

RowlEngine_ResultCode RowlEngine_GetGameIdentityDiagnosticUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    return copyIdentityText(handle, buffer, bufferSize, outRequiredSize, true);
}

int32_t RowlEngine_GetGameIdentitySource(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return ROWL_GAME_IDENTITY_NONE;
    return invokeNoexcept<int32_t>([&] {
        auto engine = toEngineChecked(handle);
        return engine ? static_cast<int32_t>(engine->getGameIdentity().source)
                      : static_cast<int32_t>(ROWL_GAME_IDENTITY_NONE);
    }, static_cast<int32_t>(ROWL_GAME_IDENTITY_NONE));
}

} // extern "C"
