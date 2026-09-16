/**
 * c_api_contract.cpp
 *
 * Version/capability negotiation and caller-owned UTF-8 output buffers.
 * This is the additive foundation for new result-coded C APIs; legacy entry
 * points remain in their subsystem translation units as compatibility wrappers.
 */

#include "c_api_internal.hpp"
#include "rowl/platform/user_data_directories.hpp"

#include <string>

namespace {

constexpr RowlEngine_ApiVersion kApiVersion{
    ROWL_ENGINE_C_API_VERSION_MAJOR,
    ROWL_ENGINE_C_API_VERSION_MINOR,
    ROWL_ENGINE_C_API_VERSION_PATCH};
constexpr uint64_t kCapabilities =
    ROWL_ENGINE_CAPABILITY_RESULT_CODES |
    ROWL_ENGINE_CAPABILITY_CALLER_BUFFERS |
    ROWL_ENGINE_CAPABILITY_USER_DATA_DIRECTORIES |
    ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT |
    ROWL_ENGINE_CAPABILITY_PLAYER_LOOP |
    ROWL_ENGINE_CAPABILITY_SAVE_METADATA |
    ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES |
    ROWL_ENGINE_CAPABILITY_LOCALIZATION |
    ROWL_ENGINE_CAPABILITY_RICH_TEXT_MARKUP |
    ROWL_ENGINE_CAPABILITY_TEXT_SHAPING |
    ROWL_ENGINE_CAPABILITY_ACCESSIBILITY |
    ROWL_ENGINE_CAPABILITY_LONG_AUDIO_CONTRACT |
    ROWL_ENGINE_CAPABILITY_CAMERA_ROTATION_IGNORED |
    ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING |
    ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY;

RowlEngine_ResultCode copyHostPath(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize, bool profile) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto path = profile ? engine->getProfileDirectoryPath()
                                  : engine->getSaveDirectoryPath();
        return copyUtf8ToCaller(Rowl::Platform::pathToUtf8(path), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_GetApiVersion(
    RowlEngine_ApiVersion* outVersion) {
    if (!outVersion) return ROWL_RESULT_INVALID_ARGUMENT;
    *outVersion = kApiVersion;
    return ROWL_RESULT_OK;
}

RowlEngine_ResultCode RowlEngine_GetCapabilities(uint64_t* outCapabilities) {
    if (!outCapabilities) return ROWL_RESULT_INVALID_ARGUMENT;
    *outCapabilities = kCapabilities;
    return ROWL_RESULT_OK;
}

RowlEngine_ResultCode RowlEngine_GetSaveDirectoryUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    return copyHostPath(handle, buffer, bufferSize, outRequiredSize, false);
}

RowlEngine_ResultCode RowlEngine_GetProfileDirectoryUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    return copyHostPath(handle, buffer, bufferSize, outRequiredSize, true);
}

} // extern "C"
