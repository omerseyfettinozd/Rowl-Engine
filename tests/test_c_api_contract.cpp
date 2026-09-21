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
        (capabilities & ROWL_ENGINE_CAPABILITY_USER_DATA_DIRECTORIES) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_PLAYER_LOOP) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_SAVE_METADATA) == 0 ||
        (capabilities & ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES) == 0) {
        std::cerr << "API version/capability negotiation failed" << std::endl;
        exit(1);
    }

    // B1: MSDF capability biti + enum sabit-genişlik kontratı. Bit değeri
    // yalnız-eklemeli (262144); varlığı build-tipine bağlıdır (normalde 1,
    // shaderless-fallback'ta 0 — ROWL_TEST_GPU_MSDF_EXPECTED CMake'ten gelir).
    // Maskede bilinmeyen bit olmamalıdır (rezerv-kaçak avcısı).
    if (ROWL_ENGINE_CAPABILITY_MSDF_RENDER != UINT64_C(262144)) {
        std::cerr << "MSDF capability bit value drifted" << std::endl;
        exit(1);
    }
    if (sizeof(RowlEngine_ResultCode) != 4) {
        std::cerr << "RowlEngine_ResultCode is not 32-bit" << std::endl;
        exit(1);
    }
#if ROWL_TEST_GPU_MSDF_EXPECTED
    if ((capabilities & ROWL_ENGINE_CAPABILITY_MSDF_RENDER) == 0) {
        std::cerr << "MSDF capability bit missing in shader build" << std::endl;
        exit(1);
    }
#else
    if ((capabilities & ROWL_ENGINE_CAPABILITY_MSDF_RENDER) != 0) {
        std::cerr << "MSDF capability bit set in shaderless build" << std::endl;
        exit(1);
    }
