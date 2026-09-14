/**
 * test_c_api_contract.cpp — additive version/result/caller-buffer ABI contract.
 */
#include "rowl_test_harness.hpp"

namespace {

class ContractPlatformHost final : public Rowl::Platform::PlatformHost {
public:
    std::unique_ptr<std::istream> openAssetStream(const std::string&) override {
        return nullptr;
    }
    std::filesystem::path writableSavePath() const override { return savePath; }
    std::filesystem::path writableProfilePath() const override { return profilePath; }
    Rowl::Platform::LifecycleState lifecycleState() const override {
        return Rowl::Platform::LifecycleState::Active;
    }
    std::vector<Rowl::Platform::RuntimeInputEvent> takeInputEvents() override { return {}; }
    Rowl::Platform::RenderSurface renderSurface() const override { return {}; }
    Rowl::Platform::AudioFocus audioFocus() const override {
        return Rowl::Platform::AudioFocus::Granted;
    }

    std::filesystem::path savePath;
    std::filesystem::path profilePath;
};

std::string readDirectory(
    RowlEngineHandle handle,
    RowlEngine_ResultCode (*getter)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    if (getter(handle, nullptr, 0, &required) != ROWL_RESULT_OK || required < 2) {
        std::cerr << "Caller-buffer size query failed" << std::endl;
        exit(1);
    }
    std::vector<char> undersized(required - 1, 'x');
    uint32_t repeatedRequired = 0;
    if (getter(handle, undersized.data(), static_cast<uint32_t>(undersized.size()),
               &repeatedRequired) != ROWL_RESULT_BUFFER_TOO_SMALL ||
        repeatedRequired != required || undersized.front() != '\0') {
        std::cerr << "Caller-buffer undersized contract failed" << std::endl;
        exit(1);
    }
    std::vector<char> buffer(required, '\0');
    if (getter(handle, buffer.data(), static_cast<uint32_t>(buffer.size()),
               &repeatedRequired) != ROWL_RESULT_OK || repeatedRequired != required ||
        buffer.back() != '\0') {
        std::cerr << "Caller-buffer exact-size copy failed" << std::endl;
        exit(1);
    }
    return std::string(buffer.data());
}

} // namespace

void test_c_api_contract() {
    TEST_SECTION("Versioned Result-Coded C API Contract");

    static_assert(static_cast<int>(Rowl::Core::RuntimeErrorCode::BufferTooSmall) ==
                  ROWL_RESULT_BUFFER_TOO_SMALL);
    RowlEngine_ApiVersion version{};
    uint64_t capabilities = 0;
    if (RowlEngine_GetApiVersion(nullptr) != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_GetApiVersion(&version) != ROWL_RESULT_OK ||
        version.major != ROWL_ENGINE_C_API_VERSION_MAJOR ||
        version.minor != ROWL_ENGINE_C_API_VERSION_MINOR ||
        version.patch != ROWL_ENGINE_C_API_VERSION_PATCH ||
        RowlEngine_GetCapabilities(nullptr) != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
        (capabilities & ROWL_ENGINE_CAPABILITY_RESULT_CODES) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_CALLER_BUFFERS) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_USER_DATA_DIRECTORIES) == 0) {
        std::cerr << "API version/capability negotiation failed" << std::endl;
        exit(1);
    }

    uint32_t required = 77;
    if (RowlEngine_GetSaveDirectoryUtf8(nullptr, nullptr, 0, &required) !=
            ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_GetSaveDirectoryUtf8(nullptr, nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_HANDLE) {
        std::cerr << "Directory query null-handle contract failed" << std::endl;
        exit(1);
    }

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) exit(1);
    auto* engine = Rowl::Core::testEngineFromHandle(handle);
    auto host = std::make_shared<ContractPlatformHost>();
    const auto unicodeRoot = std::filesystem::temp_directory_path() /
        std::filesystem::path(u8"Rowl-Kayıt-玩家") /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    host->savePath = unicodeRoot / "saves";
    host->profilePath = unicodeRoot / "profiles";
    engine->getContext()->setPlatformHost(host);

    if (RowlEngine_GetSaveDirectoryUtf8(handle, nullptr, 1, &required) !=
            ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_GetSaveDirectoryUtf8(handle, nullptr, 0, nullptr) !=
            ROWL_RESULT_INVALID_ARGUMENT) {
        std::cerr << "Directory query invalid-argument contract failed" << std::endl;
        exit(1);
    }

    if (readDirectory(handle, RowlEngine_GetSaveDirectoryUtf8) !=
            Rowl::Platform::pathToUtf8(host->savePath) ||
        readDirectory(handle, RowlEngine_GetProfileDirectoryUtf8) !=
            Rowl::Platform::pathToUtf8(host->profilePath)) {
        std::cerr << "UTF-8 PlatformHost paths changed at the C ABI" << std::endl;
        exit(1);
    }

    if (RowlEngine_Init(handle, 64, 64, 0) != 1) {
        std::cerr << "C API contract fixture could not claim its owner thread" << std::endl;
        exit(1);
    }
    RowlEngine_ResultCode wrongThreadResult = ROWL_RESULT_OK;
    std::thread wrongThread([&] {
        uint32_t wrongThreadRequired = 0;
        wrongThreadResult = RowlEngine_GetSaveDirectoryUtf8(
            handle, nullptr, 0, &wrongThreadRequired);
    });
    wrongThread.join();
    if (wrongThreadResult != ROWL_RESULT_INVALID_HANDLE) {
        std::cerr << "Result-coded API did not reject a wrong-thread handle" << std::endl;
        exit(1);
    }

    if (RowlEngine_SaveGameSlotResult(handle, 0) != ROWL_RESULT_OK ||
        !std::filesystem::is_regular_file(host->savePath / "save_slot_0.json") ||
        RowlEngine_LoadGameSlotResult(handle, 0) != ROWL_RESULT_OK ||
        RowlEngine_SaveGameSlot(handle, 1) != 1 ||
        !std::filesystem::is_regular_file(host->savePath / "save_slot_1.json")) {
        std::cerr << "Unicode result-coded save/load or legacy success wrapper failed" << std::endl;
        exit(1);
    }

    if (RowlEngine_SaveGameSlotResult(nullptr, 0) != ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_LoadGameSlotResult(nullptr, 0) != ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_SaveGameSlotResult(handle, -1) != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_SaveGameSlot(handle, -1) != 0 ||
        RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_INVALID_ARGUMENT) {
        std::cerr << "Result-coded save/load or legacy wrapper contract failed" << std::endl;
        exit(1);
    }

    RowlEngine_Destroy(handle);
    std::error_code cleanupError;
    std::filesystem::remove_all(unicodeRoot, cleanupError);
    TEST_PASS("version, capabilities, UTF-8 caller buffers, and legacy wrappers");
}
