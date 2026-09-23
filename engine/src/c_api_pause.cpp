/**
 * c_api_pause.cpp — MS-6 quick slots & pause-menu surface.
 *
 * Same ABI discipline as the other c_api_* units: live-handle guard,
 * invokeNoexcept boundary, thread-local string buffers, never throws.
 */
#include "c_api_internal.hpp"

// R1 (#6): WithLength boyutu bu tampondan alır.
static thread_local std::string g_pauseMenuJsonBuf;

int RowlEngine_SetQuickSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    // W8-a (2): kardes SaveGameSlotResult emsali (c_api_state.cpp:129-133) —
    // claim-or-reject ile loud: yabanci WRONG_THREAD damgali 0, state'e
    // dokunulmaz. Olu-handle sessiz 0 aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "set_quick_save_slot");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->setQuickSaveSlot(slotIndex)) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetQuickSaveSlot(RowlEngineHandle handle) {
    // W8-f1 (1): SetQuickSaveSlot emsali (yukarida) — claim-or-reject ile
    // loud: yabanci WRONG_THREAD damgali -1, state'e dokunulmaz.
    // Olu-handle sessiz -1 aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "get_quick_save_slot");
        return -1;
    }
    if (!isLiveHandle(handle)) return -1;
    return invokeNoexcept<int32_t>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getQuickSaveSlot() : -1;
    }, -1);
}

int RowlEngine_QuickSave(RowlEngineHandle handle) {
    // W8-a (2): SetQuickSaveSlot ile ayni loud-kapi (op: quick_save).
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "quick_save");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->quickSave()) ? 1 : 0;
    }, 0);
}

int RowlEngine_QuickLoad(RowlEngineHandle handle) {
    // W8-a (2): SetQuickSaveSlot ile ayni loud-kapi (op: quick_load).
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "quick_load");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->quickLoad()) ? 1 : 0;
    }, 0);
}

void RowlEngine_SetPaused(RowlEngineHandle handle, int paused) {
    // W8-f1 (1): SetQuickSaveSlot emsali — claim-or-reject ile loud:
    // yabanci WRONG_THREAD damgali sessiz ret, state'e dokunulmaz.
    // Olu-handle sessiz ret aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "set_paused");
        return;
    }
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) checked->setPaused(paused != 0);
    });
}

int RowlEngine_IsPaused(RowlEngineHandle handle) {
    // W8-f1 (1): SetQuickSaveSlot emsali — claim-or-reject ile loud:
    // yabanci WRONG_THREAD damgali 0, state'e dokunulmaz. Olu-handle
    // sessiz 0 aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "is_paused");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isPaused()) ? 1 : 0;
    }, 0);
}

void RowlEngine_PauseMenuCommand(RowlEngineHandle handle, int command) {
    // W8-f1 (1): SetQuickSaveSlot emsali — claim-or-reject ile loud:
    // yabanci WRONG_THREAD damgali sessiz ret, state'e dokunulmaz.
    // Olu-handle sessiz ret aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "pause_menu_command");
        return;
    }
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
    // W8-f1 (1): SetQuickSaveSlot emsali — claim-or-reject ile loud:
    // yabanci WRONG_THREAD damgali kapali literal, state'e dokunulmaz.
    // Olu-handle aynen.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "get_pause_menu_json");
        return "{\"open\":false,\"rows\":[]}";
    }
    if (!isLiveHandle(handle)) return "{\"open\":false,\"rows\":[]}";
    std::string& buffer = g_pauseMenuJsonBuf;
    return invokeNoexcept<const char*>([&] {
        auto checked = toEngineChecked(handle);
        buffer = checked ? checked->getPauseMenuJson() : "{\"open\":false,\"rows\":[]}";
        return buffer.c_str();
    }, "{\"open\":false,\"rows\":[]}");
}

const char* RowlEngine_GetPauseMenuJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    // W8-f1 (1): ayri kapi yok — ic getter (yukarida) claim'ler; sarmalayici
    // kapiyi almaz (story WithLength emsali: cift-kapi self-deadlock disiplini).
    const char* value = RowlEngine_GetPauseMenuJson(handle);
    if (outLen) *outLen = withLengthOf(g_pauseMenuJsonBuf, value);
    return value;
}

// B2a: PauseMenu caller-buffer varyantı (dead-handle'da INVALID_HANDLE;
// eski API "{\"open\":false,\"rows\":[]}" dönerdi, o korunur).
RowlEngine_ResultCode RowlEngine_GetPauseMenuJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    // W8-f1 (1): claim-or-reject ile loud damga; donus disiplini korunur
    // (olu-handle gibi INVALID_HANDLE).
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "get_pause_menu_json");
        return ROWL_RESULT_INVALID_HANDLE;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getPauseMenuJson(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}
