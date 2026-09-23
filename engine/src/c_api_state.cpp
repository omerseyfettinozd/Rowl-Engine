/**
 * c_api_state.cpp
 *
 * C-API session state: save slots, rewind, scripting, diagnostics, structured results.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/state/save_metadata.hpp"
#include "rowl/state/save_slots.hpp"
#include "nlohmann/json.hpp"
extern "C" {
/* ── Scripting, session state & structured results ────────────────────────── */

// R1 (#6): WithLength boyutları bu tamponlardan alır.
static thread_local std::string g_scriptDiagnosticsBuf;
static thread_local std::string g_dialogueHistoryBuf;
static thread_local std::string g_variableBuf;
static thread_local std::string g_lastResultOpBuf;
static thread_local std::string g_lastResultMsgBuf;
static thread_local std::string g_lastResultTargetBuf;

const char* RowlEngine_GetScriptRuntimeDiagnosticsJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "[]";
    std::string& buffer = g_scriptDiagnosticsBuf;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json diagnostics = nlohmann::json::array();
        auto checked = toEngineChecked(handle);
        if (!checked) return "[]";
        for (const auto& status : checked->getScriptRuntimeStatuses()) {
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
    std::string& buffer = g_dialogueHistoryBuf;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json history = nlohmann::json::array();
        auto checked = toEngineChecked(handle);
        if (!checked) return "[]";
        for (const auto& entry : checked->getDialogueHistory()) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
                {"content_id", entry.contentId},
            });
        }
        buffer = history.dump();
        return buffer.c_str();
    }, "[]");
}

const char* RowlEngine_GetScriptRuntimeDiagnosticsJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetScriptRuntimeDiagnosticsJson(handle);
    if (outLen) *outLen = withLengthOf(g_scriptDiagnosticsBuf, value);
    return value;
}

const char* RowlEngine_GetDialogueHistoryJsonWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetDialogueHistoryJson(handle);
    if (outLen) *outLen = withLengthOf(g_dialogueHistoryBuf, value);
    return value;
}

// B2a: thread_local ödünç-imza yerine caller-buffer varyantları
// (additive-only; eski const char* API'ler aynen korunur).
RowlEngine_ResultCode RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        nlohmann::json diagnostics = nlohmann::json::array();
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        for (const auto& status : engine->getScriptRuntimeStatuses()) {
            diagnostics.push_back({
                {"module_id", status.moduleId},
                {"path", status.sourcePath},
                {"state", status.state},
                {"error", status.lastError},
            });
        }
        return copyUtf8ToCaller(diagnostics.dump(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetDialogueHistoryJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        nlohmann::json history = nlohmann::json::array();
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        for (const auto& entry : engine->getDialogueHistory()) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
                {"content_id", entry.contentId},
            });
        }
        return copyUtf8ToCaller(history.dump(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_SaveGameSlotResult(
    RowlEngineHandle handle, int32_t slotIndex) {
    // D4 (#49): claim-or-reject. Create-sonrası/Init-öncesi penceresinde
    // handle sahipsizdir (isLiveHandle her thread'e geçer); iki thread
    // Save/Load/Rewind'i kilitsiz yarıştırıp m_gameState shared_ptr'ında UB
    // üretirdi. İlk çağıran claim'ler, yabancılar WRONG_THREAD damgasıyla
    // reddedilir — sessiz INVALID_HANDLE gömülmesi yok, state'e dokunulmaz.
    // Tek-kilit: classify-then-claim TOCTOU'su kaybedeni yanlışlıkla
    // INVALID_HANDLE yapardı.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "save_game_slot");
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        engine->saveGameSlot(slotIndex);
        const auto context = engine->getContext();
        return context
            ? static_cast<RowlEngine_ResultCode>(context->getLastResult().rawCode())
            : ROWL_RESULT_UNKNOWN_ERROR;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_SaveGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    return RowlEngine_SaveGameSlotResult(handle, slotIndex) == ROWL_RESULT_OK ? 1 : 0;
}

RowlEngine_ResultCode RowlEngine_LoadGameSlotResult(
    RowlEngineHandle handle, int32_t slotIndex) {
    // D4 (#49): Save ile aynı claim-or-reject (gerekçe yukarıda).
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "load_game_slot");
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        engine->loadGameSlot(slotIndex);
        const auto context = engine->getContext();
        return context
            ? static_cast<RowlEngine_ResultCode>(context->getLastResult().rawCode())
            : ROWL_RESULT_UNKNOWN_ERROR;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_LoadGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    return RowlEngine_LoadGameSlotResult(handle, slotIndex) == ROWL_RESULT_OK ? 1 : 0;
}

int RowlEngine_HasSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    // D4 (#49): salt-okuma değil — Engine::hasSaveSlot görünürde const ama
    // sessionPersistence() üzerinden setSaveDirectory yazar; aynı kapı.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "has_save_slot");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->hasSaveSlot(slotIndex)) ? 1 : 0;
    }, 0);
}

