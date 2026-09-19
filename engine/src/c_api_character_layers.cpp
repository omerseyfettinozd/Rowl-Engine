/**
 * c_api_character_layers.cpp
 *
 * Faz 5 Dilim 3 — katmanli karakter C API yuzeyi
 * (ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS = 32768). Eklemeli; eski giris
 * noktalarina dokunulmaz. engine.cpp / window.cpp buyumez: tum kurallar
 * Rowl::Scene::CharacterLayers / CharacterPresetLibrary'dadir, burasi
 * yalnizca ABI siniridir (handle dogrulama + giris tasiyicisi +
 * boyut-sorgu/cagiran-tamponu + istisna yutma).
 *
 * Durum handle basina tutulur (bellekte; kalicilik C# tarafinda JSON).
 * Oda: slot asset set/get, opaklik/gorunurluk, expression uygula (isimle),
 * preset listesi JSON, draw-list JSON (sira gozlemlenebilirligi), hata sorgusu.
 * Hepsi fail-closed: null-handle -> INVALID_HANDLE (0/"" tasiyicilarda),
 * bilinmeyen slot/preset -> INVALID_ARGUMENT, asiri buyuk giris red.
 */

#include "c_api_internal.hpp"
#include "rowl/scene/character_layers.hpp"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {

/// C API giris tasiyicisi (byte), Dilim 1-2 deseni: 256 KiB + 1 cap.
constexpr std::size_t kCharacterInputLimitBytes = 262144;

struct CharacterRuntime {
    Rowl::Scene::CharacterLayers layers;
    Rowl::Scene::CharacterPresetLibrary presets;
    std::string lastError;
};

std::mutex g_characterMutex;
std::unordered_map<RowlEngineHandle, CharacterRuntime> g_characterStates;

RowlEngine_ResultCode checkCharacterInput(const char* input, std::string_view& out) noexcept {
    if (input == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    const void* terminator =
        std::memchr(input, '\0', kCharacterInputLimitBytes + 1u);
    if (terminator == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    out = std::string_view(input,
                            static_cast<const char*>(terminator) - input);
    return ROWL_RESULT_OK;
}

RowlEngine_ResultCode checkSlotName(const char* slotName,
                                    Rowl::Scene::CharacterSlot& out) noexcept {
    std::string_view view;
    const RowlEngine_ResultCode inputCheck = checkCharacterInput(slotName, view);
    if (inputCheck != ROWL_RESULT_OK) return inputCheck;
    const auto slot = Rowl::Scene::characterSlotFromName(view);
    if (!slot) return ROWL_RESULT_INVALID_ARGUMENT;
    out = *slot;
    return ROWL_RESULT_OK;
}

RowlEngine_ResultCode checkPresetName(const char* presetName,
                                      std::string_view& out) noexcept {
    const RowlEngine_ResultCode inputCheck =
        checkCharacterInput(presetName, out);
    if (inputCheck != ROWL_RESULT_OK) return inputCheck;
    if (out.empty() || out.size() > Rowl::Scene::kMaxCharacterPresetNameBytes ||
        out.find('\0') != std::string_view::npos) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    return ROWL_RESULT_OK;
}

} // namespace

// D2 (#140): prefetch TU'sundaki ikiz — Destroy yolunda lifecycle
// TU'su buradan temizler; Create/kullan/Destroy sizintisi kapanir.
void clearCharacterStatesForHandle(RowlEngineHandle handle) noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
    } catch (...) {
    }
}

