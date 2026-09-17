/**
 * test_game_state.cpp — Game-state save/load/rewind slots.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#include "rowl/state/save_metadata.hpp"
#include "rowl/state/session_persistence.hpp"
#include <vector>

void test_game_state() {
    TEST_SECTION("GameState & Rewind Subsystem");

    // Initial state creation
    auto s1 = Rowl::State::GameState::createInitialState(101);
    if (s1->stepId != 1 || s1->activeNodeId != 101 || s1->previousState != nullptr) {
        std::cerr << "GameState init mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Initial GameState Creation (Node #101, Step 1)");

    // State progression with variable mutation
    auto s2 = Rowl::State::GameState::createNextState(s1, 102, "player_name", "Evelyn");
    if (s2->stepId != 2 || s2->activeNodeId != 102 || s2->getVariable("player_name") != "Evelyn" || !s1->getVariable("player_name").empty()) {
        std::cerr << "GameState mutation mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Immutable State Transition with Variable Mutation");

    // State progression without variable mutation (structural sharing check)
    auto s3 = Rowl::State::GameState::createNextState(s2, 103);
    if (s3->stepId != 3 || s3->activeNodeId != 103 || s3->getVariable("player_name") != "Evelyn" || s3->variables != s2->variables) {
        std::cerr << "GameState structural sharing mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Zero-Copy Structural Sharing of Variables");

    // Rewind 2 steps back from s3 -> should be s1
    auto rewound = Rowl::State::GameState::rewind(s3, 2);
    if (!rewound || rewound->stepId != 1 || rewound->activeNodeId != 101) {
        std::cerr << "GameState rewind mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Multi-Step Historical Rewind (Step 3 -> Step 1)");

    // SessionPersistence owns checkpoint alignment and rewind stepping over
    // the immutable chain; Engine delegates to these without changing behavior.
    {
        auto fresh = Rowl::State::SessionPersistence::checkpoint(nullptr, 101);
        if (!fresh || fresh->stepId != 1 || fresh->activeNodeId != 101) {
            std::cerr << "SessionPersistence checkpoint from null mismatch" << std::endl;
            exit(1);
        }
        auto aligned = Rowl::State::SessionPersistence::checkpoint(s3, 103);
        if (aligned != s3) {
            std::cerr << "SessionPersistence checkpoint must reuse an aligned chain" << std::endl;
            exit(1);
        }
        auto advanced = Rowl::State::SessionPersistence::checkpoint(s3, 104);
        if (!advanced || advanced->activeNodeId != 104 || advanced->stepId != s3->stepId + 1 ||
            advanced->getVariable("player_name") != "Evelyn" || advanced->previousState != s3) {
            std::cerr << "SessionPersistence checkpoint node-mismatch mismatch" << std::endl;
            exit(1);
        }
        if (Rowl::State::SessionPersistence::rewind(nullptr, 1)) {
            std::cerr << "SessionPersistence rewind of null must report no movement" << std::endl;
            exit(1);
        }
        if (Rowl::State::SessionPersistence::rewind(s3, 0)) {
            std::cerr << "SessionPersistence rewind of zero steps must report no movement" << std::endl;
            exit(1);
        }
        auto stepped = Rowl::State::SessionPersistence::rewind(s3, 2);
        if (stepped != s1) {
            std::cerr << "SessionPersistence rewind chain mismatch" << std::endl;
            exit(1);
        }
        auto clamped = Rowl::State::SessionPersistence::rewind(s3, 99);
        if (!clamped || clamped->stepId != 1) {
            std::cerr << "SessionPersistence rewind must clamp at the history root" << std::endl;
            exit(1);
        }
        if (Rowl::State::SessionPersistence::rewind(s1, 1)) {
            std::cerr << "SessionPersistence rewind at root must report no movement" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("SessionPersistence Checkpoint and Rewind Chain Ownership");

    // JSON Serialization & Slot Persistence
    std::string serialized = s2->serializeJson();
    auto deserialized = Rowl::State::GameState::deserializeJson(serialized);
    if (!deserialized || deserialized->stepId != s2->stepId || deserialized->activeNodeId != 102 ||
        deserialized->getVariable("player_name") != "Evelyn") {
        std::cerr << "GameState JSON serialize/deserialize mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState JSON Serialization & Deserialization");

    const auto withHistory = Rowl::State::GameState::withDialogueHistory(s2, {
        {101, "Evelyn", "First remembered line", true, ""},
        {102, "Mina", "Second remembered line", true, ""},
    });
    const auto restoredHistory = Rowl::State::GameState::deserializeJson(withHistory->serializeJson());
    if (!restoredHistory || !restoredHistory->dialogueHistory ||
        restoredHistory->dialogueHistory->size() != 2 ||
        restoredHistory->dialogueHistory->at(1).speaker != "Mina" ||
        restoredHistory->dialogueHistory->at(1).dialogue != "Second remembered line") {
        std::cerr << "GameState dialogue history persistence mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Bounded Dialogue History Save Persistence and v2 Fallback");

    // Faz 2: backlog entries round-trip their content ids; legacy payloads
    // without the key decode to "" and stay loadable, while hostile ids are
    // rejected instead of persisted.
    const auto withIds = Rowl::State::GameState::withDialogueHistory(s2, {
        {201, "Evelyn", "Tracked line", true, "11111111-2222-3333-4444-555555555555"},
        {202, "Mina", "Legacy line", true, ""},
    });
    const auto restoredIds = Rowl::State::GameState::deserializeJson(withIds->serializeJson());
    if (!restoredIds || !restoredIds->dialogueHistory ||
        restoredIds->dialogueHistory->size() != 2 ||
        restoredIds->dialogueHistory->at(0).contentId !=
            "11111111-2222-3333-4444-555555555555" ||
        !restoredIds->dialogueHistory->at(1).contentId.empty()) {
        std::cerr << "GameState dialogue history content_id round-trip mismatch" << std::endl;
        exit(1);
    }
    const auto legacyHistory = Rowl::State::GameState::deserializeJson(
        R"({"version":3,"step_id":1,"active_node_id":201,"variables":{},"dialogue_history":[
            {"node_id":201,"speaker":"Evelyn","dialogue":"Old line","read":true}]})");
    if (!legacyHistory || !legacyHistory->dialogueHistory ||
        legacyHistory->dialogueHistory->size() != 1 ||
        !legacyHistory->dialogueHistory->at(0).contentId.empty()) {
        std::cerr << "Legacy history entries must decode with an empty content_id" << std::endl;
        exit(1);
    }
    const std::string hostileId(2048, 'x');
    const auto hostile = Rowl::State::GameState::decodeJson(
        std::string(R"({"version":3,"step_id":1,"active_node_id":201,"variables":{},"dialogue_history":[)") +
        R"({"node_id":201,"speaker":"E","dialogue":"H","read":true,"content_id":")" + hostileId + "\"}]}");
    if (hostile.status != Rowl::State::GameStateDecodeStatus::InvalidData || hostile.state) {
        std::cerr << "Oversized history content_id must be rejected" << std::endl;
        exit(1);
    }
    TEST_PASS("Dialogue History content_id Round-Trip, Legacy Fallback, and Hostile Rejection");

    // Faz 2 Dilim 4: display-only save metadata round-trips; legacy saves
    // decode to defaults; malformed/oversized payloads are rejected.
    {
        const auto meta = Rowl::State::GameState::withSaveMetadata(s2, {
            3723.5, "ch7", "The Crossing", "Evelyn: Dawn breaks.",
            std::string("\x89PNG\r\n\x1a\n", 8) + "fake-bytes", 320, 180,
        });
        const auto restoredMeta =
            Rowl::State::GameState::deserializeJson(meta->serializeJson());
        if (!restoredMeta || restoredMeta->playtimeSeconds != 3723.5 ||
            restoredMeta->chapterId != "ch7" ||
            restoredMeta->chapterTitle != "The Crossing" ||
            restoredMeta->summary != "Evelyn: Dawn breaks." ||
            restoredMeta->thumbnailPng != meta->thumbnailPng ||
            restoredMeta->thumbnailWidth != 320 || restoredMeta->thumbnailHeight != 180) {
            std::cerr << "Save metadata round-trip mismatch" << std::endl;
            exit(1);
        }
        const auto legacyMeta = Rowl::State::GameState::deserializeJson(
            R"({"version":3,"step_id":1,"active_node_id":101,"variables":{}})");
        if (!legacyMeta || legacyMeta->playtimeSeconds != 0.0 ||
            !legacyMeta->chapterId.empty() || !legacyMeta->summary.empty() ||
            !legacyMeta->thumbnailPng.empty() || legacyMeta->thumbnailWidth != 0) {
            std::cerr << "Legacy saves must decode with default metadata" << std::endl;
            exit(1);
        }
        const auto badThumb = Rowl::State::GameState::decodeJson(
            R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},)"
            R"("thumbnail_png_base64":"!!!not-base64!!!"})");
        if (badThumb.status != Rowl::State::GameStateDecodeStatus::InvalidData) {
            std::cerr << "Malformed thumbnail base64 must be rejected" << std::endl;
            exit(1);
        }
        const auto badPlaytime = Rowl::State::GameState::decodeJson(
            R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},)"
            R"("playtime_seconds":-5})");
        if (badPlaytime.status != Rowl::State::GameStateDecodeStatus::InvalidData) {
            std::cerr << "Negative playtime must be rejected" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Save Metadata Round-Trip, Legacy Defaults, and Hostile Rejection");

    // Thumbnail encoder: PNG magic, downscale cap, base64 round-trip.
    {
        std::vector<uint8_t> rgba(640 * 480 * 4, 128);
        const auto thumb = Rowl::State::encodeThumbnailPng(
            rgba.data(), 640, 480, 640 * 4);
        if (thumb.png.size() < 8 ||
            thumb.png.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0 ||
            thumb.width != 320 || thumb.height != 240) {
            std::cerr << "Thumbnail encoder must emit capped PNG bytes" << std::endl;
            exit(1);
        }
        std::string decoded;
        if (!Rowl::State::base64Decode(
                Rowl::State::base64Encode(
                    reinterpret_cast<const uint8_t*>(thumb.png.data()),
                    static_cast<uint32_t>(thumb.png.size())),
                decoded) ||
            decoded != thumb.png) {
            std::cerr << "Thumbnail base64 round-trip mismatch" << std::endl;
            exit(1);
        }
        if (Rowl::State::base64Decode("!!!", decoded)) {
            std::cerr << "Malformed base64 must be rejected" << std::endl;
            exit(1);
        }
        const auto empty = Rowl::State::encodeThumbnailPng(nullptr, 0, 0, 0);
        if (!empty.png.empty() || empty.width != 0) {
            std::cerr << "Degenerate pixels must yield an empty thumbnail" << std::endl;
            exit(1);
        }
        const std::string clock = Rowl::State::iso8601UtcNow();
        if (clock.size() != 20 || clock[4] != '-' || clock[10] != 'T' || clock.back() != 'Z') {
            std::cerr << "ISO-8601 clock malformed: " << clock << std::endl;
            exit(1);
        }
        if (Rowl::State::truncateSummary("abc", 10) != "abc" ||
            Rowl::State::truncateSummary("abcdef", 4) != "abcd" ||
            Rowl::State::truncateSummary("a\xc3\xa9" "cdef", 3) != "a\xc3\xa9") {
            std::cerr << "Summary truncation must respect UTF-8 boundaries" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Thumbnail PNG Encode, Base64 Codec, Clock, and Truncation");

    auto audioState = Rowl::State::GameState::createNextStateWithAudio(
        s2, 102, "night.png", "audio/night.ogg", 0.65f, true, "Telephone");
    auto restoredAudioState = Rowl::State::GameState::deserializeJson(audioState->serializeJson());
    if (!restoredAudioState || restoredAudioState->activeBgm != "audio/night.ogg" ||
        !restoredAudioState->bgmPlaying || std::abs(restoredAudioState->bgmVolume - 0.65f) > 0.001f ||
        restoredAudioState->dspFilter != "Telephone") {
        std::cerr << "GameState audio presentation serialization mismatch" << std::endl;
        exit(1);
    }
    const auto legacyState = Rowl::State::GameState::deserializeJson(
        R"({"version":1,"step_id":1,"active_node_id":101,"variables":{}})");
    const auto v2State = Rowl::State::GameState::deserializeJson(
        R"({"version":2,"step_id":1,"active_node_id":101,"variables":{}})");
    if (!legacyState || !v2State || legacyState->bgmPlaying || !legacyState->activeBgm.empty() ||
        !v2State->dialogueHistory || !v2State->dialogueHistory->empty()) {
        std::cerr << "Legacy GameState save compatibility mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Versioned Audio Presentation Save State and v1 Compatibility");

    const auto currentDecode = Rowl::State::GameState::decodeJson(serialized);
    const auto v1Decode = Rowl::State::GameState::decodeJson(
        R"({"version":1,"step_id":1,"active_node_id":101,"variables":{}})");
    const auto v2Decode = Rowl::State::GameState::decodeJson(
        R"({"version":2,"step_id":1,"active_node_id":101,"variables":{}})");
    const auto futureDecode = Rowl::State::GameState::decodeJson(
        R"({"version":999,"step_id":1,"active_node_id":101,"variables":{}})");
    if (!currentDecode.succeeded() || currentDecode.migrated() ||
        currentDecode.sourceVersion != Rowl::State::GameState::CurrentSaveFormatVersion ||
        !v1Decode.migrated() || v1Decode.sourceVersion != 1 ||
        !v2Decode.migrated() || v2Decode.sourceVersion != 2 ||
        futureDecode.succeeded() ||
        futureDecode.status != Rowl::State::GameStateDecodeStatus::UnsupportedVersion ||
        futureDecode.sourceVersion != 999) {
        std::cerr << "GameState version migration result mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Explicit GameState v1/v2 Migration and Future-Version Result");

    // T0b: CWD-relative "build/test_saves" is now a hermetic per-run temp
    // dir — parallel runs and foreign CWDs can never collide or pollute
    // the repo.
    const std::string testSaveDir =
        (std::filesystem::temp_directory_path() /
         ("rowl_game_state_" +
          std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count())))
            .string();
    {
        std::error_code hermeticEc;
        std::filesystem::create_directories(testSaveDir, hermeticEc);
        if (hermeticEc) {
            std::cerr << "GameState hermetic save dir could not be created: "
                      << hermeticEc.message() << std::endl;
            exit(1);
        }
    }
    Rowl::State::SessionPersistence persistence(testSaveDir);
    if (!persistence.saveSlot(s2, 1)) {
        std::cerr << "SessionPersistence saveSlot failed" << std::endl;
        exit(1);
    }
    if (std::filesystem::exists(std::filesystem::path(testSaveDir) / "save_slot_1.json.tmp")) {
        std::cerr << "GameState atomic save left a temporary file behind" << std::endl;
        exit(1);
    }
    if (!persistence.hasSlot(1)) {
        std::cerr << "SessionPersistence hasSlot failed" << std::endl;
        exit(1);
    }
    auto loadedSlot = persistence.loadSlot(1);
    if (!loadedSlot || loadedSlot->activeNodeId != 102 || loadedSlot->getVariable("player_name") != "Evelyn") {
        std::cerr << "GameState loadFromSlot content mismatch" << std::endl;
        exit(1);
    }
    const auto replacementState = Rowl::State::GameState::createNextState(s2, 303, "player_name", "Mina");
    if (!persistence.saveSlot(replacementState, 1)) {
        std::cerr << "SessionPersistence atomic slot replacement failed" << std::endl;
        exit(1);
    }
    const auto replacedSlot = persistence.loadSlot(1);
    if (!replacedSlot || replacedSlot->activeNodeId != 303 || replacedSlot->getVariable("player_name") != "Mina" ||
        std::filesystem::exists(std::filesystem::path(testSaveDir) / "save_slot_1.json.tmp")) {
        std::cerr << "GameState atomic slot replacement produced inconsistent data" << std::endl;
        exit(1);
    }
    if (!Rowl::State::GameState::hasSlot(1, testSaveDir) ||
        !Rowl::State::GameState::loadFromSlot(1, testSaveDir)) {
        std::cerr << "GameState compatibility delegates did not reach SessionPersistence"
                  << std::endl;
        exit(1);
    }
    TEST_PASS("SessionPersistence Atomic Slots and GameState Compatibility Delegates");

    std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_5.json")
        << R"({"version":1,"step_id":1,"active_node_id":101,"variables":{}})";
    const auto migratedSlot = persistence.loadSlotDetailed(5);
    if (!migratedSlot.migrated() || migratedSlot.sourceVersion != 1 ||
        !migratedSlot.state || migratedSlot.state->activeNodeId != 101) {
        std::cerr << "SessionPersistence did not expose v1 migration result" << std::endl;
        exit(1);
    }
    TEST_PASS("SessionPersistence Exposes Versioned Migration Result");

    // Malformed save matrix: empty files, oversized payloads and mistyped
    // versions must classify safely without crashing.
    {
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_6.json").close();
        const auto emptySlot = persistence.loadSlotDetailed(6);
        if (emptySlot.succeeded() ||
            emptySlot.status != Rowl::State::SessionLoadStatus::InvalidData) {
            std::cerr << "Zero-byte save slot was not InvalidData" << std::endl;
            exit(1);
        }
        {
            std::ofstream large(std::filesystem::path(testSaveDir) / "save_slot_7.json",
                                std::ios::binary);
            const std::string chunk(1024 * 1024, 'x');
            for (int i = 0; i < 5; ++i) large << chunk;
        }
        const auto largeSlot = persistence.loadSlotDetailed(7);
        if (largeSlot.succeeded() ||
            largeSlot.status != Rowl::State::SessionLoadStatus::FileTooLarge) {
            std::cerr << "Oversized save slot was not FileTooLarge" << std::endl;
            exit(1);
        }
        const auto stringVersion = Rowl::State::GameState::decodeJson(
            R"({"version":"3","step_id":1,"active_node_id":101,"variables":{}})");
        if (stringVersion.succeeded() ||
            stringVersion.status != Rowl::State::GameStateDecodeStatus::InvalidData) {
            std::cerr << "String save version was not InvalidData" << std::endl;
            exit(1);
        }
        const auto missingVersion = Rowl::State::GameState::decodeJson(
            R"({"step_id":1,"active_node_id":101,"variables":{}})");
        if (!missingVersion.succeeded() || missingVersion.migrated() ||
            missingVersion.sourceVersion != Rowl::State::GameState::CurrentSaveFormatVersion) {
            std::cerr << "Versionless save was not treated as the current format" << std::endl;
            exit(1);
        }
        persistence.deleteSlot(6);
        persistence.deleteSlot(7);
    }
    TEST_PASS("Malformed save matrix classifies empty, oversized and mistyped saves");

    // Save files are user-controlled input once they reach disk. Reject
    // malformed, unsupported, and structurally invalid content safely.
    {
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_2.json") << "{ not valid json";
        if (persistence.loadSlot(2)) {
            std::cerr << "Malformed GameState save was accepted" << std::endl;
            exit(1);
        }
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_3.json")
            << R"({"version":999,"step_id":1,"active_node_id":101,"variables":{}})";
        const auto futureSlot = persistence.loadSlotDetailed(3);
        if (futureSlot.succeeded() ||
            futureSlot.status != Rowl::State::SessionLoadStatus::UnsupportedVersion ||
            futureSlot.sourceVersion != 999) {
            std::cerr << "Future GameState save version result mismatch" << std::endl;
            exit(1);
        }
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_4.json")
            << R"({"version":1,"step_id":0,"active_node_id":101,"variables":[]})";
        if (persistence.loadSlot(4)) {
            std::cerr << "Structurally invalid GameState save was accepted" << std::endl;
            exit(1);
        }
        if (persistence.saveSlot(s2, -1) || persistence.hasSlot(-1) ||
            persistence.deleteSlot(-1)) {
            std::cerr << "Negative GameState save slot was accepted" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("GameState Save Corruption, Version, and Slot-Bounds Containment");

    // Faz 6 Dilim 7 IS 2/2: save-format sürüm karar kilidi. Davranış değişikliği
    // yok — decodeJson sürümsüz JSON'u Loaded-olarak-3 kabul eder (game_state.cpp:265),
    // v1/v2 pasif Migrated toleransıdır (:379-381, dönüşüm kodu yok), bilinmeyen
    // sürümler reddedilir (:261-268). Bu matris kararı kilitler.
    {
        struct VersionExpectation {
            const char* label;
            const char* payload;
            Rowl::State::GameStateDecodeStatus status;
            uint32_t sourceVersion;
            bool success;
            bool migrated;
        };
        const VersionExpectation matrix[] = {
            {"missing-version", R"({"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::Loaded, 3, true, false},
            {"v1", R"({"version":1,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::Migrated, 1, true, true},
            {"v2", R"({"version":2,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::Migrated, 2, true, true},
            {"v3", R"({"version":3,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::Loaded, 3, true, false},
            {"v4", R"({"version":4,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::UnsupportedVersion, 4, false, false},
            {"v999", R"({"version":999,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::UnsupportedVersion, 999, false, false},
            {"string-version", R"({"version":"3","step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::InvalidData, 0, false, false},
            {"negative-version", R"({"version":-1,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::InvalidData, 0, false, false},
            {"float-version", R"({"version":3.5,"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::InvalidData, 0, false, false},
            {"object-version", R"({"version":{},"step_id":1,"active_node_id":101,"variables":{}})",
             Rowl::State::GameStateDecodeStatus::InvalidData, 0, false, false},
        };
        for (const auto& c : matrix) {
            const auto r = Rowl::State::GameState::decodeJson(c.payload);
            if (r.status != c.status || r.sourceVersion != c.sourceVersion ||
                r.succeeded() != c.success || r.migrated() != c.migrated ||
                (c.success != (r.state != nullptr))) {
                std::cerr << "Save-format version lock mismatch: " << c.label << std::endl;
                exit(1);
            }
        }
        // v999 decode-seviyesinde reddedilir; engine eşlemesi (engine.cpp:2068-2096,
        // ValidationError + red) mevcut e2e ile kilitlidir
        // (test_runtime_context_and_diagnostics.cpp:129-153), burada tekrarlanmaz.
        // Bilinmeyen sürümün slot katmanındaki karşılığı (session_persistence.cpp:120-127)
        // v4 üzerinden doğrulanır:
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_8.json")
            << R"({"version":4,"step_id":1,"active_node_id":101,"variables":{}})";
        const auto v4Slot = persistence.loadSlotDetailed(8);
        if (v4Slot.succeeded() ||
            v4Slot.status != Rowl::State::SessionLoadStatus::UnsupportedVersion ||
            v4Slot.sourceVersion != 4) {
            std::cerr << "v4 save slot was not UnsupportedVersion" << std::endl;
            exit(1);
        }
        persistence.deleteSlot(8);
    }
    TEST_PASS("Save-Format Version Matrix Lock (Loaded/Migrated/UnsupportedVersion/InvalidData)");

    persistence.deleteSlot(1);
    if (persistence.hasSlot(1)) {
        std::cerr << "SessionPersistence deleteSlot failed" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState Slot Cleanup (deleteSlot)");
    // T0b: hermetic dir cleanup is strict — a leftover must fail loudly,
    // never silently pollute the temp dir across reruns.
    {
        std::error_code cleanupEc;
        std::filesystem::remove_all(testSaveDir, cleanupEc);
        if (cleanupEc) {
            std::cerr << "GameState hermetic save dir cleanup failed: "
                      << cleanupEc.message() << std::endl;
            exit(1);
        }
    }
}
