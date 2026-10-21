/**
 * c_api_game_data.cpp — D01b per-game data C API (thin wrappers).
 *
 * The layout/migration logic lives in state/game_data_layout.cpp. This unit
 * only wires it into SetProjectDirectory and exposes the results.
 */
#include "c_api_internal.hpp"

#include "rowl/platform/user_data_directories.hpp"
#include "rowl/state/game_data_layout.hpp"

void applyProjectGameData(Rowl::Core::Engine& engine, const std::filesystem::path& projectRoot) {
    try {
        auto layout = Rowl::State::prepareGameData(
            Rowl::Platform::resolveUserDataRoot(), engine.getGameIdentity(), projectRoot);
        engine.setSaveDirectory(Rowl::Platform::pathToUtf8(layout.saveDirectory));
        engine.setGameDataLayout(std::move(layout));
    } catch (...) {
        engine.setSaveDirectory(Rowl::Platform::pathToUtf8(projectRoot / "saves"));
    }
}

void reportProjectGameDataDiagnostic(Rowl::Core::Engine& engine) {
    // Derived ids keep project-local saves by design (diagnostic only); an
    // authored id that could not use its per-game dir, or a partial
    // migration, is reported through the usual last-result error.
    if (engine.getGameIdentity().source == Rowl::Platform::GameIdentitySource::Derived) return;
    const auto& message = engine.getGameDataLayout().diagnostic;
    auto* ctx = engine.getContext();
    if (message.empty() || !ctx || !ctx->getLastResult().isOk()) return;
    ctx->setError(Rowl::Core::RuntimeErrorCode::IoError, message, "set_project_directory", "");
}

namespace {

RowlEngine_ResultCode copyDataText(RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
                                   uint32_t* outRequiredSize, bool migration) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto engine = toEngineChecked(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto& layout = engine->getGameDataLayout();
        const std::string text = migration ? layout.migrationJson
                                           : Rowl::Platform::pathToUtf8(layout.dataDirectory);
        return copyUtf8ToCaller(text, buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

} // namespace

extern "C" {

RowlEngine_ResultCode RowlEngine_GetGameDataDirectoryUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return copyDataText(handle, buffer, bufferSize, outRequiredSize, false);
}

RowlEngine_ResultCode RowlEngine_GetLastDataMigrationJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize) {
    return copyDataText(handle, buffer, bufferSize, outRequiredSize, true);
}

} // extern "C"
