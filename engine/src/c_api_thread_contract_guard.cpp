/**
 * c_api_thread_contract_guard.cpp
 *
 * D14: legacy mixer koprusu icin checked loud varyantlar. D01
 * (c_api_embed_guards.cpp) desenini izler: public contract yalnizca
 * rowl/c_api.h'tir; bu TU yeni TU'dur, engine.cpp / window.cpp /
 * MainWindowViewModel / EngineHost'a satir eklenmez (fail-closed yalnizca
 * burada). Paylasilan guard'lar c_api_internal.hpp'ten gelir
 * (classifyHandle / isLiveHandle / stampWrongThread).
 *
 * Kapsanan legacy yuzey (c_api.h): SetFadeCurve / GetFadeCurve (Faz 5 Dilim 2
 * mixer, ~816-822 bandi). Legacy void/int formlar sessiz-tier'dir
 * (olu handle'da no-op / 0; yabanci thread'de de sessiz duser cunku yalnizca
 * isLiveHandle bakar). Checked formlar loud-tier'dir: canli handle'a yabanci
 * thread'den cagri WRONG_THREAD (14) damgalar + cross-thread okunur
 * (RowlEngine_GetLastResultCode/Message); olu handle sessiz INVALID_HANDLE
 * (damgalanacak motor yok).
 *
 * Baslik rotusu YOK (D14 karari): prototipler burada + docs/
 * THREAD_CONTRACT_LEGACY_BRIDGE.md'de belgelenir; c_api.h'ye dokunulmaz,
 * boylece D12'nin :29-31 bandi duzeltmesiyle rebase cakismasi olmaz ve DN
 * kapisi duser. ABI yalnizca buyur (sembol silinmez).
 */

#include "c_api_internal.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/audio/fade_curves.hpp"

extern "C" {

ROWL_API RowlEngine_ResultCode RowlEngine_SetFadeCurveChecked(
    RowlEngineHandle handle, int curve) {
    static constexpr const char* kOp = "set_fade_curve";
    // Yabanci thread loud WRONG_THREAD (ownership tier); olu handle sessiz
    // INVALID_HANDLE (damgalanacak motor yok).
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, kOp);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;

    if (curve != 0 && curve != 1) {
        invokeNoexcept([&] {
            if (auto engine = toEngineChecked(handle)) {
                if (auto* ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Fade curve must be 0 (Linear) or 1 (EqualPower); "
                                  "failing closed without touching the live mixer",
                                  kOp, "");
                }
            }
        });
        return ROWL_RESULT_INVALID_ARGUMENT;
    }

    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return ROWL_RESULT_INVALID_HANDLE;
        auto* audio = checked->getAudio();
        if (!audio) return ROWL_RESULT_UNKNOWN_ERROR;
        audio->setFadeCurve(static_cast<Rowl::Audio::FadeCurve>(curve));
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

ROWL_API RowlEngine_ResultCode RowlEngine_GetFadeCurveChecked(
    RowlEngineHandle handle, int* outValue) {
    static constexpr const char* kOp = "get_fade_curve";
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, kOp);
        return ROWL_RESULT_WRONG_THREAD;
    }
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;

    if (outValue == nullptr) {
        invokeNoexcept([&] {
            if (auto engine = toEngineChecked(handle)) {
                if (auto* ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Output pointer is null; failing closed",
                                  kOp, "");
                }
            }
        });
        return ROWL_RESULT_INVALID_ARGUMENT;
    }

    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return ROWL_RESULT_INVALID_HANDLE;
        const auto* audio = checked ? checked->getAudio() : nullptr;
        if (!audio) return ROWL_RESULT_UNKNOWN_ERROR;
        *outValue = static_cast<int>(audio->fadeCurve());
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

}  // extern "C"