#endif
    constexpr uint64_t kKnownCapabilities =
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
        ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY |
        ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS |
        ROWL_ENGINE_CAPABILITY_PREFETCH_CHAPTERS |
        ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE |
        ROWL_ENGINE_CAPABILITY_MSDF_RENDER;
    if ((capabilities & ~kKnownCapabilities) != 0) {
        std::cerr << "Unknown capability bits in mask" << std::endl;
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
    // Tur-7: Windows CI aborts silently inside this section (bare
    // "abort() has been called" after the Init story log). Phase markers
    // on stderr pin the exact call; unitbuf in test_main flushes them.
    std::cerr << "[contract] phase 1: init ok" << std::endl;
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

    // Graph vNext chapter surface: empty on a graph-free engine, populated
    // after a v5 load. Uses its own temp document so save-slot fixtures below
    // keep their pristine engine state.
    {
        uint32_t chapterCount = 77;
        if (RowlEngine_GetChapterCount(nullptr, &chapterCount) != ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetChapterCount(handle, nullptr) != ROWL_RESULT_INVALID_ARGUMENT ||
            RowlEngine_GetCurrentChapterIdUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetChapterIdAtUtf8(nullptr, 0, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE) {
            std::cerr << "Chapter query null-handle contract failed" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetChapterCount(handle, &chapterCount) != ROWL_RESULT_OK ||
            chapterCount != 0) {
            std::cerr << "Graph-free engine must report zero chapters" << std::endl;
            exit(1);
        }
        uint32_t currentRequired = 0;
        if (RowlEngine_GetCurrentChapterIdUtf8(handle, nullptr, 0, &currentRequired) !=
                ROWL_RESULT_OK ||
            currentRequired != 1) {
            std::cerr << "Graph-free engine must report an empty current chapter" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetChapterIdAtUtf8(handle, 0, nullptr, 0, &currentRequired) !=
            ROWL_RESULT_INVALID_ARGUMENT) {
            std::cerr << "Chapter index over an empty table must be invalid" << std::endl;
            exit(1);
        }
        std::cerr << "[contract] phase 2: chapter empty-queries ok" << std::endl;

        const std::filesystem::path chapterGraph = unicodeRoot / "vnext_graph.json";
        {
            std::error_code dirError;
            std::filesystem::create_directories(unicodeRoot, dirError);
            if (dirError) {
                std::cerr << "Chapter fixture directory could not be created" << std::endl;
                exit(1);
            }
            std::ofstream graphFile(chapterGraph);
            graphFile << R"({
                "format_version": 5, "start_node_id": 101,
                "nodes": [
                    {"id": 101, "chapter_id": "ch1", "next_nodes": [{"id": 102}]},
                    {"id": 102, "chapter_id": "ch1", "next_nodes": [{"id": 103}]},
                    {"id": 103, "chapter_id": "ch2"}
                ],
                "subgraphs": [
                    {"id": "sg1", "entry_node_id": 101,
                     "exit_node_ids": [102], "node_ids": [101, 102]}
                ],
                "chapters": [
                    {"id": "ch1", "title": "Arrivals", "order": 1},
                    {"id": "ch2", "title": "Departures", "order": 0}
                ]
            })";
        }
        // Narrow-path reality: on Windows path::string() encodes in the
        // ANSI codepage, so the CJK fixture dir is unrepresentable and the
        // conversion THROWS (tur-8: this exact uncaught throw killed
        // Windows CI as SEH 0xE06D7363 between phase 2 and 2b). UTF-8
        // bytes never throw — the engine must still survive them
        // gracefully (no load, zero chapters, readable story-graph
        // error), which is exactly what the narrowThrew branch asserts.
        bool narrowThrew = false;
        std::string chapterNarrow;
        try {
            chapterNarrow = chapterGraph.string();
        } catch (const std::exception& narrowError) {
            std::cerr << "[contract] phase 2b-note: narrow conversion threw ("
                      << narrowError.what() << "); using UTF-8 bytes" << std::endl;
            narrowThrew = true;
            const std::u8string utf8narrow = chapterGraph.u8string();
            chapterNarrow.assign(
                reinterpret_cast<const char*>(utf8narrow.data()), utf8narrow.size());
        }
        {
            std::error_code probeEc;
            std::cerr << "[contract] phase 2b: graph narrow bytes=" << chapterNarrow.size()
                      << " exists=" << std::filesystem::exists(chapterGraph, probeEc)
                      << " ec=" << probeEc.value() << std::endl;
        }
        RowlEngine_LoadStoryGraph(handle, chapterNarrow.c_str());
        std::cerr << "[contract] phase 3: vnext load returned" << std::endl;
        if (narrowThrew) {
            // Tur-12: the engine resolves the UTF-8 C ABI path losslessly
            // (pathFromUtf8), so the CJK graph loads on Windows exactly
            // like on Linux — a non-ASCII project dir is a supported
            // product case, not a graceful-failure case.
            uint32_t narrowCount = 77;
            if (RowlEngine_GetChapterCount(handle, &narrowCount) != ROWL_RESULT_OK ||
                narrowCount != 2) {
                std::cerr << "UTF-8 graph path must load two chapters" << std::endl;
                exit(1);
            }
            // The graceful contract survives for genuinely-missing files:
            // the active graph is preserved and an error stays observable.
            const std::u8string missingU8 =
                (unicodeRoot / "rowl-missing-graph.json").u8string();
            const std::string missingNarrow(
                reinterpret_cast<const char*>(missingU8.data()), missingU8.size());
            RowlEngine_LoadStoryGraph(handle, missingNarrow.c_str());
            uint32_t gracefulCount = 77;
            if (RowlEngine_GetChapterCount(handle, &gracefulCount) != ROWL_RESULT_OK ||
                gracefulCount != 2) {
                std::cerr << "Missing graph path must preserve the active graph" << std::endl;
                exit(1);
            }
            const char* graphError = RowlEngine_GetLastStoryGraphError(handle);
            if (!graphError || !graphError[0]) {
                std::cerr << "Missing graph path must report a story-graph error" << std::endl;
                exit(1);
            }
            std::cerr << "[contract] phase 3b: UTF-8 path loaded, missing path handled gracefully"
                      << std::endl;
        } else if (RowlEngine_GetChapterCount(handle, &chapterCount) != ROWL_RESULT_OK ||
            chapterCount != 2) {
            std::cerr << "Loaded v5 graph must report two chapters" << std::endl;
            exit(1);
        }
        // Order-sorted: ch2 (order 0) precedes ch1 (order 1).
        // Tur-12: the graph loads on every platform now (UTF-8 path),
        // so this block always runs.
        {
            uint32_t atRequired = 0;
            if (RowlEngine_GetChapterIdAtUtf8(handle, 0, nullptr, 0, &atRequired) !=
                    ROWL_RESULT_OK ||
                atRequired != 4) {
                std::cerr << "Chapter index size query failed" << std::endl;
                exit(1);
            }
            std::vector<char> tiny(2, 'x');
            uint32_t tinyRequired = 0;
            if (RowlEngine_GetChapterIdAtUtf8(handle, 0, tiny.data(), 2, &tinyRequired) !=
                    ROWL_RESULT_BUFFER_TOO_SMALL ||
                tinyRequired != 4 || tiny.front() != '\0') {
                std::cerr << "Chapter index undersized contract failed" << std::endl;
                exit(1);
            }
            std::vector<char> slot(atRequired, '\0');
            uint32_t slotRequired = 0;
            if (RowlEngine_GetChapterIdAtUtf8(handle, 0, slot.data(),
                                              static_cast<uint32_t>(slot.size()),
                                              &slotRequired) != ROWL_RESULT_OK ||
                std::string(slot.data()) != "ch2") {
                std::cerr << "Chapter order sorting failed at the C ABI" << std::endl;
                exit(1);
            }
            if (RowlEngine_GetChapterIdAtUtf8(handle, 1, slot.data(),
                                              static_cast<uint32_t>(slot.size()),
                                              &slotRequired) != ROWL_RESULT_OK ||
                std::string(slot.data()) != "ch1") {
                std::cerr << "Chapter index 1 did not resolve" << std::endl;
                exit(1);
            }
            if (RowlEngine_GetCurrentChapterIdUtf8(handle, slot.data(),
                                                   static_cast<uint32_t>(slot.size()),
                                                   &slotRequired) != ROWL_RESULT_OK ||
                std::string(slot.data()) != "ch1") {
                std::cerr << "Current chapter must follow the start node" << std::endl;
                exit(1);
            }
            if (RowlEngine_GetChapterIdAtUtf8(handle, 2, slot.data(),
                                              static_cast<uint32_t>(slot.size()),
                                              &slotRequired) != ROWL_RESULT_INVALID_ARGUMENT) {
                std::cerr << "Out-of-range chapter index must be invalid" << std::endl;
                exit(1);
            }
        }
    }

    if (RowlEngine_SaveGameSlotResult(handle, 0) != ROWL_RESULT_OK ||
        !std::filesystem::is_regular_file(host->savePath / "save_slot_0.json") ||
        RowlEngine_LoadGameSlotResult(handle, 0) != ROWL_RESULT_OK ||
        RowlEngine_SaveGameSlot(handle, 1) != 1 ||
        !std::filesystem::is_regular_file(host->savePath / "save_slot_1.json")) {
        std::cerr << "Unicode result-coded save/load or legacy success wrapper failed" << std::endl;
        exit(1);
    }

    // Faz 2 player loop: the presented dialogues expose their content ids
    // for read tracking, and the backlog carries them per entry. Legacy
    // lines without an id contribute an empty string (fail-closed hosts
    // never treat "" as read).
    {
        if (RowlEngine_GetActiveDialogueContentIdsJson(
                nullptr, nullptr, 0, &required) != ROWL_RESULT_INVALID_HANDLE) {
            std::cerr << "Active content ids null-handle contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetPlayState(handle, 1);
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type": "dialogue", "enabled": true, "data": {
                "speaker": "Evelyn", "dialogue": "Remember me.",
                "content_id": "11111111-2222-3333-4444-555555555555"}},
            {"type": "dialogue", "enabled": true, "data": {
                "speaker": "Mina", "dialogue": "Legacy line without an id."}}
        ])");
        const std::string activeIds =
            readDirectory(handle, RowlEngine_GetActiveDialogueContentIdsJson);
        if (activeIds != "[\"11111111-2222-3333-4444-555555555555\",\"\"]") {
            std::cerr << "Active dialogue content ids mismatch: " << activeIds << std::endl;
            exit(1);
        }
        const char* historyJson = RowlEngine_GetDialogueHistoryJson(handle);
        const std::string history = historyJson ? historyJson : "";
        if (history.find("\"content_id\":\"11111111-2222-3333-4444-555555555555\"") ==
                std::string::npos ||
            history.find("Legacy line without an id.") == std::string::npos) {
            std::cerr << "Backlog entries must carry their content ids: " << history << std::endl;
            exit(1);
        }
        RowlEngine_SetPlayState(handle, 0);
    }

    if (RowlEngine_SaveGameSlotResult(nullptr, 0) != ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_LoadGameSlotResult(nullptr, 0) != ROWL_RESULT_INVALID_HANDLE ||
        RowlEngine_SaveGameSlotResult(handle, -1) != ROWL_RESULT_INVALID_ARGUMENT ||
        RowlEngine_SaveGameSlot(handle, -1) != 0 ||
        RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_INVALID_ARGUMENT) {
        std::cerr << "Result-coded save/load or legacy wrapper contract failed" << std::endl;
        exit(1);
    }

    // Faz 2 Dilim 4: slot display metadata reads the file without loading
    // it into the live story.
    {
        if (RowlEngine_GetSaveSlotMetadataJson(
                nullptr, 0, nullptr, 0, &required) != ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetSaveSlotMetadataJson(
                handle, -1, nullptr, 0, &required) != ROWL_RESULT_INVALID_ARGUMENT ||
            RowlEngine_GetSaveSlotMetadataJson(
                handle, 101, nullptr, 0, &required) != ROWL_RESULT_INVALID_ARGUMENT ||
            RowlEngine_GetSaveSlotMetadataJson(
                handle, 77, nullptr, 0, &required) != ROWL_RESULT_FILE_NOT_FOUND) {
            std::cerr << "Slot metadata error contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetPlayState(handle, 1);
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type": "dialogue", "enabled": true, "data": {
                "speaker": "Evelyn", "dialogue": "Hi.",
                "content_id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"}}
        ])");
        RowlEngine_Step(handle, 1.0f / 60.0f);
        if (RowlEngine_SaveGameSlot(handle, 7) != 1) {
            std::cerr << "Metadata fixture slot could not be saved" << std::endl;
            exit(1);
        }
        uint32_t metaRequired = 0;
        if (RowlEngine_GetSaveSlotMetadataJson(handle, 7, nullptr, 0, &metaRequired) !=
            ROWL_RESULT_OK) {
            std::cerr << "Slot metadata size query failed" << std::endl;
            exit(1);
        }
        std::vector<char> metaBuffer(metaRequired, '\0');
        uint32_t metaRepeated = 0;
        if (RowlEngine_GetSaveSlotMetadataJson(
                handle, 7, metaBuffer.data(),
                static_cast<uint32_t>(metaBuffer.size()), &metaRepeated) != ROWL_RESULT_OK) {
            std::cerr << "Slot metadata fetch failed" << std::endl;
            exit(1);
        }
        const std::string meta(metaBuffer.data());
        if (meta.find("\"chapter_id\":\"ch1\"") == std::string::npos ||
            meta.find("\"chapter_title\":\"Arrivals\"") == std::string::npos ||
            meta.find("\"summary\":\"Legacy line without an id.\"") == std::string::npos ||
            meta.find("\"saved_at\":\"20") == std::string::npos ||
            meta.find("\"playtime_seconds\":") == std::string::npos ||
            meta.find("\"has_thumbnail\":true") == std::string::npos ||
            meta.find("\"thumbnail_width\":64") == std::string::npos ||
            meta.find("\"thumbnail_png_base64\":\"iVBOR") == std::string::npos) {
            std::cerr << "Slot metadata payload mismatch: " << meta.substr(0, 400) << std::endl;
            exit(1);
        }
        RowlEngine_SetPlayState(handle, 0);
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "Metadata query must not disturb the live story" << std::endl;
            exit(1);
        }
    }

    // Faz 2 Dilim 5: presented choice buttons are queryable for selection UI.
    {
        if (RowlEngine_GetChoiceCount(nullptr) != 0) {
            std::cerr << "Choice count null-handle contract failed" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetChoiceLabelAtUtf8(
                nullptr, 0, nullptr, 0, &required) != ROWL_RESULT_INVALID_HANDLE) {
            std::cerr << "Choice label null-handle contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type": "choice", "enabled": true, "data": {"options": [
                {"option_id": "left", "text": "Go left"},
                {"option_id": "right", "text": "Go right"}
            ]}}
        ])");
        if (RowlEngine_GetChoiceCount(handle) != 2) {
            std::cerr << "Presented choice count mismatch" << std::endl;
            exit(1);
        }
        const std::string firstLabel = readDirectory(
            handle,
            [](RowlEngineHandle h, char* buffer, uint32_t size, uint32_t* outRequired) {
                return RowlEngine_GetChoiceLabelAtUtf8(h, 0, buffer, size, outRequired);
            });
        if (firstLabel != "Go left") {
            std::cerr << "Presented choice label mismatch: " << firstLabel << std::endl;
            exit(1);
        }
        const std::string firstOption = readDirectory(
            handle,
            [](RowlEngineHandle h, char* buffer, uint32_t size, uint32_t* outRequired) {
                return RowlEngine_GetChoiceOptionIdAtUtf8(h, 0, buffer, size, outRequired);
            });
        if (firstOption != "left") {
            std::cerr << "Presented choice option id mismatch: " << firstOption << std::endl;
            exit(1);
        }
        if (RowlEngine_GetChoiceLabelAtUtf8(
                handle, 7, nullptr, 0, &required) != ROWL_RESULT_INVALID_ARGUMENT) {
            std::cerr << "Choice label range contract failed" << std::endl;
            exit(1);
        }
    }

    // Faz 5 Dilim 1: audio-streaming observability + volume matrix (additive).
    // Capability bit, dead-handle fail-closed vectors, and the GetLocale-style
    // caller-buffer contract (NULL/0 size query, undersized BUFFER_TOO_SMALL).
    {
        uint64_t streamingCaps = 0;
        if (RowlEngine_GetCapabilities(&streamingCaps) != ROWL_RESULT_OK ||
            (streamingCaps & ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING) == 0 ||
            ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING != UINT64_C(8192)) {
            std::cerr << "AUDIO_STREAMING capability bit 8192 must be advertised" << std::endl;
            exit(1);
        }
        if (RowlEngine_IsStreaming(nullptr) != 0 ||
            RowlEngine_GetStreamInfoJson(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetBgmVolume(nullptr) != 0.0f ||
            RowlEngine_GetAmbienceVolume(nullptr) != 0.0f ||
            RowlEngine_GetUiVolume(nullptr) != 0.0f) {
            std::cerr << "Streaming null-handle contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetAmbienceVolume(nullptr, 0.5f);
        RowlEngine_SetUiVolume(nullptr, 0.5f);
        uint32_t streamRequired = 0;
        if (RowlEngine_GetStreamInfoJson(handle, nullptr, 0, &streamRequired) !=
                ROWL_RESULT_OK ||
            streamRequired < 2) {
            std::cerr << "StreamInfo size query failed" << std::endl;
            exit(1);
        }
        std::vector<char> streamTiny(streamRequired - 1, 'x');
        uint32_t streamRepeated = 0;
        if (RowlEngine_GetStreamInfoJson(
                handle, streamTiny.data(),
                static_cast<uint32_t>(streamTiny.size()),
                &streamRepeated) != ROWL_RESULT_BUFFER_TOO_SMALL ||
            streamRepeated != streamRequired || streamTiny.front() != '\0') {
            std::cerr << "StreamInfo undersized contract failed" << std::endl;
            exit(1);
        }
        std::vector<char> streamBuffer(streamRequired, '\0');
        if (RowlEngine_GetStreamInfoJson(
                handle, streamBuffer.data(),
                static_cast<uint32_t>(streamBuffer.size()),
                &streamRepeated) != ROWL_RESULT_OK ||
            streamRepeated != streamRequired || streamBuffer.back() != '\0') {
            std::cerr << "StreamInfo exact-size copy failed" << std::endl;
            exit(1);
        }
        const std::string streamJson(streamBuffer.data());
        if (streamJson.find("\"mode\":") == std::string::npos ||
            streamJson.find("\"reason\":") == std::string::npos) {
            std::cerr << "StreamInfo JSON schema mismatch: " << streamJson << std::endl;
            exit(1);
        }
        if (RowlEngine_IsStreaming(handle) != 0) {
            std::cerr << "Stream-free engine must report IsStreaming==0" << std::endl;
            exit(1);
        }
        RowlEngine_SetAmbienceVolume(handle, 0.5f);
        RowlEngine_SetUiVolume(handle, 0.25f);
        if (std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.5f) > 1e-6 ||
            std::abs(RowlEngine_GetUiVolume(handle) - 0.25f) > 1e-6) {
            std::cerr << "Ambience/Ui volume round-trip failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetAmbienceVolume(handle, -2.0f);
        RowlEngine_SetUiVolume(handle, 7.0f);
        if (std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.0f) > 1e-6 ||
            std::abs(RowlEngine_GetUiVolume(handle) - 1.0f) > 1e-6) {
            std::cerr << "Volume clamp contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetAmbienceVolume(handle, std::numeric_limits<float>::quiet_NaN());
        RowlEngine_SetUiVolume(handle, std::numeric_limits<float>::infinity());
        if (std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.0f) > 1e-6 ||
            std::abs(RowlEngine_GetUiVolume(handle) - 1.0f) > 1e-6) {
            std::cerr << "Non-finite volume input must be ignored" << std::endl;
            exit(1);
        }
    }

    // B2a: state+pause caller-buffer varyantları (7 adet). Ölü-handle'da
    // INVALID_HANDLE; round-trip'te legacy string ile birebir eşleşme;
    // copyUtf8ToCaller kontratı (NULL/0 size-query, undersized BUFFER_TOO_SMALL).
    {
        if (RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetDialogueHistoryJsonUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetPauseMenuJsonUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetVariableUtf8(nullptr, "k", nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetVariableUtf8(handle, nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetLastResultOperationUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetLastResultMessageUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetLastResultTargetUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE) {
            std::cerr << "B2a null-handle contract failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetVariable(handle, "b2a_key", "b2a_değer-测试");
        const std::string varValue = readDirectory(
            handle,
            [](RowlEngineHandle h, char* buffer, uint32_t size, uint32_t* outRequired) {
                return RowlEngine_GetVariableUtf8(h, "b2a_key", buffer, size, outRequired);
            });
        if (varValue != "b2a_değer-测试" ||
            std::string(RowlEngine_GetVariable(handle, "b2a_key")) != varValue) {
            std::cerr << "B2a variable round-trip mismatch: " << varValue << std::endl;
            exit(1);
        }
        const std::string diagnostics = readDirectory(
            handle, RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8);
        if (diagnostics != RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)) {
            std::cerr << "B2a diagnostics round-trip mismatch" << std::endl;
            exit(1);
        }
        const std::string history = readDirectory(
            handle, RowlEngine_GetDialogueHistoryJsonUtf8);
        if (history != RowlEngine_GetDialogueHistoryJson(handle)) {
            std::cerr << "B2a history round-trip mismatch" << std::endl;
            exit(1);
        }
        const std::string pauseMenu = readDirectory(
            handle, RowlEngine_GetPauseMenuJsonUtf8);
        if (pauseMenu != RowlEngine_GetPauseMenuJson(handle)) {
            std::cerr << "B2a pause-menu round-trip mismatch" << std::endl;
            exit(1);
        }
        RowlEngine_SaveGameSlotResult(handle, 0);
        const std::string lastOp = readDirectory(
            handle, RowlEngine_GetLastResultOperationUtf8);
        const std::string lastMsg = readDirectory(
            handle, RowlEngine_GetLastResultMessageUtf8);
        const std::string lastTarget = readDirectory(
            handle, RowlEngine_GetLastResultTargetUtf8);
        if (lastOp != RowlEngine_GetLastResultOperation(handle) ||
            lastMsg != RowlEngine_GetLastResultMessage(handle) ||
            lastTarget != RowlEngine_GetLastResultTarget(handle)) {
            std::cerr << "B2a last-result round-trip mismatch" << std::endl;
            exit(1);
        }
    }

    // B2b: story+audio caller-buffer varyantları (5 adet). B2a dersi:
    // her parity-kontrolü varyant adını taşır (ara-log'suz çağrılarda
    // başarısızlık noktası kaymasın).
    {
        using Utf8Getter = RowlEngine_ResultCode (*)(RowlEngineHandle, char*, uint32_t, uint32_t*);
        auto checkParity = [&](const char* name, Utf8Getter getter, const char* legacy) {
            const std::string value = readDirectory(handle, getter);
            if (value != legacy) {
                std::cerr << "B2b " << name << " parity mismatch: [" << value
                          << "] vs [" << legacy << "]" << std::endl;
                exit(1);
            }
        };
        if (RowlEngine_GetLastStoryGraphErrorUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetSpeakerUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetDialogueUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetLastAudioErrorUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE ||
            RowlEngine_GetDialogueVoiceBlipSoundUtf8(nullptr, nullptr, 0, &required) !=
                ROWL_RESULT_INVALID_HANDLE) {
            std::cerr << "B2b null-handle contract failed" << std::endl;
            exit(1);
        }
        // Konuşmacı/diyalog dolu-içerik parity'si (speaker şeması).
        RowlEngine_UpdateSceneFromJson(handle,
            R"([{"type":"speaker","data":{"speaker":"B2b-Anlatıcı","dialogue":"B2b deneme cümlesi"}}])");
        checkParity("speaker", RowlEngine_GetSpeakerUtf8,
                    RowlEngine_GetSpeaker(handle));
        checkParity("dialogue", RowlEngine_GetDialogueUtf8,
                    RowlEngine_GetDialogue(handle));
        if (std::string(RowlEngine_GetSpeaker(handle)) != "B2b-Anlatıcı" ||
            std::string(RowlEngine_GetDialogue(handle)) != "B2b deneme cümlesi") {
            std::cerr << "B2b speaker/dialogue content mismatch" << std::endl;
            exit(1);
        }
        // Story-graph-error parity'si: kayıp dosya her zaman hata üretir.
        const std::u8string b2bMissingU8 =
            (unicodeRoot / "rowl-b2b-missing-graph.json").u8string();
        const std::string b2bMissingNarrow(
            reinterpret_cast<const char*>(b2bMissingU8.data()), b2bMissingU8.size());
        RowlEngine_LoadStoryGraph(handle, b2bMissingNarrow.c_str());
        const std::string storyError = readDirectory(
            handle, RowlEngine_GetLastStoryGraphErrorUtf8);
        if (storyError.empty() ||
            storyError != RowlEngine_GetLastStoryGraphError(handle)) {
            std::cerr << "B2b story-graph-error parity mismatch" << std::endl;
            exit(1);
        }
        // Audio-error parity'si (ortamdan bağımsız: iki kanal aynı durumu
        // okur; hata yoksa iki tarafta "" — readDirectory boşluğu kabul
        // etmez, bu yüzden burada doğrudan sözleşme kurulur).
        {
            const std::string legacyAudio(RowlEngine_GetLastAudioError(handle));
            uint32_t audioReq = 0;
            if (RowlEngine_GetLastAudioErrorUtf8(handle, nullptr, 0, &audioReq) !=
                    ROWL_RESULT_OK ||
                audioReq != static_cast<uint32_t>(legacyAudio.size() + 1)) {
                std::cerr << "B2b audio-error size-query mismatch" << std::endl;
                exit(1);
            }
            std::vector<char> audioBuf(audioReq, '\0');
            uint32_t audioRep = 0;
            if (RowlEngine_GetLastAudioErrorUtf8(
                    handle, audioBuf.data(),
                    static_cast<uint32_t>(audioBuf.size()),
                    &audioRep) != ROWL_RESULT_OK ||
                audioRep != audioReq || std::string(audioBuf.data()) != legacyAudio) {
                std::cerr << "B2b audio-error parity mismatch" << std::endl;
                exit(1);
            }
        }
        // Voice-blip parity'si (set sonrası dolu-içerik).
        RowlEngine_SetDialogueVoiceBlip(handle, "voices/b2b.wav", 1.0f, 0.1f, 2, 0, 0);
        checkParity("voice-blip-sound", RowlEngine_GetDialogueVoiceBlipSoundUtf8,
                    RowlEngine_GetDialogueVoiceBlipSound(handle));
        if (std::string(RowlEngine_GetDialogueVoiceBlipSound(handle)) != "voices/b2b.wav") {
            std::cerr << "B2b voice-blip content mismatch" << std::endl;
            exit(1);
        }
    }

    // #69: sahipsiz handle uzerinde eszamanli story yazici/okuyucu islak
    // testi. Claim artik paylasimli sahiplik + TU-local kapi (g_storyGate)
    // tasiyor: yazici commit'i (chapter vektor realloc) ile okuyucu
    // toEngineChecked-anlik-goruntu kopyasi arasindaki pencere kapali.
    // Davranis kilidi (tek-is-parcaciginda kapi no-op oldugu icin RED
    // uretemez — deterministik RED tests/test_story_gate.py'dedir):
    // cokme yok, her okuma tutarli anlik-goruntu (sayi 0/2, id gecerli
    // ya da INVALID_ARGUMENT), yazici bitince durum 2'ye yerlesir.
    // Init BILEREK cagrilmaz: Init ana-is-parcacigini sahip yapar ve
    // worker'lari Foreign'a dusururdu; LoadStoryGraph/GetChapter*
    // Init gerektirmez (m_context/m_storyRuntime Create'te hazir).
    {
        const auto raceGraph = unicodeRoot / "race_graph.json";
        {
            std::ofstream raceFile(raceGraph);
            raceFile << R"({
                "format_version": 5, "start_node_id": 201,
                "nodes": [
                    {"id": 201, "chapter_id": "ch1", "next_nodes": [{"id": 202}]},
                    {"id": 202, "chapter_id": "ch2"}
                ],
                "chapters": [
                    {"id": "ch1", "title": "Race Bir", "order": 1},
                    {"id": "ch2", "title": "Race İki", "order": 0}
                ]
            })";
        }
        // Parent phase-2b kalibi: dar-yol donusumu Windows ANSI
        // codepage'de throw atabilir; UTF-8 baytlar her yerde gecer.
        std::string raceNarrow;
        try {
            raceNarrow = raceGraph.string();
        } catch (const std::exception&) {
            const std::u8string raceU8 = raceGraph.u8string();
            raceNarrow.assign(reinterpret_cast<const char*>(raceU8.data()), raceU8.size());
        }
        RowlEngineHandle raceHandle = RowlEngine_Create();
        if (raceHandle == nullptr) {
            std::cerr << "#69: race handle uretilemedi" << std::endl;
            exit(1);
        }
        std::atomic<bool> raceTorn{false};
        std::thread raceWriter([&] {
            // LoadStoryGraph void doner (sonuc GetChapterCount/
            // GetLastStoryGraphError ile gozlenir); yaris basinci
            // cagrilarin kendisidir, donus kodu degil.
            for (int i = 0; i < 50; ++i) {
                RowlEngine_LoadStoryGraph(raceHandle, raceNarrow.c_str());
            }
        });
        std::thread raceReader([&] {
            for (int i = 0; i < 200; ++i) {
                uint32_t raceCount = 99;
                if (RowlEngine_GetChapterCount(raceHandle, &raceCount) != ROWL_RESULT_OK) {
                    raceTorn = true;
                    return;
                }
                if (raceCount != 0 && raceCount != 2) {
                    raceTorn = true;
                    return;
                }
                if (raceCount == 2) {
                    char idBuf[64];
                    uint32_t idRequired = 0;
                    const auto idRc = RowlEngine_GetChapterIdAtUtf8(raceHandle, 0, idBuf,
                                                                  sizeof(idBuf), &idRequired);
                    if (idRc != ROWL_RESULT_OK && idRc != ROWL_RESULT_INVALID_ARGUMENT) {
                        raceTorn = true;
                        return;
                    }
                    if (idRc == ROWL_RESULT_OK) {
                        const std::string id(idBuf);
                        if (id != "ch1" && id != "ch2") {
                            raceTorn = true;
                            return;
                        }
                    }
                }
            }
        });
        raceWriter.join();
        raceReader.join();
        if (raceTorn) {
            std::cerr << "#69: yazar/okuyucu penceresinde yirtik anlik-goruntu" << std::endl;
            exit(1);
        }
        uint32_t settleCount = 0;
        if (RowlEngine_GetChapterCount(raceHandle, &settleCount) != ROWL_RESULT_OK ||
            settleCount != 2) {
            std::cerr << "#69: yaris penceresi sonrasi durum yerlesmedi" << std::endl;
            exit(1);
        }
        RowlEngine_Destroy(raceHandle);
    }

    RowlEngine_Destroy(handle);
    std::error_code cleanupError;
    std::filesystem::remove_all(unicodeRoot, cleanupError);
    TEST_PASS("version, capabilities, UTF-8 caller buffers, and legacy wrappers");
}
