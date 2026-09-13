/**
 * c_api_state.cpp
 *
 * C-API session state: save slots, rewind, scripting, diagnostics, structured results.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "nlohmann/json.hpp"
#include "cstring"
extern "C" {
/* ── Scripting, session state & structured results ────────────────────────── */

const char* RowlEngine_GetScriptRuntimeDiagnosticsJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "[]";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json diagnostics = nlohmann::json::array();
        for (const auto& status : toEngine(handle)->getScriptRuntimeStatuses()) {
            diagnostics.push_back({
                {"module_id", status.moduleId},
                {"path", status.sourcePath},
                {"state", status.state},
                {"error", status.lastError},
            });
        }
        buffer = diagnostics.dump();
        return buffer.c_str();
    }, "[]");
}

const char* RowlEngine_GetDialogueHistoryJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "[]";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json history = nlohmann::json::array();
        for (const auto& entry : toEngine(handle)->getDialogueHistory()) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
            });
        }
        buffer = history.dump();
        return buffer.c_str();
    }, "[]");
}

const char* RowlEngine_GetScriptRuntimeDiagnosticsJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetScriptRuntimeDiagnosticsJson(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

const char* RowlEngine_GetDialogueHistoryJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetDialogueHistoryJson(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

int RowlEngine_SaveGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->saveGameSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_LoadGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->loadGameSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_HasSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->hasSaveSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_DeleteSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->deleteSaveSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_Rewind(RowlEngineHandle handle, uint32_t steps) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->rewind(steps) ? 1 : 0;
    }, 0);
}

uint64_t RowlEngine_GetCurrentStepId(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        return toEngine(handle)->getCurrentStepId();
    }, 0);
}

void RowlEngine_SetVariable(RowlEngineHandle handle, const char* key, const char* value) {
    if (!isLiveHandle(handle) || !key || !value) return;
    invokeNoexcept([&] {
        toEngine(handle)->setScriptVariable(key, value);
    });
}

const char* RowlEngine_GetVariable(RowlEngineHandle handle, const char* key) {
    if (!isLiveHandle(handle) || !key) return "";
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getScriptVariable(key);
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetVariableWithLength(RowlEngineHandle handle, const char* key, uint32_t* outLen) {
    const char* value = RowlEngine_GetVariable(handle, key);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

int RowlEngine_EvaluateCondition(RowlEngineHandle handle, const char* conditionExpr) {
    if (!isLiveHandle(handle) || !conditionExpr) return 1;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->evaluateCondition(conditionExpr) ? 1 : 0;
    }, 1);
}

int RowlEngine_ExecuteScript(RowlEngineHandle handle, const char* scriptCode) {
    if (!isLiveHandle(handle)) return 0;
    if (!scriptCode) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Script code string pointer is null",
                                  "execute_script", "");
                }
            }
        });
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto* engine = toEngine(handle);
        return (engine && engine->executeScript(scriptCode)) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetLastResultCode(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::InvalidHandle);
    return invokeNoexcept<int32_t>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError);
        return engine->getContext()->getLastResult().rawCode();
    }, static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError));
}

const char* RowlEngine_GetLastResultOperation(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        buf = "none";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "unknown";
        buf = engine->getContext()->getLastResult().operation;
        return buf.c_str();
    }, "unknown");
}

const char* RowlEngine_GetLastResultMessage(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        buf = "Invalid or uninitialized engine handle";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "Internal error occurred";
        buf = engine->getContext()->getLastResult().message;
        return buf.c_str();
    }, "Internal error occurred");
}

const char* RowlEngine_GetLastResultTarget(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) return buf.c_str();
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "";
        buf = engine->getContext()->getLastResult().target;
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetLastResultOperationWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultOperation(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

const char* RowlEngine_GetLastResultMessageWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultMessage(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

const char* RowlEngine_GetLastResultTargetWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultTarget(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

void RowlEngine_ClearLastResult(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        if (engine && engine->getContext()) {
            engine->getContext()->clearResult();
        }
    });
}

} // extern "C"
