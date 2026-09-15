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

#include <climits>
#include <cstdint>
#include <exception>

namespace {

uint32_t g_fuzzState = 0x5EEDF00Du;

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

    // (2) Out-of-range slot indexes through the engine validation path.
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

    std::filesystem::remove_all(testRoot, cleanupError);
}
