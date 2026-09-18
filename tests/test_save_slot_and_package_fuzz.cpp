/**
 * test_save_slot_and_package_fuzz.cpp — Deterministic fail-closed fuzz tests
 * for save-slot JSON decoding and .rowlpkg package headers.
 *
 * Fixed seed, no randomness, no network. Temp files live only under
 * std::filesystem::temp_directory_path(). Every hostile input must be
 * rejected with an error code — never a crash, never an escaped exception.
 *
 * Wired into the suite through test_main.cpp like every other translation
 * unit (declaration in rowl_test_harness.hpp).
 */
#include "rowl_test_harness.hpp"
#include "rowl/state/save_durability.hpp"
#include "rowl/state/session_persistence.hpp"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <exception>

namespace {

uint32_t g_fuzzState = 0x5EEDF00Du;

// Portable env-var injection: MSVC has no POSIX setenv/unsetenv.
// _putenv_s manipulates the same CRT block std::getenv reads, and
// _putenv_s(name, "") removes the variable (unsetenv equivalent).
void setTestEnv(const char* name, const char* value) {
#ifdef _WIN32
    if (value == nullptr) {
        _putenv_s(name, "");
    } else {
        _putenv_s(name, value);
    }
#else
    if (value == nullptr) {
        ::unsetenv(name);
    } else {
        ::setenv(name, value, 1);
    }
#endif
}

uint8_t nextFuzzByte() {
    g_fuzzState = g_fuzzState * 1664525u + 1013904223u;
    return static_cast<uint8_t>(g_fuzzState >> 24u);
}

void failCase(const std::string& what) {
    std::cerr << what << std::endl;
    exit(1);
}

// decodeJson must answer every hostile payload with an error status
// (InvalidData or UnsupportedVersion) and a null state. An exception
// escaping decodeJson is itself a fail-closed violation.
void requireDecodeRejected(const std::string& payload, const char* caseName) {
    Rowl::State::GameStateDecodeResult result;
    try {
        result = Rowl::State::GameState::decodeJson(payload);
    } catch (const std::exception& error) {
        failCase(std::string("decodeJson threw on save fuzz case ") + caseName +
                 ": " + error.what());
    } catch (...) {
        failCase(std::string("decodeJson threw an unknown exception on save fuzz case ") +
                 caseName);
    }
    if (result.succeeded() || result.state != nullptr ||
        (result.status != Rowl::State::GameStateDecodeStatus::InvalidData &&
         result.status != Rowl::State::GameStateDecodeStatus::UnsupportedVersion)) {
        failCase(std::string("Hostile save payload was not rejected: ") + caseName);
    }
}

// A single-byte corruption of a valid save may still parse (e.g. a digit
// flip), which is legitimate. Fail-closed then means: rejection, or an
// accepted state that still satisfies the engine invariants decodeJson
// enforces (non-zero ids, finite bounded volumes and playtime).
void requireCorruptionContained(const std::string& payload, const char* caseName) {
    Rowl::State::GameStateDecodeResult result;
    try {
        result = Rowl::State::GameState::decodeJson(payload);
    } catch (...) {
        failCase(std::string("decodeJson threw on save corruption case ") + caseName);
    }
    if (!result.succeeded()) return;
    const auto& state = result.state;
    if (state->stepId == 0 || state->activeNodeId == 0 ||
        !std::isfinite(state->bgmVolume) || state->bgmVolume < 0.0f ||
        state->bgmVolume > 1.0f || !std::isfinite(state->playtimeSeconds) ||
        state->playtimeSeconds < 0.0) {
        failCase(std::string("Corrupted save decoded into an invalid state: ") + caseName);
    }
}

void requirePackageRejected(const std::filesystem::path& packagePath,
                            const char* caseName) {
    bool valid = false;
    bool threw = false;
    try {
        Rowl::VFS::RowlPkgDataSource source(packagePath.string());
        valid = source.isValid();
        if (!valid) {
            if (source.exists("any.txt") || !source.read("any.txt").empty()) {
                failCase(std::string("Rejected package still served reads: ") + caseName);
            }
        }
        // openStream() must fail closed as well: null or !good, never throws.
        auto stream = source.openStream("any.txt");
        if (stream && stream->good()) {
            failCase(std::string("Rejected package still served a stream: ") + caseName);
        }
    } catch (...) {
        threw = true;
    }
    if (threw) {
        failCase(std::string("Package open threw instead of failing closed: ") + caseName);
    }
    if (valid) {
        failCase(std::string("Malformed package was accepted: ") + caseName);
    }
}

void writeBytes(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream output(path, std::ios::binary);
    if (size > 0) {
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    output.close();
}

} // namespace

void test_save_slot_and_package_fuzz() {
    TEST_SECTION("Deterministic Save-Slot & Package Fuzz (fail-closed)");

    const auto testRoot =
        std::filesystem::temp_directory_path() / "rowl_save_slot_package_fuzz";
    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
    std::filesystem::create_directories(testRoot, cleanupError);

    // (1) Truncated / garbage / wrong-type save JSON payloads.
    // NOTE: std::string with explicit lengths — the two embedded-NUL
    // entries would truncate at the first NUL as const char*.
    const std::string hostileSaves[] = {
        "",
        " ",
        "{",
        "}",
        "{ not valid json",
        "[1,2,3",
        "null",
        "[]",
        "42",
        "\"save\"",
        "true",
        std::string("\x00\x01\x02", 3),
        std::string("\xff\xfe\x00" "abc", 6),
        R"({"version":"3","step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":true,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":null,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":[3],"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":3.5,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":-1,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":0,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":4,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":999,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":4294967295,"step_id":1,"active_node_id":101,"variables":{}})",
        R"({"version":3,"step_id":0,"active_node_id":101,"variables":{}})",
        R"({"version":3,"step_id":1,"active_node_id":0,"variables":{}})",
        R"({"version":3,"step_id":"1","active_node_id":101,"variables":{}})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":[]})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":"x"})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":42})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"bgm_volume":2.0})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"bgm_volume":"loud"})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"playtime_seconds":-1})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"thumbnail_png_base64":"!!!"})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"dialogue_history":{}})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"dialogue_history":[42]})",
        R"({"version":3,"step_id":1,"active_node_id":101,"variables":{},"dialogue_history":[{"node_id":0,"speaker":"E","dialogue":"H"}]})",
    };
    for (size_t index = 0; index < sizeof(hostileSaves) / sizeof(hostileSaves[0]); ++index) {
        requireDecodeRejected(hostileSaves[index],
                              ("hand-written hostile save #" + std::to_string(index)).c_str());
    }
    {
        // Oversized payloads are rejected before parsing.
        std::string oversized(4 * 1024 * 1024 + 1, 'x');
        requireDecodeRejected(oversized, "oversized save payload");
    }
    TEST_PASS("Hand-written hostile save payloads decode to an error, never throw");

    {
        // Every strict prefix of a valid save is incomplete JSON and must fail.
        const auto valid =
            Rowl::State::GameState::createInitialState(101)->serializeJson();
        for (size_t length = 0; length < valid.size(); ++length) {
            requireDecodeRejected(valid.substr(0, length),
                                  ("truncated save prefix len " + std::to_string(length)).c_str());
        }
        // Deterministic single-byte corruptions: rejected, or still invariant-valid.
        g_fuzzState = 0x5EEDF00Du;
        for (uint32_t round = 0; round < 64; ++round) {
            std::string mutated = valid;
            const size_t position =
                (static_cast<size_t>(nextFuzzByte()) << 8u | nextFuzzByte()) % valid.size();
            mutated[position] = static_cast<char>(nextFuzzByte());
            requireCorruptionContained(
                mutated, ("corrupted save round " + std::to_string(round)).c_str());
        }
        // Deterministic binary garbage with a guaranteed-unparseable first byte.
        // Reseed so this corpus is decoupled from the corruption-loop stream.
        g_fuzzState = 0x5EEDF00Du;
        for (uint32_t round = 0; round < 64; ++round) {
            std::string garbage(1 + (nextFuzzByte() % 128), '\0');
            for (auto& byte : garbage) byte = static_cast<char>(nextFuzzByte());
            garbage.front() = static_cast<char>(0xFF);
            requireDecodeRejected(garbage,
                                  ("binary garbage save #" + std::to_string(round)).c_str());
        }
    }
    TEST_PASS("Truncated, corrupted, and garbage save payloads stay fail-closed");

    // (2) Out-of-range slot indexes through the persistence validation path.
    {
        Rowl::State::SessionPersistence persistence(testRoot / "saves");
        const auto state = Rowl::State::GameState::createInitialState(101);
        const int32_t badSlots[] = {-1, 100, 101, INT_MAX, INT_MIN};
        for (const int32_t slot : badSlots) {
            const std::string label = "slot " + std::to_string(slot);
            if (persistence.saveSlot(state, slot)) {
                failCase("Out-of-range saveSlot accepted " + label);
            }
            if (persistence.hasSlot(slot)) {
                failCase("Out-of-range hasSlot reported " + label);
            }
            if (persistence.loadSlot(slot) != nullptr ||
                persistence.loadSlotDetailed(slot).succeeded()) {
                failCase("Out-of-range loadSlot accepted " + label);
            }
            if (persistence.deleteSlot(slot)) {
                failCase("Out-of-range deleteSlot accepted " + label);
            }
            // The GameState static delegates must enforce the same boundary:
            // they forward to SessionPersistence.
            const std::string dir = (testRoot / "saves").string();
            if (Rowl::State::GameState::saveToSlot(state, slot, dir) ||
                Rowl::State::GameState::hasSlot(slot, dir) ||
                Rowl::State::GameState::loadFromSlot(slot, dir) != nullptr ||
                Rowl::State::GameState::deleteSlot(slot, dir)) {
                failCase("GameState delegate accepted out-of-range " + label);
            }
            const auto stray =
                testRoot / "saves" / ("save_slot_" + std::to_string(slot) + ".json");
            if (std::filesystem::exists(stray)) {
                failCase("Out-of-range slot left a file behind: " + stray.string());
            }
        }
        // Boundary slots 0 and 99 remain usable: the gate rejects
        // out-of-range indexes, not saving itself.
        for (const int32_t slot : {0, 99}) {
            if (!persistence.saveSlot(state, slot) || !persistence.hasSlot(slot) ||
                !persistence.loadSlot(slot)) {
                failCase("Boundary slot rejected: " + std::to_string(slot));
            }
            persistence.deleteSlot(slot);
        }
    }
    TEST_PASS("Out-of-range slot indexes rejected on every entry point, boundaries intact");

    // (2b) Engine-level canonical range (0..99, rowl/state/save_slots.hpp).
    // Slot 100 must be rejected with InvalidArgument before touching
    // persistence (it used to fall through to IoError/FileNotFound);
    // hasSaveSlot has no result code to surface, so it only answers false.
    {
        Rowl::Core::Engine engine;
        engine.setSaveDirectory((testRoot / "engine_saves").string());
        if (!engine.initialize({})) {
            failCase("Engine-level slot-range test could not initialize offscreen");
        }
        const auto lastCode = [&] {
            return engine.getContext()->getLastResult().code;
        };
        for (const int32_t slot : {100, -1, 101, INT_MAX, INT_MIN}) {
            const std::string label = "slot " + std::to_string(slot);
            if (engine.saveGameSlot(slot)) {
                failCase("Engine saveGameSlot accepted out-of-range " + label);
            }
            if (lastCode() != Rowl::Core::RuntimeErrorCode::InvalidArgument) {
                failCase("Engine saveGameSlot did not report InvalidArgument for " + label);
            }
            if (engine.loadGameSlot(slot)) {
                failCase("Engine loadGameSlot accepted out-of-range " + label);
            }
            if (lastCode() != Rowl::Core::RuntimeErrorCode::InvalidArgument) {
                failCase("Engine loadGameSlot did not report InvalidArgument for " + label);
            }
            if (engine.hasSaveSlot(slot)) {
                failCase("Engine hasSaveSlot reported out-of-range " + label);
            }
            if (engine.deleteSaveSlot(slot)) {
                failCase("Engine deleteSaveSlot accepted out-of-range " + label);
            }
            if (lastCode() != Rowl::Core::RuntimeErrorCode::InvalidArgument) {
                failCase("Engine deleteSaveSlot did not report InvalidArgument for " + label);
            }
            const auto stray =
                testRoot / "engine_saves" / ("save_slot_" + std::to_string(slot) + ".json");
            if (std::filesystem::exists(stray)) {
                failCase("Engine out-of-range slot left a file behind: " + stray.string());
            }
        }
        // Boundary slots 0 and 99 round-trip through the Engine entry points.
        for (const int32_t slot : {0, 99}) {
            if (!engine.saveGameSlot(slot) || !engine.hasSaveSlot(slot) ||
                !engine.loadGameSlot(slot)) {
                failCase("Engine boundary slot rejected: " + std::to_string(slot));
            }
            if (!engine.deleteSaveSlot(slot) || engine.hasSaveSlot(slot)) {
                failCase("Engine boundary slot delete failed: " + std::to_string(slot));
            }
        }
        engine.shutdown();
    }
    TEST_PASS("Engine rejects slot 100 (and -1/101) with InvalidArgument, boundaries 0/99 round-trip");

    // (3) Malformed .rowlpkg headers and magic values.
    constexpr size_t kHeaderSize = sizeof(Rowl::VFS::RowlPkgHeader);
    const auto packagePath = [&](const std::string& name) { return testRoot / name; };
    {
        // Positive control: a known-valid empty package (fileCount=0,
        // indexOffset==headerSize) must be accepted, serve fail-closed
        // answers for unlisted entries, and stream fail-closed.
        Rowl::VFS::RowlPkgHeader emptyHeader{{'R', 'O', 'W', 'L'}, 1, 0,
                                             static_cast<uint64_t>(kHeaderSize)};
        const auto emptyPath = packagePath("valid_empty.rowlpkg");
        writeBytes(emptyPath, &emptyHeader, sizeof(emptyHeader));
        try {
            Rowl::VFS::RowlPkgDataSource emptySource(emptyPath.string());
            if (!emptySource.isValid()) {
                failCase("Valid empty package was rejected");
            }
            if (emptySource.exists("any.txt") || !emptySource.read("any.txt").empty()) {
                failCase("Valid empty package served an unlisted read");
            }
            auto emptyStream = emptySource.openStream("any.txt");
            if (emptyStream && emptyStream->good()) {
                failCase("Valid empty package served a stream for an unlisted entry");
            }
        } catch (...) {
            failCase("Valid empty package threw instead of answering fail-closed");
        }

        Rowl::VFS::RowlPkgHeader badMagic{{'X', 'X', 'X', 'X'}, 1, 0,
                                          static_cast<uint64_t>(kHeaderSize)};
        writeBytes(packagePath("bad_magic.rowlpkg"), &badMagic, sizeof(badMagic));
        requirePackageRejected(packagePath("bad_magic.rowlpkg"), "bad magic");

        Rowl::VFS::RowlPkgHeader lowerMagic{{'r', 'o', 'w', 'l'}, 1, 0,
                                            static_cast<uint64_t>(kHeaderSize)};
        writeBytes(packagePath("lower_magic.rowlpkg"), &lowerMagic, sizeof(lowerMagic));
        requirePackageRejected(packagePath("lower_magic.rowlpkg"), "lowercase magic");

        writeBytes(packagePath("empty.rowlpkg"), nullptr, 0);
        requirePackageRejected(packagePath("empty.rowlpkg"), "empty file");
        writeBytes(packagePath("tiny.rowlpkg"), "RO", 2);
        requirePackageRejected(packagePath("tiny.rowlpkg"), "two-byte file");

        // Truncated-header ladder: every length below a full header, all with
        // a valid magic prefix so the size gate is what rejects them.
        g_fuzzState = 0xB16B00B5u;
        for (size_t length = 0; length < kHeaderSize; ++length) {
            std::vector<uint8_t> bytes(length);
            for (auto& byte : bytes) byte = nextFuzzByte();
            const char magic[4] = {'R', 'O', 'W', 'L'};
            for (size_t i = 0; i < length && i < 4; ++i) bytes[i] = static_cast<uint8_t>(magic[i]);
            const auto path = packagePath("truncated_" + std::to_string(length) + ".rowlpkg");
            writeBytes(path, bytes.data(), bytes.size());
            requirePackageRejected(path,
                                   ("truncated header len " + std::to_string(length)).c_str());
        }

        // Absurd header field values, each isolated with an otherwise empty archive.
        const auto absurd = [&](const std::string& name, Rowl::VFS::RowlPkgHeader header) {
            const auto path = packagePath(name);
            writeBytes(path, &header, sizeof(header));
            requirePackageRejected(path, name.c_str());
        };
        absurd("spec_v0.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 0, 0,
                                        static_cast<uint64_t>(kHeaderSize)});
        absurd("spec_v2.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 2, 0,
                                        static_cast<uint64_t>(kHeaderSize)});
        absurd("spec_vmax.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 0xFFFFu, 0,
                                        static_cast<uint64_t>(kHeaderSize)});
        absurd("count_over_max.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 100001,
                                        static_cast<uint64_t>(kHeaderSize)});
        absurd("count_u32max.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 0xFFFFFFFFu,
                                        static_cast<uint64_t>(kHeaderSize)});
        absurd("index_zero.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 0, 0});
        absurd("index_one.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 0, 1});
        absurd("index_u64max.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 0, 0xFFFFFFFFFFFFFFFFull});
        absurd("index_past_eof.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 0,
                                        static_cast<uint64_t>(kHeaderSize + 1)});
        absurd("count_one_no_index.rowlpkg",
               Rowl::VFS::RowlPkgHeader{{'R', 'O', 'W', 'L'}, 1, 1,
                                        static_cast<uint64_t>(kHeaderSize)});

        // Deterministic binary fuzz corpus: broken magic is the invariant, so
        // every input is invalid by construction while lengths and trailing
        // bytes still exercise the short/arbitrary-file paths.
        g_fuzzState = 0xC0FFEE42u;
        for (uint32_t round = 0; round < 32; ++round) {
            std::vector<uint8_t> bytes(1 + (nextFuzzByte() % 128));
            for (auto& byte : bytes) byte = nextFuzzByte();
            bytes.front() = static_cast<uint8_t>('X');
            const auto path = packagePath("fuzz_" + std::to_string(round) + ".rowlpkg");
            writeBytes(path, bytes.data(), bytes.size());
            requirePackageRejected(
                path, ("binary package fuzz #" + std::to_string(round)).c_str());
        }
    }
    TEST_PASS("Malformed package headers, magic, and sizes rejected fail-closed");

    // (4) Save durability: atomic write, ENOSPC fail-closed, stray-.tmp cleanup.
    {
        Rowl::State::SessionPersistence persistence(testRoot / "durability");
        const int32_t slot = 7;
        const auto goodState = Rowl::State::GameState::createInitialState(101);
        if (!persistence.saveSlot(goodState, slot)) {
            failCase("Durability baseline save failed");
        }
        const auto slotPath =
            testRoot / "durability" / ("save_slot_" + std::to_string(slot) + ".json");
        const auto tmpPath = Rowl::State::saveTempPathFor(slotPath);
        auto readBytes = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        };
        const std::string baselineBytes = readBytes(slotPath);
        if (baselineBytes.empty()) {
            failCase("Durability baseline slot file is empty");
        }

        // ENOSPC injection (test bridge): save must fail closed and the
        // previous good slot file must be preserved byte-identical.
        Rowl::State::setSaveDurabilityInjectEnospc(true);
        const auto nextState =
            Rowl::State::GameState::createNextState(goodState, 202);
        const bool injectedOk = persistence.saveSlot(nextState, slot);
        Rowl::State::setSaveDurabilityInjectEnospc(false);
        if (injectedOk) {
            failCase("ENOSPC-injected save unexpectedly succeeded");
        }
        if (readBytes(slotPath) != baselineBytes) {
            failCase("ENOSPC-injected save modified the previous good slot");
        }
        if (std::filesystem::exists(tmpPath)) {
            failCase("ENOSPC-injected save left a stray temp file behind");
        }
        {
            const auto loaded = persistence.loadSlot(slot);
            if (loaded == nullptr || loaded->activeNodeId != 101) {
                failCase("Load after ENOSPC did not return the previous good slot");
            }
        }

        // Env-var injection path (production default off): with the variable
        // set the save fails the same way; unset, it succeeds again.
        setTestEnv("ROWL_SAVE_INJECT_ENOSPC", "1");
        if (persistence.saveSlot(nextState, slot)) {
            setTestEnv("ROWL_SAVE_INJECT_ENOSPC", nullptr);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            failCase("Env-var ENOSPC-injected save unexpectedly succeeded");
        }
        setTestEnv("ROWL_SAVE_INJECT_ENOSPC", nullptr);
        if (readBytes(slotPath) != baselineBytes) {
            failCase("Env-var ENOSPC-injected save modified the previous good slot");
        }
        if (!persistence.saveSlot(nextState, slot)) {
            failCase("Save after clearing ENOSPC injection failed");
        }
        const std::string updatedBytes = readBytes(slotPath);
        if (updatedBytes == baselineBytes) {
            failCase("Save after clearing injection did not advance the slot");
        }
        // Restore the baseline so the stray-tmp check below reads known-good data.
        if (!persistence.saveSlot(goodState, slot)) {
            failCase("Could not restore durability baseline slot");
        }

        // Simulated half-tmp: a stray "<slot>.json.tmp" beside the good slot
        // must be ignored by load, which still reads the good slot and
        // removes the stray.
        {
            std::ofstream stray(tmpPath, std::ios::binary | std::ios::trunc);
            stray << baselineBytes.substr(0, baselineBytes.size() / 2);
            stray.close();
        }
        if (!std::filesystem::exists(tmpPath)) {
            failCase("Could not plant stray temp file for durability test");
        }
        {
            const auto loaded = persistence.loadSlot(slot);
            if (loaded == nullptr || loaded->activeNodeId != 101) {
                failCase("Load with stray temp did not return the good slot");
            }
        }
        if (std::filesystem::exists(tmpPath)) {
            failCase("Load did not clean up the stray temp file");
        }
        if (readBytes(slotPath) != baselineBytes) {
            failCase("Stray-temp cleanup modified the good slot");
        }
    }
    TEST_PASS("Atomic save survives ENOSPC fail-closed and stray-.tmp cleanup");

    // (5) Errno-parametric injection + durability edges (Faz 6 Dilim 7, IS 1/2).
    // Root-safe: injection + file plants only, no chmod. Every sub-case
    // closes its injection before the next one starts.
    {
        Rowl::State::SessionPersistence persistence(testRoot / "durability_errno");
        const auto goodState = Rowl::State::GameState::createInitialState(101);
        auto readBytes = [](const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        };
        const auto slotPathFor = [&](int32_t slot) {
            return testRoot / "durability_errno" /
                ("save_slot_" + std::to_string(slot) + ".json");
        };
        const auto requireFailClosedMatrix =
            [&](int32_t slot, const std::string& baselineBytes, const char* caseName) {
                const auto slotPath = slotPathFor(slot);
                const auto tmpPath = Rowl::State::saveTempPathFor(slotPath);
                if (readBytes(slotPath) != baselineBytes) {
                    failCase(std::string(caseName) + ": slot modified by failed save");
                }
                if (std::filesystem::exists(tmpPath)) {
                    failCase(std::string(caseName) + ": stray temp left behind");
                }
                const auto loaded = persistence.loadSlot(slot);
                if (loaded == nullptr || loaded->activeNodeId != 101) {
                    failCase(std::string(caseName) +
                             ": load after failed save lost the good slot");
                }
            };

        // (5a) EACCES-injected save: false + EACCES in errorOut + matrix.
        {
            const int32_t slot = 3;
            if (!persistence.saveSlot(goodState, slot)) {
                failCase("EACCES durability baseline save failed");
            }
            const auto slotPath = slotPathFor(slot);
            const std::string baselineBytes = readBytes(slotPath);
            const auto nextState =
                Rowl::State::GameState::createNextState(goodState, 202);
            Rowl::State::setSaveDurabilityInjectErrno(EACCES);
            std::string directError;
            const bool directOk = Rowl::State::writeSlotFileAtomically(
                slotPath, nextState->serializeJson(), &directError);
            const bool injectedOk = persistence.saveSlot(nextState, slot);
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (directOk || injectedOk) {
                failCase("EACCES-injected save unexpectedly succeeded");
            }
            if (directError.find("EACCES") == std::string::npos) {
                failCase("EACCES-injected save errorOut missing EACCES: " + directError);
            }
            requireFailClosedMatrix(slot, baselineBytes, "EACCES injection");
        }

        // (5b) EROFS-injected save: same fail-closed matrix with EROFS code.
        {
            const int32_t slot = 4;
            if (!persistence.saveSlot(goodState, slot)) {
                failCase("EROFS durability baseline save failed");
            }
            const auto slotPath = slotPathFor(slot);
            const std::string baselineBytes = readBytes(slotPath);
            const auto nextState =
                Rowl::State::GameState::createNextState(goodState, 202);
            Rowl::State::setSaveDurabilityInjectErrno(EROFS);
            std::string directError;
            const bool directOk = Rowl::State::writeSlotFileAtomically(
                slotPath, nextState->serializeJson(), &directError);
            const bool injectedOk = persistence.saveSlot(nextState, slot);
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (directOk || injectedOk) {
                failCase("EROFS-injected save unexpectedly succeeded");
            }
            if (directError.find("EROFS") == std::string::npos) {
                failCase("EROFS-injected save errorOut missing EROFS: " + directError);
            }
            requireFailClosedMatrix(slot, baselineBytes, "EROFS injection");
        }

        // (5c) Truncated final .json plant: InvalidData, no crash, no stale data.
        {
            const int32_t slot = 5;
            if (!persistence.saveSlot(goodState, slot)) {
                failCase("Truncated-final baseline save failed");
            }
            const auto slotPath = slotPathFor(slot);
            const std::string baselineBytes = readBytes(slotPath);
            if (baselineBytes.size() < 16) {
                failCase("Truncated-final baseline slot unexpectedly tiny");
            }
            {
                std::ofstream plant(slotPath, std::ios::binary | std::ios::trunc);
                plant << baselineBytes.substr(0, baselineBytes.size() / 2);
                plant.close();
            }
            Rowl::State::SessionLoadResult result;
            try {
                result = persistence.loadSlotDetailed(slot);
            } catch (...) {
                failCase("Load of truncated final slot threw instead of InvalidData");
            }
            if (result.succeeded() || result.state != nullptr ||
                result.status != Rowl::State::SessionLoadStatus::InvalidData) {
                failCase("Truncated final slot did not answer InvalidData");
            }
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
        }

        // (5d) Rename-fail: empty directory planted at the slot path.
        {
            const int32_t slot = 8;
            const auto slotPath = slotPathFor(slot);
            const auto tmpPath = Rowl::State::saveTempPathFor(slotPath);
            std::error_code setupError;
            std::filesystem::remove_all(slotPath, setupError);
            std::filesystem::create_directories(slotPath, setupError);
            if (!std::filesystem::is_directory(slotPath)) {
                failCase("Could not plant directory probe for rename-fail test");
            }
            // The probe must genuinely fail: save into the planted directory
            // path has to answer false (D6 chmod-555 lesson — never assume).
            const bool probeOk = persistence.saveSlot(goodState, slot);
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (probeOk) {
                std::filesystem::remove_all(slotPath, setupError);
                failCase("Rename-fail probe unexpectedly succeeded; directory plant "
                         "does not fail on this platform");
            }
            if (!std::filesystem::is_directory(slotPath)) {
                failCase("Rename-fail save removed the planted directory");
            }
            if (std::filesystem::exists(tmpPath)) {
                failCase("Rename-fail save left a stray temp file behind");
            }
            std::filesystem::remove_all(slotPath, setupError);
        }

        // (5e) Unsupported errno normalizes to ENOSPC; legacy bool wrapper
        // stays equivalent and leaves no injection behind.
        {
            Rowl::State::setSaveDurabilityInjectErrno(9999);
            if (Rowl::State::saveDurabilityInjectErrno() != ENOSPC) {
                Rowl::State::setSaveDurabilityInjectErrno(0);
                failCase("Unsupported injected errno did not normalize to ENOSPC");
            }
            std::string normalizedError;
            const bool normalizedOk = Rowl::State::writeSlotFileAtomically(
                slotPathFor(6), goodState->serializeJson(), &normalizedError);
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (normalizedOk) {
                failCase("Normalized-ENOSPC save unexpectedly succeeded");
            }
            if (normalizedError.find("ENOSPC") == std::string::npos) {
                failCase("Normalized-ENOSPC errorOut missing ENOSPC: " + normalizedError);
            }
            Rowl::State::setSaveDurabilityInjectEnospc(true);
            if (Rowl::State::saveDurabilityInjectErrno() != ENOSPC ||
                !Rowl::State::saveDurabilityInjectEnospc()) {
                Rowl::State::setSaveDurabilityInjectEnospc(false);
                failCase("Legacy ENOSPC wrapper diverged from errno-parametric hook");
            }
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (Rowl::State::saveDurabilityInjectErrno() != 0) {
                failCase("Injection leaked past cleanup");
            }
        }

        // (5f) errno-0 tolerance on the real open-fail path: iostream need
        // not set errno, so a stale 0 must never render "[...(0): Success]".
        {
            const auto blocker = testRoot / "durability_errno" / "blocker_file";
            {
                std::ofstream out(blocker, std::ios::binary | std::ios::trunc);
                out << "x";
            }
            const auto slotPath = blocker / "save_slot_9.json";
            errno = 0;  // tolerate a platform that leaves errno untouched
            std::string openError;
            const bool openOk = Rowl::State::writeSlotFileAtomically(
                slotPath, goodState->serializeJson(), &openError);
            Rowl::State::setSaveDurabilityInjectErrno(0);
            Rowl::State::setSaveDurabilityInjectEnospc(false);
            if (openOk) {
                failCase("Open-fail probe unexpectedly succeeded");
            }
            if (openError.find("(0)") != std::string::npos) {
                failCase("Open-fail error carries a misleading errno-0 tag: " +
                         openError);
            }
        }
    }
    TEST_PASS("Errno-parametric injection (EACCES/EROFS/normalize), truncated-final InvalidData, rename-fail closed");

    // (6) A2b: hasSlot no-throw + save nesting-depth guard (bilerek-boz).
    {
        // Overlong save directory: the throwing exists/is_regular_file
        // probes fail with an OS error here (ENAMETOOLONG). Pre-fix that
        // error escaped hasSlot as std::filesystem::filesystem_error; the
        // A2b contract answers false and never throws. The probe keeps the
        // case honest: on a platform where overlong paths resolve, the
        // assertions below would be vacuous, so skip instead of passing.
        const std::string longDir =
            testRoot.string() + "/" + std::string(5000, 'x');
        const std::filesystem::path longSlot =
            std::filesystem::path(longDir) / "save_slot_0.json";
        std::error_code probeEc;
        (void)std::filesystem::exists(longSlot, probeEc);
        if (!probeEc) {
            std::cout << "  [SKIP] overlong paths resolve on this platform; "
                         "hasSlot no-throw case is vacuous"
                      << std::endl;
        } else {
            Rowl::State::SessionPersistence longPersistence{
                std::filesystem::path(longDir)};
            bool threw = false;
            bool answer = true;
            try {
                answer = longPersistence.hasSlot(0);
            } catch (...) {
                threw = true;
            }
            if (threw) {
                failCase("hasSlot threw on a hostile save directory instead of false");
            }
            if (answer) {
                failCase("hasSlot reported a slot inside an overlong directory");
            }
            try {
                if (Rowl::State::GameState::hasSlot(0, longDir)) {
                    failCase("GameState::hasSlot reported a slot inside an "
                             "overlong directory");
                }
            } catch (...) {
                failCase("GameState::hasSlot threw on a hostile save directory");
            }
            TEST_PASS("hasSlot answers false (never throws) on a hostile save directory");
        }
    }

    {
        // Nesting-depth guard: 512 levels of real "[" nest far above the 128
        // cap yet far below any stack danger, so the pre-guard binary
        // survives to report RED (decode succeeds — the nested array lands
        // in a dumped variable value) while the guarded build answers
        // InvalidData without ever entering the parser.
        std::string nested("{\"step_id\":1,\"active_node_id\":101,");
        nested += "\"variables\":{\"k\":";
        nested.append(512, '[');
        nested.append(512, ']');
        nested += "}}";
        requireDecodeRejected(nested, "512-deep nested save payload");

        // Brackets inside dialogue strings must not count toward depth: a
        // 300-"[" chapter title is flat JSON and must still load.
        const std::string bracketText(300, '[');
        const std::string flat =
            std::string("{\"step_id\":1,\"active_node_id\":101,") +
            "\"variables\":{},\"chapter_title\":\"" + bracketText + "\"}";
        Rowl::State::GameStateDecodeResult flatResult;
        try {
            flatResult = Rowl::State::GameState::decodeJson(flat);
        } catch (...) {
            failCase("decodeJson threw on brackets-inside-string payload");
        }
        if (!flatResult.succeeded()) {
            failCase("Brackets inside a save string tripped the depth guard");
        }
    }
    TEST_PASS("Save nesting-depth guard rejects deep nesting, ignores string brackets");

    std::filesystem::remove_all(testRoot, cleanupError);
}