extern "C" {

RowlEngine_ResultCode RowlEngine_SetCharacterSlotAsset(
    RowlEngineHandle handle, const char* slotName, const char* assetPath) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    const RowlEngine_ResultCode slotCheck = checkSlotName(slotName, slot);
    if (slotCheck != ROWL_RESULT_OK) return slotCheck;
    std::string_view assetView;
    const RowlEngine_ResultCode assetCheck =
        checkCharacterInput(assetPath, assetView);
    if (assetCheck != ROWL_RESULT_OK) return assetCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        if (!runtime.layers.setSlotAsset(slot, assetView)) {
            runtime.lastError = runtime.layers.lastError();
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetCharacterSlotAssetUtf8(
    RowlEngineHandle handle, const char* slotName, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    const RowlEngine_ResultCode slotCheck = checkSlotName(slotName, slot);
    if (slotCheck != ROWL_RESULT_OK) return slotCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        const std::size_t index = static_cast<std::size_t>(slot);
        (void)index;
        return copyUtf8ToCaller(runtime.layers.slotState(slot).asset, buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_SetCharacterSlotOpacity(
    RowlEngineHandle handle, const char* slotName, float opacity) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    const RowlEngine_ResultCode slotCheck = checkSlotName(slotName, slot);
    if (slotCheck != ROWL_RESULT_OK) return slotCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        if (!runtime.layers.setSlotOpacity(slot, opacity)) {
            runtime.lastError = runtime.layers.lastError();
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetCharacterSlotOpacity(
    RowlEngineHandle handle, const char* slotName, float* outOpacity) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    if (outOpacity == nullptr) return ROWL_RESULT_INVALID_ARGUMENT;
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    const RowlEngine_ResultCode slotCheck = checkSlotName(slotName, slot);
    if (slotCheck != ROWL_RESULT_OK) return slotCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        *outOpacity = runtime.layers.slotState(slot).opacity;
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_SetCharacterSlotVisible(
    RowlEngineHandle handle, const char* slotName, int visible) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    const RowlEngine_ResultCode slotCheck = checkSlotName(slotName, slot);
    if (slotCheck != ROWL_RESULT_OK) return slotCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        runtime.layers.setSlotVisible(slot, visible != 0);
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_IsCharacterSlotVisible(RowlEngineHandle handle,
                                      const char* slotName) {
    Rowl::Scene::CharacterSlot slot = Rowl::Scene::CharacterSlot::Body;
    if (checkSlotName(slotName, slot) != ROWL_RESULT_OK) return 0;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return 0;
    }
    return invokeNoexcept<int>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        return runtime.layers.slotState(slot).visible ? 1 : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_RegisterCharacterPreset(
    RowlEngineHandle handle, const char* presetName,
    const char* expressionJsonUtf8) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view nameView;
    const RowlEngine_ResultCode nameCheck =
        checkPresetName(presetName, nameView);
    if (nameCheck != ROWL_RESULT_OK) return nameCheck;
    std::string_view jsonView;
    const RowlEngine_ResultCode jsonCheck =
        checkCharacterInput(expressionJsonUtf8, jsonView);
    if (jsonCheck != ROWL_RESULT_OK) return jsonCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        nlohmann::json expression;
        try {
            expression = nlohmann::json::parse(jsonView);
        } catch (...) {
            runtime.lastError = "expression JSON parse failed";
            return ROWL_RESULT_PARSE_ERROR;
        }
        std::string error;
        if (!runtime.presets.registerPreset(nameView, expression, error)) {
            runtime.lastError = error;
            return ROWL_RESULT_VALIDATION_ERROR;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_ApplyCharacterExpression(
    RowlEngineHandle handle, const char* presetName) {
    // A4-tur1 (siralama): handle-once — olu-handle'da arg'lara bakilmadan
    // INVALID_HANDLE (story-TU konvansiyonu); erase-hijyeni korunur.
    if (!toEngineChecked(handle)) {
        std::lock_guard<std::mutex> lock(g_characterMutex);
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    std::string_view nameView;
    const RowlEngine_ResultCode nameCheck =
        checkPresetName(presetName, nameView);
    if (nameCheck != ROWL_RESULT_OK) return nameCheck;
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        const std::string name(nameView.data(), nameView.size());
        if (!runtime.presets.hasPreset(name)) {
            runtime.lastError = std::string("unknown preset '") + name + "'";
            return ROWL_RESULT_INVALID_ARGUMENT;
        }
        std::string error;
        if (!runtime.presets.applyExpression(name, runtime.layers, error)) {
            runtime.lastError = error;
            return ROWL_RESULT_VALIDATION_ERROR;
        }
        runtime.lastError.clear();
        return ROWL_RESULT_OK;
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetCharacterPresetListJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        return copyUtf8ToCaller(runtime.presets.presetListJson().dump(),
                                buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetCharacterDrawListJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : runtime.layers.composeDrawList()) {
            arr.push_back({{"slot", Rowl::Scene::characterSlotName(item.slot)},
                           {"asset", item.asset},
                           {"opacity", item.opacity}});
        }
        return copyUtf8ToCaller(arr.dump(), buffer, bufferSize,
                                outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

RowlEngine_ResultCode RowlEngine_GetLastCharacterErrorUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    std::lock_guard<std::mutex> lock(g_characterMutex);
    if (!isLiveHandle(handle)) {
        g_characterStates.erase(handle);
        return ROWL_RESULT_INVALID_HANDLE;
    }
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        CharacterRuntime& runtime = g_characterStates[handle];
        const std::string& error = runtime.lastError.empty()
                                       ? runtime.layers.lastError()
                                       : runtime.lastError;
        return copyUtf8ToCaller(error, buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // extern "C"