int RowlEngine_DeleteSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    // D4 (#49): disk + m_pauseSlotCacheValid yazar; aynı kapı.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "delete_save_slot");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->deleteSaveSlot(slotIndex)) ? 1 : 0;
    }, 0);
}

int RowlEngine_Rewind(RowlEngineHandle handle, uint32_t steps) {
    // D4 (#49): m_gameState zincirini yazar; aynı kapı.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "rewind");
        return 0;
    }
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->rewind(steps)) ? 1 : 0;
    }, 0);
}

uint64_t RowlEngine_GetCurrentStepId(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getCurrentStepId() : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_GetSaveSlotMetadataJson(
    RowlEngineHandle handle, int32_t slotIndex, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    // D4 (#49): display-only ama engine->hasSaveSlot üzerinden aynı
    // yazan-yola girer; aynı kapı.
    if (claimHandleOrClassify(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "get_save_slot_metadata");
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    if (!Rowl::State::isValidSlot(slotIndex)) return ROWL_RESULT_INVALID_ARGUMENT;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        // Display-only read: the live story is never touched.
        if (!engine->hasSaveSlot(slotIndex)) return ROWL_RESULT_FILE_NOT_FOUND;
        const auto state = Rowl::State::GameState::loadFromSlot(
            slotIndex,
            Rowl::Platform::pathToUtf8(engine->getSaveDirectoryPath()));
        if (!state) return ROWL_RESULT_PARSE_ERROR;
        const nlohmann::json metadata = {
            {"slot", slotIndex},
            {"saved_at", state->savedAt},
            {"playtime_seconds", state->playtimeSeconds},
            {"chapter_id", state->chapterId},
            {"chapter_title", state->chapterTitle},
            {"summary", state->summary},
            {"thumbnail_width", state->thumbnailWidth},
            {"thumbnail_height", state->thumbnailHeight},
            {"has_thumbnail", !state->thumbnailPng.empty()},
            {"thumbnail_png_base64", Rowl::State::base64Encode(
                reinterpret_cast<const uint8_t*>(state->thumbnailPng.data()),
                static_cast<uint32_t>(state->thumbnailPng.size()))},
        };
        return copyUtf8ToCaller(metadata.dump(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

void RowlEngine_SetVariable(RowlEngineHandle handle, const char* key, const char* value) {
    if (!isLiveHandle(handle) || !key || !value) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) checked->setScriptVariable(key, value);
    });
}

const char* RowlEngine_GetVariable(RowlEngineHandle handle, const char* key) {
    if (!isLiveHandle(handle) || !key) return "";
    std::string& buf = g_variableBuf;
    return invokeNoexcept<const char*>([&] {
        auto checked = toEngineChecked(handle);
        buf = checked ? checked->getScriptVariable(key) : "";
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetVariableWithLength(RowlEngineHandle handle, const char* key, uint32_t* outLen) {
    const char* value = RowlEngine_GetVariable(handle, key);
    if (outLen) *outLen = withLengthOf(g_variableBuf, value);
    return value;
}

// B2a: GetVariable caller-buffer varyantı (ölü-handle'da INVALID_HANDLE;
// eski API "" dönerdi, o korunur).
// W8-a (6): null-key ölü-handle kanalından ayrıldı (EvaluateCondition
// :301-312 emsali) — önce handle (ölü -> INVALID_HANDLE aynen), sonra
// null-key InvalidArgument + damga. INVALID_HANDLE yalnız ölü-handle'ındır.
RowlEngine_ResultCode RowlEngine_GetVariableUtf8(
    RowlEngineHandle handle, const char* key, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    if (!key) {
        invokeNoexcept([&] {
            if (auto engine = toEngineChecked(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Variable key pointer is null; failing closed",
                                  "get_variable", "");
                }
            }
        });
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getScriptVariable(key), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_EvaluateCondition(RowlEngineHandle handle, const char* conditionExpr) {
    // Fail-closed: every error path reports false (0). A dead handle surfaces
    // as InvalidHandle through RowlEngine_GetLastResultCode(); a null
    // expression records InvalidArgument on the handle's context.
    if (!isLiveHandle(handle)) return 0;
    if (!conditionExpr) {
        invokeNoexcept([&] {
            if (auto engine = toEngineChecked(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Condition expression pointer is null; failing closed",
                                  "evaluate_condition", "");
                }
            }
        });
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->evaluateCondition(conditionExpr)) ? 1 : 0;
    }, 0);
}

int RowlEngine_ExecuteScript(RowlEngineHandle handle, const char* scriptCode) {
    if (!isLiveHandle(handle)) return 0;
    if (!scriptCode) {
        invokeNoexcept([&] {
            if (auto engine = toEngineChecked(handle)) {
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
        auto engine = toEngineChecked(handle);
        return (engine && engine->executeScript(scriptCode)) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetLastResultCode(RowlEngineHandle handle) {
    // D3 (B1d #102/#150): live-but-foreign reads the engine's OWN stamped
    // result (WRONG_THREAD after a rejected call) through shared ownership —
    // INVALID_HANDLE stays reserved for genuinely dead handles, so callers
    // can finally tell the two apart. Context reads are internally locked;
    // a handle that died between classify and copy reports INVALID_HANDLE.
    if (!isLiveHandle(handle)) {
        if (classifyHandle(handle) != HandleStanding::Foreign) {
            return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::InvalidHandle);
        }
        return invokeNoexcept<int32_t>([&] {
            const auto engine = copyEngineAnyThread(handle);
            if (!engine || !engine->getContext()) {
                return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::InvalidHandle);
            }
            return engine->getContext()->getLastResult().rawCode();
        }, static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError));
    }
    return invokeNoexcept<int32_t>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError);
        return engine->getContext()->getLastResult().rawCode();
    }, static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError));
}

const char* RowlEngine_GetLastResultOperation(RowlEngineHandle handle) {
    std::string& buf = g_lastResultOpBuf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        // D3: foreign-live → stamped op ("shutdown"/"step"/...); dead → "none".
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<const char*>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return "unknown";
                buf = engine->getContext()->getLastResult().operation;
                return buf.c_str();
            }, "unknown");
        }
        buf = "none";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return "unknown";
        buf = engine->getContext()->getLastResult().operation;
        return buf.c_str();
    }, "unknown");
}

