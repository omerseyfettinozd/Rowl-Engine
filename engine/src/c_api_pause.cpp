/**
 * c_api_pause.cpp — MS-6 quick slots & pause-menu surface.
 *
 * Same ABI discipline as the other c_api_* units: live-handle guard,
 * invokeNoexcept boundary, thread-local string buffers, never throws.
 */
#include "c_api_internal.hpp"

#include <cstring>

int RowlEngine_SetQuickSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->setQuickSaveSlot(slotIndex)) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetQuickSaveSlot(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return -1;
    return invokeNoexcept<int32_t>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getQuickSaveSlot() : -1;
    }, -1);
}

int RowlEngine_QuickSave(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->quickSave()) ? 1 : 0;
    }, 0);
}

int RowlEngine_QuickLoad(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->quickLoad()) ? 1 : 0;
    }, 0);
}

void RowlEngine_SetPaused(RowlEngineHandle handle, int paused) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) checked->setPaused(paused != 0);
    });
}

int RowlEngine_IsPaused(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isPaused()) ? 1 : 0;
    }, 0);
}

void RowlEngine_PauseMenuCommand(RowlEngineHandle handle, int command) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        using Cmd = Rowl::Core::PauseMenuCommand;
        Cmd parsed;
        switch (command) {
            case ROWL_PAUSE_MENU_UP: parsed = Cmd::Up; break;
            case ROWL_PAUSE_MENU_DOWN: parsed = Cmd::Down; break;
            case ROWL_PAUSE_MENU_LEFT: parsed = Cmd::Left; break;
            case ROWL_PAUSE_MENU_RIGHT: parsed = Cmd::Right; break;
            case ROWL_PAUSE_MENU_BACK: parsed = Cmd::Back; break;
            case ROWL_PAUSE_MENU_CONFIRM: parsed = Cmd::Confirm; break;
            default: return;
        }
        if (auto checked = toEngineChecked(handle)) checked->pauseMenuCommand(parsed);
    });
}

const char* RowlEngine_GetPauseMenuJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "{\"open\":false,\"rows\":[]}";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        auto checked = toEngineChecked(handle);
        buffer = checked ? checked->getPauseMenuJson() : "{\"open\":false,\"rows\":[]}";
        return buffer.c_str();
    }, "{\"open\":false,\"rows\":[]}");
}

const char* RowlEngine_GetPauseMenuJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetPauseMenuJson(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

// B2a: PauseMenu caller-buffer varyantı (dead-handle'da INVALID_HANDLE;
// eski API "{\"open\":false,\"rows\":[]}" dönerdi, o korunur).
RowlEngine_ResultCode RowlEngine_GetPauseMenuJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getPauseMenuJson(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}
