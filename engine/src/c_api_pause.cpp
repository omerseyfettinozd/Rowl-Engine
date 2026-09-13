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
        return toEngine(handle)->setQuickSaveSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetQuickSaveSlot(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return -1;
    return invokeNoexcept<int32_t>([&] {
        return toEngine(handle)->getQuickSaveSlot();
    }, -1);
}

int RowlEngine_QuickSave(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->quickSave() ? 1 : 0;
    }, 0);
}

int RowlEngine_QuickLoad(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->quickLoad() ? 1 : 0;
    }, 0);
}

void RowlEngine_SetPaused(RowlEngineHandle handle, int paused) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setPaused(paused != 0);
    });
}

int RowlEngine_IsPaused(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->isPaused() ? 1 : 0;
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
        toEngine(handle)->pauseMenuCommand(parsed);
    });
}

const char* RowlEngine_GetPauseMenuJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "{\"open\":false,\"rows\":[]}";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        buffer = toEngine(handle)->getPauseMenuJson();
        return buffer.c_str();
    }, "{\"open\":false,\"rows\":[]}");
}

const char* RowlEngine_GetPauseMenuJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetPauseMenuJson(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}