const char* RowlEngine_GetLastResultMessage(RowlEngineHandle handle) {
    std::string& buf = g_lastResultMsgBuf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        // D3: foreign-live → stamped message; dead → legacy text.
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<const char*>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return "Internal error occurred";
                buf = engine->getContext()->getLastResult().message;
                return buf.c_str();
            }, "Internal error occurred");
        }
        buf = "Invalid or uninitialized engine handle";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return "Internal error occurred";
        buf = engine->getContext()->getLastResult().message;
        return buf.c_str();
    }, "Internal error occurred");
}

const char* RowlEngine_GetLastResultTarget(RowlEngineHandle handle) {
    std::string& buf = g_lastResultTargetBuf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        // D3: foreign-live → stamped target; dead → empty (unchanged).
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<const char*>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return "";
                buf = engine->getContext()->getLastResult().target;
                return buf.c_str();
            }, "");
        }
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return "";
        buf = engine->getContext()->getLastResult().target;
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetLastResultOperationWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultOperation(handle);
    if (outLen) *outLen = withLengthOf(g_lastResultOpBuf, value);
    return value;
}

const char* RowlEngine_GetLastResultMessageWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultMessage(handle);
    if (outLen) *outLen = withLengthOf(g_lastResultMsgBuf, value);
    return value;
}

const char* RowlEngine_GetLastResultTargetWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastResultTarget(handle);
    if (outLen) *outLen = withLengthOf(g_lastResultTargetBuf, value);
    return value;
}

// B2a: LastResult üçlüsünün caller-buffer varyantları. Eski API'lerin
// dead-handle string'leri ("none", "Invalid or...", "") korunur; yeni
// varyantlar dead-handle'da INVALID_HANDLE döner (buffer'lı kontrat).
RowlEngine_ResultCode RowlEngine_GetLastResultOperationUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) {
        // D3-fix (review): plain okuyucu gibi Foreign'a damgayı sun;
        // INVALID_HANDLE yalnız gerçekten ölü handle'ındır.
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<RowlEngine_ResultCode>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
                return copyUtf8ToCaller(engine->getContext()->getLastResult().operation,
                                        buffer, bufferSize, outRequiredSize);
            }, ROWL_RESULT_UNKNOWN_ERROR);
        }
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getContext()->getLastResult().operation,
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLastResultMessageUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) {
        // D3-fix (review): Foreign damga-okuması; ölü → INVALID_HANDLE.
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<RowlEngine_ResultCode>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
                return copyUtf8ToCaller(engine->getContext()->getLastResult().message,
                                        buffer, bufferSize, outRequiredSize);
            }, ROWL_RESULT_UNKNOWN_ERROR);
        }
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getContext()->getLastResult().message,
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLastResultTargetUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) {
        // D3-fix (review): Foreign damga-okuması; ölü → INVALID_HANDLE.
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            return invokeNoexcept<RowlEngine_ResultCode>([&] {
                const auto engine = copyEngineAnyThread(handle);
                if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
                return copyUtf8ToCaller(engine->getContext()->getLastResult().target,
                                        buffer, bufferSize, outRequiredSize);
            }, ROWL_RESULT_UNKNOWN_ERROR);
        }
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine || !engine->getContext()) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(engine->getContext()->getLastResult().target,
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

void RowlEngine_ClearLastResult(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto engine = toEngineChecked(handle);
        if (engine && engine->getContext()) {
            engine->getContext()->clearResult();
        }
    });
}

} // extern "C"
