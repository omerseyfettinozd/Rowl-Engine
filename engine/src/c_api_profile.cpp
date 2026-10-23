/**
 * c_api_profile.cpp — D01c-1 player profile C API (thin wrappers).
 *
 * The profile logic lives in state/player_profile.cpp; the Engine owns the
 * store (loaded at SetProjectDirectory, flushed on shutdown). Owner-thread
 * API: same claim-or-reject discipline as the save/load family, so the store
 * (no internal locking) is only ever touched from the engine's thread.
 */
#include "c_api_internal.hpp"

#include "rowl/state/player_profile.hpp"

namespace {

using Rowl::State::PlayerProfileStore;
using Rowl::State::ProfileMutation;

template <typename Fn>
RowlEngine_ResultCode withProfile(RowlEngineHandle handle, const char* op, Fn&& body) {
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, op);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return body(engine->playerProfile());
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode toResult(ProfileMutation mutation) {
    switch (mutation) {
        case ProfileMutation::Ok:
        case ProfileMutation::Unchanged: return ROWL_RESULT_OK;
        case ProfileMutation::Invalid: return ROWL_RESULT_INVALID_ARGUMENT;
        case ProfileMutation::ReadOnly:
        case ProfileMutation::Unmounted: return ROWL_RESULT_STATE_ERROR;
        case ProfileMutation::Full: return ROWL_RESULT_VALIDATION_ERROR;
    }
    return ROWL_RESULT_UNKNOWN_ERROR;
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_ProfileGetJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return withProfile(handle, "profile_get_json", [&](PlayerProfileStore& profile) {
        return copyUtf8ToCaller(profile.toJson(), buffer, bufferSize, outRequiredSize);
    });
}

RowlEngine_ResultCode RowlEngine_ProfileGetStatusJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return withProfile(handle, "profile_get_status", [&](PlayerProfileStore& profile) {
        return copyUtf8ToCaller(profile.statusJson(), buffer, bufferSize, outRequiredSize);
    });
}

RowlEngine_ResultCode RowlEngine_ProfileSetPreferencesJson(
    RowlEngineHandle handle, const char* json) {
    return withProfile(handle, "profile_set_preferences", [&](PlayerProfileStore& profile) {
        if (!json) return ROWL_RESULT_INVALID_ARGUMENT;
        return toResult(profile.setPreferencesJson(json));
    });
}

RowlEngine_ResultCode RowlEngine_ProfileMarkRead(RowlEngineHandle handle, const char* contentId) {
    return withProfile(handle, "profile_mark_read", [&](PlayerProfileStore& profile) {
        if (!contentId) return ROWL_RESULT_INVALID_ARGUMENT;
        return toResult(profile.markRead(contentId));
    });
}

RowlEngine_ResultCode RowlEngine_ProfileIsRead(
    RowlEngineHandle handle, const char* contentId, int32_t* outIsRead) {
    if (outIsRead) *outIsRead = 0;
    return withProfile(handle, "profile_is_read", [&](PlayerProfileStore& profile) {
        if (!contentId || !outIsRead) return ROWL_RESULT_INVALID_ARGUMENT;
        *outIsRead = profile.isRead(contentId) ? 1 : 0;
        return ROWL_RESULT_OK;
    });
}

RowlEngine_ResultCode RowlEngine_ProfileFlush(RowlEngineHandle handle) {
    return withProfile(handle, "profile_flush", [&](PlayerProfileStore& profile) {
        return profile.flush() ? ROWL_RESULT_OK : ROWL_RESULT_IO_ERROR;
    });
}

} // extern "C"
