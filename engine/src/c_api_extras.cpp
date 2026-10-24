/**
 * c_api_extras.cpp — D07-1 unlockable-content C API (thin wrappers).
 *
 * The catalog and the unlock logic live in core/engine_extras.cpp and
 * state/player_profile.cpp. Owner-thread API with the same claim-or-reject
 * discipline as the profile family; before RowlEngine_Init every call is a
 * STATE_ERROR.
 */
#include "c_api_internal.hpp"

#include "rowl/state/player_profile.hpp"

namespace {

using Rowl::State::ProfileMutation;
using Rowl::State::UnlockKind;

template <typename Fn>
RowlEngine_ResultCode withExtras(RowlEngineHandle handle, const char* op, Fn&& body) {
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

RowlEngine_ResultCode RowlEngine_GetExtrasJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return withExtras(handle, "get_extras_json", [&](Rowl::Core::Engine& engine) {
        return copyUtf8ToCaller(engine.getExtrasJson(), buffer, bufferSize, outRequiredSize);
    });
}

RowlEngine_ResultCode RowlEngine_ProfileUnlock(RowlEngineHandle handle, const char* kind, const char* id) {
    return withExtras(handle, "profile_unlock", [&](Rowl::Core::Engine& engine) {
        if (!kind || !id) return ROWL_RESULT_INVALID_ARGUMENT;
        const auto parsed = Rowl::State::parseUnlockKind(kind);
        if (!parsed) return ROWL_RESULT_INVALID_ARGUMENT;
        switch (engine.unlockExtra(*parsed, id)) {
            case ProfileMutation::Ok:
            case ProfileMutation::Unchanged: return ROWL_RESULT_OK;
            case ProfileMutation::Invalid: return ROWL_RESULT_INVALID_ARGUMENT;
            case ProfileMutation::ReadOnly:
            case ProfileMutation::Unmounted: return ROWL_RESULT_STATE_ERROR;
            case ProfileMutation::Full: return ROWL_RESULT_VALIDATION_ERROR;
        }
        return ROWL_RESULT_UNKNOWN_ERROR;
    });
}

RowlEngine_ResultCode RowlEngine_ProfileIsUnlocked(
    RowlEngineHandle handle, const char* kind, const char* id, int32_t* outIsUnlocked) {
    if (outIsUnlocked) *outIsUnlocked = 0;
    return withExtras(handle, "profile_is_unlocked", [&](Rowl::Core::Engine& engine) {
        if (!kind || !id || !outIsUnlocked) return ROWL_RESULT_INVALID_ARGUMENT;
        const auto parsed = Rowl::State::parseUnlockKind(kind);
        if (!parsed) return ROWL_RESULT_INVALID_ARGUMENT;
        *outIsUnlocked = engine.playerProfile().isUnlocked(*parsed, id) ? 1 : 0;
        return ROWL_RESULT_OK;
    });
}

} // extern "C"
