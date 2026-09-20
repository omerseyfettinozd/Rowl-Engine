/**
 * test_audio_streaming.cpp — Faz 5 Dilim 1: OGG streaming core + mixer bus
 * skeleton + decision observability.
 *
 * New translation unit (additive-only slice; test_audio_engine.cpp and
 * test_audio_device_recovery.cpp are untouched). Six groups:
 *  1. Threshold formula pins (64 MiB basis, no formula copy in prod path).
 *  2. Header-probe edges: WAV claimed-size intents + 29/30/31 s boundary.
 *  3. StreamInfo JSON schema + decideStream wrapper semantics.
 *  4. Engine routing: short OGG -> memory, granule-patched OGG BGM ->
 *     stream, WAV-over-threshold -> RAM fallback, non-BGM never streams.
 *  5. C API observability: capability bit, IsStreaming, GetStreamInfoJson
 *     caller-buffer contract, BGM/Ambience/Ui volumes.
 *  6. Robustness: virtual-clock soak + loop wrap, suspend skips pump,
 *     device rebuild requeues, silent/corrupt fail-closed.
 *
 * Granule-patch technique: the last OggS page granule of a real OGG
 * fixture is overwritten with a huge value (with the page CRC repaired so
 * the decoder still accepts the page), so both the header probe AND the
 * streaming source (ov_pcm_total) report over-threshold while the decoder
 * still plays the real packets and then hits EOS. The short tiny fixture
 * (240 frames) can never stream through the source-duration gate
 * (max 256 frames even with a huge granule), so routing/soak/capacity use
 * the 2 s long fixture (88200 frames, patched to 1e9 with CRC fix); the
 * tiny fixture stays probe-only and is NEVER used for parity.
 * Bit-exact parity is proven honestly on the short NORMAL (unpatched)
 * fixture: OggStreamSource small-chunk reads vs the identical ov_read
 * full-decode path, memcmp-compared (same decoder, same file).
 */
#include "rowl_test_harness.hpp"
#include "rowl/audio/audio_streaming.hpp"
#include "rowl/audio/long_audio_contract.hpp"
#include "rowl/audio/mixer_buses.hpp"
#include "rowl/audio/stream_mixer.hpp"

#include <cmath>
#include <limits>
#include <vorbis/vorbisfile.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

void appendU16LE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendU32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

// Minimal PCM WAV header advertising claimedDataBytes of PCM without
// carrying them: the probe trusts the claim (intent signal, no decode).
std::vector<uint8_t> makeClaimWav(uint32_t sampleRateHz, uint16_t channels,
                                  uint16_t bitsPerSample,
                                  uint32_t claimedDataBytes) {
    const uint32_t byteRate = sampleRateHz * channels * (bitsPerSample / 8u);
    const uint16_t blockAlign =
        static_cast<uint16_t>(channels * (bitsPerSample / 8u));
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(out, 36u + claimedDataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32LE(out, 16u);
    appendU16LE(out, 1u);
    appendU16LE(out, channels);
    appendU32LE(out, sampleRateHz);
    appendU32LE(out, byteRate);
    appendU16LE(out, blockAlign);
    appendU16LE(out, bitsPerSample);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32LE(out, claimedDataBytes);
    return out;
}

void fail(const std::string& message) {
    std::cerr << message << std::endl;
    exit(1);
}

void expect(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

// Small real OGG/Vorbis fixture (same bytes as test_audio_engine.cpp's
// checked-in tone). Replaced at write time with the exact literal; the
// placeholder below is never compiled in.
const char* kToneOggBase64 = "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";

std::vector<uint8_t> toneOggBytes() {
    const auto bytes = decodeBase64(kToneOggBase64);
    expect(!bytes.empty(), "Streaming fixture OGG base64 did not decode");
    expect(bytes.size() > 64 && std::memcmp(bytes.data(), "OggS", 4) == 0,
           "Streaming fixture is not an OGG bitstream");
    return bytes;
}

// Overwrites the trailing OggS page granule with a huge sample count so
// the header probe reports over-threshold while the decoder still plays
// the real packets and then hits EOS (no encoder needed).
// NOTE: probe-only trick (no CRC repair). The patched page fails the OGG
// CRC check, so ov_pcm_total/ov_read see 0 frames on the tiny fixture;
// it must NEVER be used for parity or for the source-duration streaming
// gate (openBgmStream checks OggStreamSource::durationSeconds()).
// Routing/soak/capacity use patchGranuleWithCrcForStream + the long
// fixture below instead.
[[maybe_unused]] std::vector<uint8_t> patchGranuleForStream(std::vector<uint8_t> ogg,
                                           uint64_t granule) {
    size_t lastPage = std::string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (std::memcmp(ogg.data() + i, "OggS", 4) == 0) lastPage = i;
    }
    expect(lastPage != std::string::npos, "No OggS page found for granule patch");
    expect(lastPage + 14 <= ogg.size(), "Truncated OggS page in granule patch");
    for (int b = 0; b < 8; ++b) {
        ogg[lastPage + 6 + b] = static_cast<uint8_t>((granule >> (8 * b)) & 0xFFu);
    }
    return ogg;
}

// OGG page CRC (poly 0x04C11DB7, init 0) for the CRC-repairing patch.
// Same algorithm the muxer uses: CRC field zeroed, then CRC over the full
// page (header + segment table + body).
uint32_t oggPageCrc(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) {
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
            }
            table[i] = r;
        }
        ready = true;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFFu];
    }
    return crc;
}

// CRC-repairing granule patch: the streaming gate (openBgmStream) checks
// OggStreamSource::durationSeconds() (ov_pcm_total, CRC-sensitive), so a
// streamable patched fixture must keep its last page CRC-valid. The tiny
// 240-frame fixture still caps at 256 frames via ov even with a huge
// granule, so this is used with the long (88200-frame) fixture, whose
// patched pcm_total is the granule itself (1e9 -> over-threshold).
std::vector<uint8_t> patchGranuleWithCrcForStream(std::vector<uint8_t> ogg,
                                                  uint64_t granule) {
    size_t lastPage = std::string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (std::memcmp(ogg.data() + i, "OggS", 4) == 0) lastPage = i;
    }
    expect(lastPage != std::string::npos, "No OggS page found for CRC granule patch");
    expect(lastPage + 27 <= ogg.size(), "Truncated OggS page in CRC granule patch");
    for (int b = 0; b < 8; ++b) {
        ogg[lastPage + 6 + b] = static_cast<uint8_t>((granule >> (8 * b)) & 0xFFu);
    }
    const size_t nseg = ogg[lastPage + 26];
    expect(lastPage + 27 + nseg <= ogg.size(), "Truncated OggS segment table");
    size_t body = 0;
    for (size_t i = 0; i < nseg; ++i) body += ogg[lastPage + 27 + i];
    const size_t pageEnd = lastPage + 27 + nseg + body;
    expect(pageEnd <= ogg.size(), "Truncated OggS page body");
    ogg[lastPage + 22] = 0;
    ogg[lastPage + 23] = 0;
    ogg[lastPage + 24] = 0;
    ogg[lastPage + 25] = 0;
    const uint32_t crc = oggPageCrc(ogg.data() + lastPage, pageEnd - lastPage);
    ogg[lastPage + 22] = static_cast<uint8_t>(crc & 0xFFu);
    ogg[lastPage + 23] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    ogg[lastPage + 24] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
    ogg[lastPage + 25] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
    return ogg;
}

// Long real OGG/Vorbis fixture: 2 s sine, 44100 Hz stereo (88200
// frames, ~9.3 KiB). Generated once via ffmpeg sine+libvorbis; checked in
// as base64 so the test stays hermetic (no encoder at runtime). Stereo
// 44.1 kHz keeps the ~0.5 s pump gate above 4x4096 frames, so warmup fills
// the 16384-frame ring in one pump (8000 Hz mono would stall at 1 chunk).
const char* kLongToneOggBase64 = "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4AAAEGCDpsTiAIWGrAQAQgIAHIpiWZrmeZ4niqapmiRJ0zxPFEXRNE1TVUmSpnmeKIqiaZqmqrIsTfM8URRF01RVVYWmeZ4oiqJpqqrqwvM8TxRF0TRV1XXheZ4niqJomqrquhBFUTRN01RNVXVdIIqmaZqqqqquC0RPFE1TVV3XdYHniaJpqqqrui4QTdNUVVV1XVkGmKZpqqrryjJAVVXVdV1XlgGqqqqu67qyDFBV13VdWZZlAK7rurIsywIAAA4cAAACjKCTjCqLsNGECw9AoSErAoAoAADAGKYUU8owJiGkEBrGJIQUQiYlpdJSqiCkUlIpFYRUSiolo5RSailVEFIpqZQKQiollVIAANiBAwDYgYVQaMhKACAPAIAwRinGGHNOIqQUY845JxFSijHnnJNKMeacc85JKRlzzDnnpJTOOeecc1JK5pxzzjkppXPOOeeclFJK55xzTkopJYTOQSellNI555wTAABU4AAAEGCjyOYEI0GFhqwEAFIBAAyOY1ma5nmiaJqWJGma53meKJqmJkma5nmeJ4qqyfM8TxRF0TRVled5niiKommqKtcVRdM0TVVVXbIsiqZpmqrqujBN01RV13VdmKZpqqrrui5sW1VV1XVlGbatqqrqurIMXNd1ZdmWgSy7ruzasgAA8AQHAKACG1ZHOCkaCyw0ZCUAkAEAQBiDkEIIIWUQQgohhJRSCAkAABhwAAAIMKEMFBqyEgBIBQAAjLHWWmuttdZAZ6211lprrYDMWmuttdZaa6211lprrbXWUmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaay2llFJKKaWUUkoppZRSSimllFJKBQD6VTgA+D/YsDrCSdFYYKEhKwGAcAAAwBilGHMMQimlVAgx5px0VFqLsUKIMeckpNRabMVzzkEoIZXWYiyecw5CKSnFVmNRKYRSUkottliLSqGjklJKrdVYjDGppNZai63GYoxJKbTUWosxFiNsTam12GqrsRhjayottBhjjMUIX2RsLabaag3GCCNbLC3VWmswxhjdW4ultpqLMT742lIsMdZcAAB3gwMARIKNM6wknRWOBhcashIACAkAIBBSijHGGHPOOeekUow55pxzDkIIoVSKMcaccw5CCCGUjDHmnHMQQgghhFJKxpxzEEII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";

std::vector<uint8_t> longToneOggBytes() {
    const auto bytes = decodeBase64(kLongToneOggBase64);
    expect(!bytes.empty(), "Long streaming fixture OGG base64 did not decode");
    expect(bytes.size() > 64 && std::memcmp(bytes.data(), "OggS", 4) == 0,
           "Long streaming fixture is not an OGG bitstream");
    return bytes;
}

uint64_t currentRssKb() {
#if defined(_WIN32)
    // GetProcessWorkingSetSize reports quota LIMITS, not usage, so it can
    // never observe growth; WorkingSetSize is the real resident figure.
    // (psapi link is added in tests/CMakeLists.txt, WIN32-only.)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                             sizeof(counters)) == 0) {
        return 0;
    }
    return static_cast<uint64_t>(counters.WorkingSetSize / 1024u);
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return static_cast<uint64_t>(usage.ru_maxrss); // KiB on Linux.
#endif
}

size_t parityVorbisRead(void* pointer, size_t size, size_t count,
                        void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream || size == 0 || count == 0) return 0;
    const auto requested = std::min<uint64_t>(
        static_cast<uint64_t>(size) * count, 64ULL * 1024 * 1024);
    stream->read(static_cast<char*>(pointer), static_cast<std::streamsize>(requested));
    return static_cast<size_t>(stream->gcount()) / size;
}

int parityVorbisSeek(void* datasource, ogg_int64_t offset, int whence) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream) return -1;
    std::ios_base::seekdir direction;
    switch (whence) {
        case SEEK_SET: direction = std::ios::beg; break;
        case SEEK_CUR: direction = std::ios::cur; break;
        case SEEK_END: direction = std::ios::end; break;
        default: return -1;
    }
    stream->clear();
    stream->seekg(static_cast<std::streamoff>(offset), direction);
    return stream->fail() ? -1 : 0;
}

int parityVorbisClose(void*) { return 0; }

long parityVorbisTell(void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream) return -1;
    const auto position = stream->tellg();
    return position < 0 || position > LONG_MAX ? -1 : static_cast<long>(position);
}

bool jsonHas(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\":") != std::string::npos;
}

} // namespace

void test_audio_streaming() {
    TEST_SECTION("Audio Streaming Core, Mixer Buses & Decision Observability");

    // ── Grup 1: threshold formula pins (64 MiB basis) ────────────────
    {
        using Rowl::Audio::longAudioThresholdSeconds;
        const double cd = longAudioThresholdSeconds(44100, 2, 2);
        const double dvd = longAudioThresholdSeconds(48000, 2, 2);
        expect(std::abs(cd - 380.43573696145124) < 1e-6,
               "CD-threshold pin mismatch");
        expect(std::abs(dvd - 349.52533333333333) < 1e-6,
               "DVD-threshold pin mismatch");
        expect(longAudioThresholdSeconds(0, 2, 2) == 0.0,
               "Degenerate rate must yield 0.0");
        expect(longAudioThresholdSeconds(44100, 0, 2) == 0.0,
               "Degenerate channels must yield 0.0");
        // 100 MiB claim at CD quality: ~594.43 s duration vs ~380.44 s
        // threshold -> strictly over.
        const double claimed100MiB =
            static_cast<double>(100ULL * 1024 * 1024) / (44100.0 * 2 * 2);
        expect(std::abs(claimed100MiB - 594.4308390022676) < 1e-6,
               "100 MiB claim duration pin mismatch");
        expect(claimed100MiB > cd, "100 MiB claim must exceed the CD threshold");
        TEST_PASS("Streaming threshold formula pins (64 MiB basis)");
    }

    // ── Grup 2: header-probe edges ───────────────────────────────────
    {
        using namespace Rowl::Audio;
        // 29/30/31 s boundary against a 30 s threshold: strict `>`.
        auto under = assessLongAudio(29.0, 30.0);
        auto equal = assessLongAudio(30.0, 30.0);
        auto over = assessLongAudio(31.0, 30.0);
        expect(!under.exceedsThreshold && !equal.exceedsThreshold &&
                   over.exceedsThreshold,
               "assessLongAudio must use strict > (29/30 silent, 31 warns)");
        expect(!assessLongAudio(-1.0, 30.0).exceedsThreshold,
               "Unknown duration must stay silent (fail closed)");
        expect(!assessLongAudio(60.0, 0.0).exceedsThreshold,
               "Degenerate threshold must stay silent (fail closed)");

        // WAV claimed-size intents: tiny claim -> under; 100 MiB -> over.
        const auto tinyWav = makeClaimWav(44100, 2, 16, 44100 * 2 * 2);
        const auto tinyAssess =
            probeAndAssessLongAudio(tinyWav.data(), tinyWav.size(), "tiny.wav");
        expect(!tinyAssess.exceedsThreshold, "1 s WAV claim must stay memory");
        const auto hugeWav =
            makeClaimWav(44100, 2, 16, 100u * 1024u * 1024u);
        // Probe only needs the header: feed the first 1 KiB (clamp-safe).
        const auto hugeAssess = probeAndAssessLongAudio(
            hugeWav.data(), hugeWav.size(), "huge.wav");
        expect(hugeAssess.exceedsThreshold,
               "100 MiB WAV claim must assess over-threshold");
        expect(std::abs(hugeAssess.durationSeconds - 594.4308390022676) < 1e-3,
               "100 MiB WAV probe duration mismatch");
        expect(std::abs(hugeAssess.thresholdSeconds - 380.43573696145124) < 1e-6,
               "WAV probe threshold mismatch");

        // Truncated / unknown inputs fail closed without warning.
        const uint8_t garbage[16] = {'N', 'O', 'T', 'A', 'U', 'D', 'I', 'O',
                                     0, 0, 0, 0, 0, 0, 0, 0};
        expect(!probeAndAssessLongAudio(garbage, sizeof(garbage), nullptr)
                    .exceedsThreshold,
               "Unknown header must fail closed");
        expect(!probeAndAssessLongAudio(nullptr, 0, nullptr).exceedsThreshold,
               "Null probe input must fail closed");
        TEST_PASS("Header-probe edges (WAV claims, 29/30/31 s, fail-closed)");
    }

    // ── Grup 3: StreamInfo JSON schema + decideStream ────────────────
    {
        using namespace Rowl::Audio;
        const auto tinyWav = makeClaimWav(44100, 2, 16, 44100 * 2 * 2);
        const auto hugeWav =
            makeClaimWav(44100, 2, 16, 100u * 1024u * 1024u);

        StreamDecision tinyDecision =
            decideStream(tinyWav.data(), tinyWav.size(), "tiny.wav");
        StreamDecision hugeDecision =
            decideStream(hugeWav.data(), hugeWav.size(), "huge.wav");
        StreamDecision unknownDecision = decideStream(nullptr, 0, nullptr);
        expect(!tinyDecision.stream && hugeDecision.stream &&
                   !unknownDecision.stream,
               "decideStream must mirror assessLongAudio strictly");

        const StreamInfo tinyInfo =
            makeStreamInfoFromHeader(tinyWav.data(), tinyWav.size(), 0, "tiny.wav");
        expect(tinyInfo.mode == StreamMode::Memory &&
                   tinyInfo.reason == "under_threshold",
               "Short header must yield memory/under_threshold");
        const StreamInfo hugeInfo =
            makeStreamInfoFromHeader(hugeWav.data(), hugeWav.size(), 0, "huge.wav");
        expect(hugeInfo.mode == StreamMode::Stream &&
                   hugeInfo.reason == "over_threshold",
               "Over-threshold header must yield stream/over_threshold");
        const StreamInfo unknownInfo =
            makeStreamInfoFromHeader(nullptr, 0, 0, "missing.wav");
        expect(unknownInfo.mode == StreamMode::Unknown &&
                   unknownInfo.reason == "unknown_header" &&
                   unknownInfo.durationSeconds == -1.0,
               "Unknown header must yield unknown/unknown_header with -1");

        // Schema keys are fixed: mode, duration_seconds, threshold_seconds,
        // threshold_bytes, buffered_seconds, reason, channel, asset.
        const std::string json = streamInfoToJson(hugeInfo);
        for (const char* key : {"mode", "duration_seconds", "threshold_seconds",
                                "threshold_bytes", "buffered_seconds", "reason",
                                "channel", "asset"}) {
            expect(jsonHas(json, key),
                   std::string("StreamInfo JSON missing key: ") + key);
        }
        expect(json.find("\"mode\":\"stream\"") != std::string::npos,
               "Stream JSON must carry mode stream");
        expect(json.find("\"threshold_bytes\":67108864") != std::string::npos,
               "Stream JSON must carry the 64 MiB threshold_bytes");
        // Non-finite doubles are emitted as JSON null (valid JSON,
        // C# null-wire maps back to NaN); -1 unknown durations preserved.
        StreamInfo nanInfo = hugeInfo;
        nanInfo.durationSeconds = std::numeric_limits<double>::quiet_NaN();
        expect(streamInfoToJson(nanInfo).find("\"duration_seconds\":null") !=
                   std::string::npos,
               "Stream JSON must emit null for NaN durations");
        StreamInfo infInfo = hugeInfo;
        infInfo.thresholdSeconds = std::numeric_limits<double>::infinity();
        infInfo.bufferedSeconds = -std::numeric_limits<double>::infinity();
        const std::string infJson = streamInfoToJson(infInfo);
        expect(infJson.find("\"threshold_seconds\":null") != std::string::npos &&
                   infJson.find("\"buffered_seconds\":null") != std::string::npos,
               "Stream JSON must emit null for +-Infinity");
        expect(streamInfoToJson(unknownInfo).find("\"duration_seconds\":-1") !=
                   std::string::npos,
               "Stream JSON must preserve the -1 unknown duration");

        // Mixer skeletons stay pure math (unused by the engine this slice).
        MixerBuses buses;
        buses.setUserVolume(BusId::Bgm, 0.5f);
        buses.setUserVolume(BusId::Master, 0.5f);
        expect(std::abs(buses.effectiveBgmGain() - 0.25f) < 1e-6,
               "MixerBuses effective BGM gain mismatch");
        buses.setUserVolume(BusId::Bgm, std::numeric_limits<float>::quiet_NaN());
        expect(std::abs(buses.effectiveBgmGain() - 0.25f) < 1e-6,
               "MixerBuses must ignore non-finite volumes");
        StreamMixer mixer;
        mixer.setUserVolume(StreamBusId::Ambience, 0.5f);
        expect(std::abs(mixer.gainFor(StreamBusId::Ambience) - 0.5f) < 1e-6,
               "StreamMixer ambience gain mismatch");
        TEST_PASS("StreamInfo JSON schema, decideStream, mixer skeletons");
    }

    // ── Bit-exact parity (honest, short NORMAL OGG — never patched) ────
    // The granule-patched fixture is a probe trick (its header lies about
    // duration) and must never back a parity claim. Here the same short
    // fixture bytes go through the same libvorbis decoder twice with the
    // same S16LE->float (/32768) conversion: once incrementally via
    // OggStreamSource in tiny 64-frame chunks, once as a full-decode
    // reference mirroring decodeOggVorbis (ov_read(...,0,2,1) with a 32 KiB
    // chunk; that production symbol is TU-local, so the test re-implements
    // the identical path). Same decoder + same file => byte-identical.
    {
        using namespace Rowl::Audio;
        Rowl::VFS::VFSManager parityVfs;
        const auto parityRoot =
            std::filesystem::temp_directory_path() / "rowl_streaming_parity_project";
        const auto parityDir = parityRoot / "Assets" / "audio";
        std::filesystem::create_directories(parityDir);
        const std::vector<uint8_t> tone = toneOggBytes(); // NORMAL, unpatched
        {
            std::ofstream f(parityDir / "parity_tone.ogg", std::ios::binary);
            f.write(reinterpret_cast<const char*>(tone.data()),
                    static_cast<std::streamsize>(tone.size()));
        }
        parityVfs.remountProject(parityRoot.string());

        OggStreamSource streamed_source;
        std::string parityError;
        expect(streamed_source.open(parityVfs, "audio/parity_tone.ogg", parityError),
               "Parity OggStreamSource open failed: " + parityError);
        const uint32_t parityRate = streamed_source.sampleRateHz();
        const uint32_t parityCh = streamed_source.channelCount();
        expect(parityRate != 0 && parityCh != 0 && parityCh <= 8,
               "Parity stream format invalid");
        constexpr float kParityS16ToFloat = 1.0f / 32768.0f;
        std::vector<float> viaStream;
        {
            std::vector<float> chunk(64u * 8u, 0.0f);
            bool eos = false;
            while (!eos) {
                const size_t got =
                    streamed_source.readFloatFrames(chunk.data(), 64u, eos);
                viaStream.insert(viaStream.end(), chunk.begin(),
                                 chunk.begin() + got * parityCh);
                if (got == 0 && eos) break;
            }
            expect(eos, "Parity chunked read must reach EOS");
        }
        expect(!viaStream.empty() && viaStream.size() % parityCh == 0,
               "Parity streamed sample count invalid");
        const size_t parityFrames = viaStream.size() / parityCh;

        // Reference full decode: identical ov_read contract, large chunk.
        std::vector<float> viaFull;
        {
            auto refStream = parityVfs.openReadStream("audio/parity_tone.ogg");
            expect(refStream && refStream->good(), "Parity reference open failed");
            OggVorbis_File decoder{};
            const ov_callbacks callbacks{parityVorbisRead, parityVorbisSeek,
                                         parityVorbisClose, parityVorbisTell};
            expect(ov_open_callbacks(refStream.get(), &decoder, nullptr, 0,
                                     callbacks) == 0,
                   "Parity reference ov_open failed");
            const vorbis_info* info = ov_info(&decoder, -1);
            expect(info && info->rate == static_cast<int>(parityRate) &&
                       info->channels == static_cast<int>(parityCh),
                   "Parity reference format diverges from stream source");
            std::array<char, 32 * 1024> chunk{};
            int bitstream = 0;
            while (true) {
                const long bytes =
                    ov_read(&decoder, chunk.data(),
                            static_cast<int>(chunk.size()), 0, 2, 1, &bitstream);
                expect(bytes >= 0, "Parity reference hit corrupt stream");
                if (bytes == 0) break;
                const size_t samples = static_cast<size_t>(bytes) / 2;
                for (size_t i = 0; i < samples; ++i) {
                    int16_t sample = 0;
                    std::memcpy(&sample, chunk.data() + i * 2, sizeof(sample));
                    viaFull.push_back(static_cast<float>(sample) *
                                      kParityS16ToFloat);
                }
            }
            ov_clear(&decoder);
        }
        expect(viaFull.size() == viaStream.size(),
               "Parity frame-count mismatch (stream vs full decode)");
        expect(std::memcmp(viaStream.data(), viaFull.data(),
                           viaStream.size() * sizeof(float)) == 0,
               "OggStreamSource chunked decode is not bit-identical to full decode");
        // Seek-0 restores the head: first chunk after rewind matches.
        expect(streamed_source.seekPcmFrame(0), "Parity seek-0 failed");
        {
            std::vector<float> head(64u * 8u, 0.0f);
            bool eos = false;
            const size_t got =
                streamed_source.readFloatFrames(head.data(), 64u, eos);
            expect(got > 0 && got <= parityFrames,
                   "Parity seek-0 read invalid");
            expect(std::memcmp(head.data(), viaStream.data(),
                               got * parityCh * sizeof(float)) == 0,
                   "Seek-0 first chunk diverges from stream head");
        }
        // Probe consistency on the same bytes: rate/ch match, bps == 2.
        {
            const auto header =
                probeAudioHeaderDuration(tone.data(), tone.size());
            expect(header.known && header.sampleRateHz == parityRate &&
                       header.channelCount == parityCh && header.bytesPerSample == 2,
                   "Probe/decoder format mismatch on parity fixture");
        }
        streamed_source.close();
        std::error_code parityCleanup;
        std::filesystem::remove_all(parityRoot, parityCleanup);
        TEST_PASS("Streaming/reference bit-exact parity (short OGG, 64-frame chunks vs full decode, memcmp)");
    }

    // ── Grup 4: engine routing ───────────────────────────────────────
    {
        using namespace Rowl::Audio;
        Rowl::VFS::VFSManager vfs;
        AudioEngine audio(&vfs);
        expect(audio.initialize() && audio.isInitialized(), "Audio init failed");

        const auto projectRoot =
            std::filesystem::temp_directory_path() / "rowl_streaming_test_project";
        const auto assetDir = projectRoot / "Assets" / "audio";
        std::filesystem::create_directories(assetDir);
        const auto shortPath = assetDir / "short_tone.ogg";
        const auto longPath = assetDir / "long_theme.ogg";
        const auto hugeWavPath = assetDir / "huge_claim.wav";
        const auto corruptPath = assetDir / "corrupt.ogg";
        const std::vector<uint8_t> tone = toneOggBytes();
        {
            std::ofstream f(shortPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(tone.data()),
                    static_cast<std::streamsize>(tone.size()));
        }
        {
            // 1e9 granule samples on the LONG fixture (88200 frames) with
            // the page CRC repaired: both the header probe and the
            // source-duration gate (ov_pcm_total) report over-threshold
            // while the decoder still plays the real packets then EOS.
            // The tiny fixture caps at 256 frames via ov even with a huge
            // granule, so it can never stream through the new gate.
            const auto patched = patchGranuleWithCrcForStream(
                longToneOggBytes(), static_cast<uint64_t>(1000000000));
            std::ofstream f(longPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(patched.data()),
                    static_cast<std::streamsize>(patched.size()));
        }
        {
            const auto hugeWav =
                makeClaimWav(44100, 2, 16, 100u * 1024u * 1024u);
            std::ofstream f(hugeWavPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(hugeWav.data()),
                    static_cast<std::streamsize>(hugeWav.size()));
        }
        {
            std::vector<uint8_t> truncated(
                tone.begin(),
                tone.begin() + static_cast<ptrdiff_t>(std::min<size_t>(tone.size(), 100)));
            std::ofstream f(corruptPath, std::ios::binary);
            f.write(reinterpret_cast<const char*>(truncated.data()),
                    static_cast<std::streamsize>(truncated.size()));
        }
        vfs.remountProject(projectRoot.string());

        // Short OGG on BGM stays on the byte-identical full-decode path.
        audio.playAudio("audio/short_tone.ogg", AudioChannelType::Bgm);
        audio.update();
        expect(audio.getCurrentBgmPath() == "audio/short_tone.ogg",
               "Short OGG BGM path mismatch");
        expect(!audio.isStreaming(), "Short OGG must not stream");
        expect(audio.streamInfoJson().find("\"mode\":\"memory\"") !=
                   std::string::npos,
               "Short OGG snapshot must be memory");

        // Over-threshold OGG on BGM takes the streaming path.
        audio.playAudio("audio/long_theme.ogg", AudioChannelType::Bgm);
        audio.update();
        expect(audio.getCurrentBgmPath() == "audio/long_theme.ogg",
               "Stream BGM path mismatch");
        expect(audio.isStreaming(), "Patched OGG BGM must stream");
        const std::string streamJson = audio.streamInfoJson();
        expect(streamJson.find("\"mode\":\"stream\"") != std::string::npos &&
                   streamJson.find("\"reason\":\"over_threshold\"") !=
                       std::string::npos,
               "Stream snapshot must be stream/over_threshold: " + streamJson);

        // Non-BGM channels never stream, even for over-threshold assets.
        audio.playAudio("audio/long_theme.ogg", AudioChannelType::Sfx);
        expect(audio.isStreaming(), "SFX play must not disturb BGM streaming");
        audio.playAudioInt("audio/long_theme.ogg", 1);
        expect(audio.isStreaming(), "Voice play must not disturb BGM streaming");

        // WAV over-threshold falls through to the RAM path (decode cap
        // rejects the truncated claim): previous intent is preserved.
        audio.playAudio("audio/short_tone.ogg", AudioChannelType::Bgm);
        audio.update();
        expect(!audio.isStreaming(), "Short OGG re-play must leave stream mode");
        audio.playAudio("audio/huge_claim.wav", AudioChannelType::Bgm);
        expect(!audio.isStreaming(), "Over-threshold WAV must not stream");
        expect(audio.getCurrentBgmPath() == "audio/short_tone.ogg",
               "Failed WAV load must preserve the previous BGM intent");

        // Corrupt / truncated OGG fails closed: never streams.
        audio.playAudio("audio/corrupt.ogg", AudioChannelType::Bgm);
        expect(!audio.isStreaming(), "Corrupt OGG must fail closed");

        // Snapshot channel fidelity for raw ints via playAudioInt.
        audio.playAudioInt("audio/short_tone.ogg", 0);
        audio.update();
        expect(audio.streamInfoJson().find("\"channel\":0") != std::string::npos,
               "Stream snapshot must preserve the raw BGM channel int");

        // Ambience loop RAM + Ui one-shot gain bake (volume matrix feeds).
        audio.setAmbienceVolume(0.5f);
        audio.setUiVolume(0.25f);
        expect(std::abs(audio.getAmbienceVolume() - 0.5f) < 1e-6,
               "Ambience volume mismatch");
        expect(std::abs(audio.getUiVolume() - 0.25f) < 1e-6, "Ui volume mismatch");
        audio.playAudioInt("audio/short_tone.ogg", 3);
        expect(audio.isAmbiencePlaying(), "Ambience intent must register");
        audio.playAudioInt("audio/short_tone.ogg", 4);
        // Telemetry buses 4 (Ambience) + 5 (Ui) stay in range.
        expect(audio.getChannelPeak(4, 0) >= 0.0f && audio.getChannelPeak(4, 0) <= 1.0f,
               "Ambience telemetry out of range");
        expect(audio.getChannelPeak(5, 0) >= 0.0f && audio.getChannelPeak(5, 0) <= 1.0f,
               "Ui telemetry out of range");

        audio.stopAll();
        audio.shutdown();
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        TEST_PASS("Engine routing (short/memory, patched/stream, WAV fallback, fail-closed)");
    }

    // ── Grup 5: C API observability ──────────────────────────────────
    {
        uint64_t capabilities = 0;
        expect(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
                   (capabilities & ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING) != 0,
               "Capability bit 8192 (AUDIO_STREAMING) must be advertised");

        RowlEngineHandle handle = RowlEngine_Create();
        expect(handle != nullptr, "C API handle creation failed");
        expect(RowlEngine_Init(handle, 64, 64, 0) == 1, "C API init failed");

        // Fresh engine: no BGM -> unknown/no_bgm, never streaming.
        expect(RowlEngine_IsStreaming(handle) == 0,
               "Fresh engine must not report streaming");
        expect(RowlEngine_IsStreaming(nullptr) == 0,
               "Dead handle IsStreaming must be 0 (fail closed)");
        uint32_t required = 0;
        expect(RowlEngine_GetStreamInfoJson(handle, nullptr, 0, &required) ==
                   ROWL_RESULT_OK &&
                   required >= 2,
               "StreamInfo size query failed");
        std::vector<char> tiny(required - 1, 'x');
        uint32_t repeated = 0;
        expect(RowlEngine_GetStreamInfoJson(handle, tiny.data(),
                                            static_cast<uint32_t>(tiny.size()),
                                            &repeated) ==
                   ROWL_RESULT_BUFFER_TOO_SMALL &&
                   repeated == required && tiny.front() == '\0',
               "StreamInfo undersized contract failed");
        std::vector<char> json(required, '\0');
        expect(RowlEngine_GetStreamInfoJson(handle, json.data(),
                                            static_cast<uint32_t>(json.size()),
                                            &repeated) == ROWL_RESULT_OK &&
                   repeated == required && json.back() == '\0',
               "StreamInfo exact-size fetch failed");
        const std::string info(json.data());
        expect(info.find("\"mode\":\"unknown\"") != std::string::npos &&
                   info.find("\"reason\":\"no_bgm\"") != std::string::npos,
               "Fresh StreamInfo must be unknown/no_bgm: " + info);
        expect(RowlEngine_GetStreamInfoJson(nullptr, nullptr, 0, &required) ==
                   ROWL_RESULT_INVALID_HANDLE,
               "StreamInfo dead-handle contract failed");

        // Volume matrix: defaults, clamp, non-finite ignore, dead handles.
        expect(std::abs(RowlEngine_GetBgmVolume(handle) - 1.0f) < 1e-6,
               "Default BGM volume must be 1.0");
        expect(std::abs(RowlEngine_GetAmbienceVolume(handle) - 1.0f) < 1e-6,
               "Default ambience volume must be 1.0");
        expect(std::abs(RowlEngine_GetUiVolume(handle) - 1.0f) < 1e-6,
               "Default UI volume must be 1.0");
        RowlEngine_SetAmbienceVolume(handle, 0.5f);
        RowlEngine_SetUiVolume(handle, 0.25f);
        expect(std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.5f) < 1e-6,
               "Ambience volume set mismatch");
        expect(std::abs(RowlEngine_GetUiVolume(handle) - 0.25f) < 1e-6,
               "UI volume set mismatch");
        RowlEngine_SetAmbienceVolume(handle, -2.0f);
        RowlEngine_SetUiVolume(handle, 7.0f);
        expect(std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.0f) < 1e-6,
               "Ambience clamp low mismatch");
        expect(std::abs(RowlEngine_GetUiVolume(handle) - 1.0f) < 1e-6,
               "UI clamp high mismatch");
        RowlEngine_SetAmbienceVolume(
            handle, std::numeric_limits<float>::quiet_NaN());
        RowlEngine_SetUiVolume(handle,
                               std::numeric_limits<float>::infinity());
        expect(std::abs(RowlEngine_GetAmbienceVolume(handle) - 0.0f) < 1e-6 &&
                   std::abs(RowlEngine_GetUiVolume(handle) - 1.0f) < 1e-6,
               "Non-finite volumes must keep the last valid value");
        expect(RowlEngine_GetBgmVolume(nullptr) == 0.0f &&
                   RowlEngine_GetAmbienceVolume(nullptr) == 0.0f &&
                   RowlEngine_GetUiVolume(nullptr) == 0.0f,
               "Dead-handle volume getters must return 0.0f");

        // End-to-end through the C ABI: stream a patched OGG as BGM.
        const auto projectRoot =
            std::filesystem::temp_directory_path() / "rowl_streaming_capi_project";
        const auto assetDir = projectRoot / "Assets" / "audio";
        std::filesystem::create_directories(assetDir);
        // Patched LONG fixture with CRC fix (tiny caps at 256 frames via
        // ov and can never clear the source-duration gate).
        const auto patched = patchGranuleWithCrcForStream(
            longToneOggBytes(), uint64_t{1000000000});
        {
            std::ofstream f(assetDir / "long_theme.ogg", std::ios::binary);
            f.write(reinterpret_cast<const char*>(patched.data()),
                    static_cast<std::streamsize>(patched.size()));
        }
        RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
        RowlEngine_PlayAudio(handle, "audio/long_theme.ogg", 0, 0);
        RowlEngine_Step(handle, 1.0f / 60.0f);
        expect(RowlEngine_IsStreaming(handle) == 1,
               "C API BGM stream did not report streaming");
        uint32_t streamRequired = 0;
        expect(RowlEngine_GetStreamInfoJson(handle, nullptr, 0,
                                            &streamRequired) == ROWL_RESULT_OK,
               "Stream StreamInfo size query failed");
        std::vector<char> streamJson(streamRequired, '\0');
        expect(RowlEngine_GetStreamInfoJson(
                   handle, streamJson.data(),
                   static_cast<uint32_t>(streamJson.size()),
                   &streamRequired) == ROWL_RESULT_OK,
               "Stream StreamInfo fetch failed");
        expect(std::string(streamJson.data()).find("\"mode\":\"stream\"") !=
                   std::string::npos,
               "C API StreamInfo must report stream mode");

        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        TEST_PASS("C API observability (capability, streaming, volumes)");
    }

    // ── Grup 6: robustness (soak, suspend, device, silent) ──────────
    {
        using namespace Rowl::Audio;
        Rowl::VFS::VFSManager vfs;
        AudioEngine audio(&vfs);
        expect(audio.initialize() && audio.isInitialized(), "Audio init failed");

        const auto projectRoot =
            std::filesystem::temp_directory_path() / "rowl_streaming_soak_project";
        const auto assetDir = projectRoot / "Assets" / "audio";
        std::filesystem::create_directories(assetDir);
        // Patched LONG fixture with CRC fix: tiny caps at 256 frames via
        // ov and can never clear the source-duration gate; the long
        // fixture (88200 frames, patched pcm_total 1e9) streams for real.
        const std::vector<uint8_t> longTone = longToneOggBytes();
        const auto patched = patchGranuleWithCrcForStream(
            longTone, uint64_t{1000000000});
        {
            std::ofstream f(assetDir / "long_theme.ogg", std::ios::binary);
            f.write(reinterpret_cast<const char*>(patched.data()),
                    static_cast<std::streamsize>(patched.size()));
        }
        vfs.remountProject(projectRoot.string());

        audio.setBgmLooping(true);
        audio.playAudio("audio/long_theme.ogg", AudioChannelType::Bgm);
        expect(audio.isStreaming(),
               "Soak stream did not start (patched long must clear the gate)");
        // Virtual-clock soak: 360 x update(1.0f) traverses the same
        // loop-feed/telemetry/stream code as wall-clock playback while CI
        // pays milliseconds, not minutes. RSS gauge mirrors
        // test_audio_engine.cpp (getrusage / GetProcessMemoryInfo).
        const uint64_t rssBefore = currentRssKb();
        for (int i = 0; i < 360; ++i) audio.update(1.0f);
        const uint64_t rssAfter = currentRssKb();
        expect(audio.isStreaming(), "Soak must preserve the stream decision");
        expect(audio.getCurrentBgmPath() == "audio/long_theme.ogg",
               "Soak must preserve the BGM path");
        if (rssBefore > 0 && rssAfter > rssBefore + 8192) {
            fail("Soak RSS grew unboundedly: " + std::to_string(rssBefore) +
                 " KiB -> " + std::to_string(rssAfter) + " KiB");
        }

        // Suspend skips the pump: position is preserved, nothing decodes.
        audio.setOutputSuspended(true);
        const double frozen = audio.bgmStreamBufferedSeconds();
        for (int i = 0; i < 60; ++i) audio.update(1.0f);
        expect(audio.bgmStreamBufferedSeconds() == frozen,
               "Suspended pump must preserve the decode frontier");
        expect(audio.isStreaming(), "Suspend must preserve the stream decision");
        audio.setOutputSuspended(false);
        audio.update(1.0f);
        expect(audio.isStreaming(), "Resume must preserve the stream decision");

        // Device rebuild requeues the ring window (path + granule frontier
        // preserved, no restart from zero).
        audio.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
        expect(audio.isStreaming(), "Device rebuild must preserve streaming");
        expect(audio.getCurrentBgmPath() == "audio/long_theme.ogg",
               "Device rebuild must preserve the BGM path");
        audio.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED);
        expect(audio.isStreaming(), "Format change must preserve streaming");

        // Ring-capacity warmup (loop OFF so the frontier never wraps):
        // the long fixture holds 88200 real frames > 16384-frame capacity,
        // so after a few pumps the ring is full and bufferedSeconds must
        // equal capacity/rate exactly (display-only, never affects
        // playback). Vacuous <=17 s bounds are forbidden here.
        audio.setBgmLooping(false);
        audio.playAudio("audio/long_theme.ogg", AudioChannelType::Bgm);
        expect(audio.isStreaming(), "Capacity warmup did not start streaming");
        audio.update(1.0f);
        audio.update(1.0f);
        audio.update(1.0f);
        {
            const auto probe =
                probeAudioHeaderDuration(longTone.data(), longTone.size());
            expect(probe.known && probe.sampleRateHz != 0,
                   "Capacity probe did not recover the fixture rate");
            const double expectedBuffered =
                static_cast<double>(kStreamRingCapacityFrames) /
                static_cast<double>(probe.sampleRateHz);
            const double buffered = audio.bgmStreamBufferedSeconds();
            expect(std::abs(buffered - expectedBuffered) < 1e-6,
                   "Buffered seconds must equal ring capacity/rate after warmup");
        }

        // Uninitialized engine: silent fallback never streams (fail closed).
        AudioEngine cold(&vfs);
        cold.playAudio("audio/long_theme.ogg", AudioChannelType::Bgm);
        expect(!cold.isStreaming(), "Uninitialized engine must never stream");
        expect(cold.streamInfoJson().find("\"mode\":\"unknown\"") !=
                   std::string::npos,
               "Cold StreamInfo must stay unknown");

        audio.stopAll();
        audio.shutdown();
        std::error_code cleanupError;
        std::filesystem::remove_all(projectRoot, cleanupError);
        TEST_PASS("Robustness (soak, suspend, device rebuild, fail-closed)");
    }
}

/**
 * test_audio_streaming.cpp eklentisi — OGG Seek Forward-No-Reset (#80) KİLİDİ.
 *
 * KILIT (mutant oldurur): Zstd paket giriş-akışında ileri-seek decoder
 * kurulumu (geriye-sarma) YAPMAMALIDIR; yalnızca geri-seek kurulum
 * gerektirir. seekTo() içindeki eski koşulsuz reset() geri gelirse (veya
 * ileri-dal kuruluma bağlanırsa) aşağıdaki üretim-metriği gözlemleri exit(1)
 * ile düşer (bayt-eşitliği mutantı GEÇİRİR — sayaç öldürür):
 *  (kalibrasyon) akış-açılışı: rewind-delta 1 + compressed/decompressed
 *      delta 0 (açılış kurulumu initDecoder() tek-sarmalayıcısından geçer;
 *      doğrudan-init bypass delta 0 ile düşer),
 *  (a) ileri-seek (derin konumdan): rewind-delta 0 + yeniden-decompress <=
 *      hedef-current+64KB (kalibrasyon: 64KB çıktı-chunk payı) +
 *      monoton-ilerleme + compressed-delta <= decomp-delta+64KB +
 *      compressed-delta <= kalan-compressed+64KB + bayt-eşitliği + tellg
 *      (16MiB sıkışabilir entry),
 *  (b) aynı-konum seek → no-op, rewind-delta 0,
 *  (c) SEEK_CUR ileri → rewind-delta 0 + decompress-sınırı (alt+üst) +
 *      compressed-tutarlılığı + bayt-eşitliği,
 *  (d) geri-seek (beg 0) → rewind-delta >= 1 + bayt-eşitliği,
 *  (e) SEEK_END ileri (ORTA-KUYRUK: önce blobSize-5MiB konumuna ilerle,
 *      sonra SEEK_END-512): rewind-delta 0 + decompress-sınırı (alt+üst) +
 *      compressed-tutarlılığı + kuyruk-eşitliği. Mesafe ~5MiB iken
 *      sıfırdan-çözüm ~16MiB üretir ve üst-sınırda ölür (eski baştan-kuyruk
 *      varyantı sayısal-boştu: sıfırdan maliyet mesafe+64KB içinde kalırdı),
 *  (f) sınır-aşımı seek → fail-closed (failbit),
 *  (g) OGG uçtan-uca durum-eşitliği (reset-serbest adım): paket-akışı
 *      üzerinden PCM ileri/geri seek'ler bellek-içi referans decode ile
 *      bayt-aynı (memcmp); reset iddiası taşımaz,
 *  (h) negatif SEEK_CUR: geri-hedef rewind-delta >= 1 + tam-yeniden-decode
 *      bandı (hedef <= decomp <= hedef+64KB) + bayt-eşitliği; başlangıç-ötesi
 *      negatif offset fail-closed (failbit),
 *  (i) bozuk-payload: sıkıştırılmış baytı çevrilmiş girişin akışı açılsa bile
 *      ilk okumada fail-closed (failbit, eksik bayt).
 *
 * Gözlem notları (desen aynen: TEST_SECTION/TEST_PASS + hata=exit(1)):
 *  - Sayaçlar Rowl::VFS::zstdEntryStream{RewindCount,CompressedBytes,
 *    DecompressedBytes} üretim metrikleridir (GERÇEK I/O olayları; test-seam
 *    değildir). Süreç-geneli monoton sayaçlardır: baz değeri HER seek'ten
 *    hemen önce alınır, yalnız delta karşılaştırılır (mutlak değere bel
 *    bağlanmaz).
 *  - (g) reset-serbesttir, reset-delta İDDİA EDİLMEZ: libvorbis ov_pcm_seek
 *    ikili-arama (bisection) sırasında bayt-düzeyinde meşru geri-seek
 *    yapabilir; orada kilitlenen yalnız durum-eşitliğidir.
 *  - Yamanmış granule (1e9) ov_pcm_total'u şişirir; PCM seek sınırları
 *    GERÇEK frame sayısına (88200) göre seçilir, total'a göre değil.
 *  - timing-assert/sleep/poll YOKTUR; cihaz gerektirmez (headless koşar,
 *    SKIP yok — mandal decode-düzeyindedir, SDL akışına dokunmaz).
 */
void test_audio_seek_forward_no_reset() {
    TEST_SECTION("Audio OGG Seek — Forward-No-Reset (#80)");

    // Fixture: CRC-onarımlı granule-yamalı uzun OGG (gerçek 88200 frame,
    // stereo 44100 Hz) + 16 MiB desenli blob; ikisi de tek .rowlpkg içinde
    // zstd (flags=1) — sıkıştırılmış-girdi seek yolu budur. Blob deseni
    // sıkışabilir tutulur (64 baytlık koşular, 0..255 döngüsü), paket küçük
    // kalır; kurulum mevcut fixture üretim helper'larıyla yapılır
    // (compressEntry + FNV-1a + ham başlık/kayıt yazımı).
    const std::vector<uint8_t> ogg = patchGranuleWithCrcForStream(
        longToneOggBytes(), static_cast<uint64_t>(1000000000));
    constexpr uint64_t kSeekBlobBytes = 16ULL * 1024 * 1024;
    constexpr uint64_t kSeekChunkSlack = 64ULL * 1024;
    std::string blob(static_cast<size_t>(kSeekBlobBytes), '\0');
    for (size_t i = 0; i < blob.size(); ++i) {
        blob[i] = static_cast<char>((i / 64u) % 256u);
    }
    auto compressEntry = [](const void* data, size_t size) {
        std::vector<uint8_t> out(ZSTD_compressBound(size));
        const size_t n = ZSTD_compress(out.data(), out.size(), data, size, 1);
        if (ZSTD_isError(n)) fail("Seek fixture Zstd compress failed");
        out.resize(n);
        return out;
    };
    const std::vector<uint8_t> oggComp =
        compressEntry(ogg.data(), ogg.size());
    const std::vector<uint8_t> blobComp =
        compressEntry(blob.data(), blob.size());

    const auto root =
        std::filesystem::temp_directory_path() / "rowl_audio_seek_project";
    std::filesystem::create_directories(root);
    const auto pkgPath = root / "seek.rowlpkg";
    const std::string oggName = "audio/seek_theme.ogg";
    const std::string blobName = "audio/seek_blob.bin";
    auto fnv1a64 = [](const std::string& value) {
        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    constexpr uint64_t kHeaderSize = sizeof(Rowl::VFS::RowlPkgHeader);
    const uint64_t oggOffset = kHeaderSize;
    const uint64_t blobOffset = oggOffset + oggComp.size();
    const uint64_t indexOffset = blobOffset + blobComp.size();
    const Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 2, indexOffset};
    const Rowl::VFS::RowlPkgEntryRaw oggEntry{
        fnv1a64(oggName), static_cast<uint32_t>(oggName.size()), oggOffset,
        oggComp.size(), ogg.size(), 1};
    const Rowl::VFS::RowlPkgEntryRaw blobEntry{
        fnv1a64(blobName), static_cast<uint32_t>(blobName.size()), blobOffset,
        blobComp.size(), blob.size(), 1};
    {
        std::ofstream out(pkgPath, std::ios::binary);
        expect(!!out, "Seek package could not be created");
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(oggComp.data()),
                  static_cast<std::streamsize>(oggComp.size()));
        out.write(reinterpret_cast<const char*>(blobComp.data()),
                  static_cast<std::streamsize>(blobComp.size()));
        out.write(reinterpret_cast<const char*>(&oggEntry), sizeof(oggEntry));
        out.write(oggName.data(), static_cast<std::streamsize>(oggName.size()));
        out.write(reinterpret_cast<const char*>(&blobEntry), sizeof(blobEntry));
        out.write(blobName.data(), static_cast<std::streamsize>(blobName.size()));
        out.close();
        expect(!!out, "Seek package write failed");
    }

    // Bayt-düzeyi kilit: ham Zstd giriş-akışı (libvorbis yok — tam deterministik).
    // (kalibrasyon) Açılış gözlemi: openStream'ten hemen önce baz alınır.
    // Akış-açılışı tek kurulumdur (rewind-delta 1), henüz I/O yoktur
    // (compressed/decompressed delta 0). Bu adımda yalnız blob akışı canlıdır.
    const uint64_t openBaseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
    const uint64_t openBaseComp = Rowl::VFS::zstdEntryStreamCompressedBytes();
    const uint64_t openBaseDecomp = Rowl::VFS::zstdEntryStreamDecompressedBytes();
    Rowl::VFS::RowlPkgDataSource source(pkgPath.string());
    expect(source.isValid(), "Seek package did not validate");
    auto stream = source.openStream(blobName);
    expect(stream && stream->good(), "Seek blob stream did not open");
    expect(Rowl::VFS::zstdEntryStreamRewindCount() == openBaseRewinds + 1,
           "Stream open must perform exactly one decoder setup (direct-init bypass)");
    expect(Rowl::VFS::zstdEntryStreamCompressedBytes() == openBaseComp,
           "Stream open must not consume compressed bytes");
    expect(Rowl::VFS::zstdEntryStreamDecompressedBytes() == openBaseDecomp,
           "Stream open must not produce decompressed bytes");
    TEST_PASS("Seek lock — stream open performs exactly one counted decoder setup");
    const uint64_t blobSize = static_cast<uint64_t>(blob.size());
    const uint64_t blobCompSize = static_cast<uint64_t>(blobComp.size());

    std::vector<char> probe(1024, 0);
    stream->read(probe.data(), static_cast<std::streamsize>(probe.size()));
    expect(stream->gcount() == static_cast<std::streamsize>(probe.size()) &&
               std::memcmp(probe.data(), blob.data(), probe.size()) == 0,
           "Seek blob head mismatch");

    // (a) İleri-seek (derin konumdan): rewind YOK + yeniden-decompress <=
    // mesafe+64KB + monoton-ilerleme + compressed-tutarlılığı +
    // bayt-eşitliği + tellg. Koşulsuz-reset mutantı hedefi baştan çözerek
    // rewind sayacında ve decompress-sınırında ölür (bayt-eşitliği mutantı
    // geçirir); israf-prefetch mutantı (doğruluk aynı, fazla sıkıştırılmış
    // I/O) compressed-sınırlarında ölür (decomp sayacı onu geçirir).
    {
        // Mutantı ayırt edecek derin konuma ilerle (8MiB): buradan 12MiB'e
        // ileri-seek mesafesi ~4MiB olur; mutant 12MiB'yi baştan çözer.
        constexpr uint64_t kDeepPos = 8ULL * 1024 * 1024;
        constexpr uint64_t kTarget = 12ULL * 1024 * 1024;
        stream->clear();
        stream->seekg(static_cast<std::streamoff>(kDeepPos), std::ios::beg);
        expect(stream->good(), "Deep-position seek failed");
        std::vector<char> deep(512, 0);
        stream->read(deep.data(), static_cast<std::streamsize>(deep.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(deep.size()) &&
                   std::memcmp(deep.data(), blob.data() + kDeepPos, deep.size()) == 0,
               "Deep-position read diverges from payload");
        const uint64_t current =
            static_cast<uint64_t>(static_cast<std::streamoff>(stream->tellg()));
        expect(current == kDeepPos + deep.size(), "Deep-position tell mismatch");
        const uint64_t distance = kTarget - current;

        const uint64_t baseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
        const uint64_t baseDecomp = Rowl::VFS::zstdEntryStreamDecompressedBytes();
        const uint64_t baseComp = Rowl::VFS::zstdEntryStreamCompressedBytes();
        stream->clear();
        stream->seekg(static_cast<std::streamoff>(kTarget), std::ios::beg);
        expect(stream->good(), "Forward seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() == baseRewinds,
               "Forward seek rewound the Zstd decoder (must advance without rewind)");
        const uint64_t decompDelta =
            Rowl::VFS::zstdEntryStreamDecompressedBytes() - baseDecomp;
        expect(decompDelta <= distance + kSeekChunkSlack,
               "Forward seek re-decompressed beyond target-current+64KB");
        expect(decompDelta + kSeekChunkSlack >= distance,
               "Forward seek made no monotonic decode progress");
        // Compressed-bacağı: israf-prefetch kilidi. Fixture sıkışabilir
        // olduğu için doğru kod, ürettiğinden fazla sıkıştırılmış bayt
        // tüketemez (fazlası en çok bir 64KB girdi-chunk'udur); ayrıca girişin
        // kalan sıkıştırılmışından fazlasını okuyamaz.
        const uint64_t compDelta =
            Rowl::VFS::zstdEntryStreamCompressedBytes() - baseComp;
        expect(compDelta <= decompDelta + kSeekChunkSlack,
               "Forward seek consumed compressed bytes without producing output (wasteful prefetch)");
        const uint64_t remainingComp = blobCompSize - (baseComp - openBaseComp);
        expect(compDelta <= remainingComp + kSeekChunkSlack,
               "Forward seek consumed beyond remaining compressed bytes");
        expect(static_cast<std::streamoff>(stream->tellg()) ==
                   static_cast<std::streamoff>(kTarget),
               "Forward seek tell mismatch");
        stream->read(probe.data(), static_cast<std::streamsize>(probe.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(probe.size()) &&
                   std::memcmp(probe.data(), blob.data() + kTarget, probe.size()) == 0,
               "Forward-seek read diverges from payload");
        TEST_PASS("Seek lock — forward seek bounded re-decode without rewind (16MiB entry)");
    }

    // (b) Aynı-konum seek: no-op, rewind YOK.
    {
        const std::streamoff here =
            static_cast<std::streamoff>(stream->tellg());
        const uint64_t base = Rowl::VFS::zstdEntryStreamRewindCount();
        stream->seekg(here, std::ios::beg);
        expect(stream->good(), "Same-position seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() == base,
               "Same-position seek must be a no-op (no rewind)");
        TEST_PASS("Seek lock — same-position seek is a no-op (delta 0)");
    }

    // (c) SEEK_CUR ileri: rewind YOK + decompress-sınırı (alt+üst) +
    // compressed-tutarlılığı + bayt-eşitliği.
    {
        const uint64_t baseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
        const uint64_t baseDecomp = Rowl::VFS::zstdEntryStreamDecompressedBytes();
        const uint64_t baseComp = Rowl::VFS::zstdEntryStreamCompressedBytes();
        stream->seekg(1000, std::ios::cur);
        expect(stream->good(), "SEEK_CUR forward seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() == baseRewinds,
               "SEEK_CUR forward seek rewound the decoder (must advance without rewind)");
        const uint64_t curDecompDelta =
            Rowl::VFS::zstdEntryStreamDecompressedBytes() - baseDecomp;
        expect(curDecompDelta <= 1000 + kSeekChunkSlack,
               "SEEK_CUR forward seek re-decompressed beyond 1000+64KB");
        expect(curDecompDelta + kSeekChunkSlack >= 1000,
               "SEEK_CUR forward seek made no monotonic decode progress");
        const uint64_t curCompDelta =
            Rowl::VFS::zstdEntryStreamCompressedBytes() - baseComp;
        expect(curCompDelta <= curDecompDelta + kSeekChunkSlack,
               "SEEK_CUR forward seek consumed compressed bytes without producing output");
        const uint64_t curRemainingComp = blobCompSize - (baseComp - openBaseComp);
        expect(curCompDelta <= curRemainingComp + kSeekChunkSlack,
               "SEEK_CUR forward seek consumed beyond remaining compressed bytes");
        const uint64_t pos =
            static_cast<uint64_t>(static_cast<std::streamoff>(stream->tellg()));
        std::vector<char> cur(512, 0);
        stream->read(cur.data(), static_cast<std::streamsize>(cur.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(cur.size()) &&
                   std::memcmp(cur.data(), blob.data() + pos, cur.size()) == 0,
               "SEEK_CUR forward read diverges from payload");
        TEST_PASS("Seek lock — SEEK_CUR forward skips decoder rewind (delta 0, bytes exact)");
    }

    // (d) Geri-seek: rewind VAR + bayt-eşitliği.
    {
        const uint64_t base = Rowl::VFS::zstdEntryStreamRewindCount();
        stream->clear();
        stream->seekg(0, std::ios::beg);
        expect(stream->good(), "Backward seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() >= base + 1,
               "Backward seek must rewind the decoder");
        stream->read(probe.data(), static_cast<std::streamsize>(probe.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(probe.size()) &&
                   std::memcmp(probe.data(), blob.data(), probe.size()) == 0,
               "Backward-seek head read diverges from payload");
        TEST_PASS("Seek lock — backward seek rewinds the decoder (delta >= 1, bytes exact)");
    }

    // (e) SEEK_END ileri — ORTA-KUYRUK: önce blobSize-5MiB konumuna ilerle
    // (derin konum), sonra SEEK_END-512. Mesafe ~5MiB iken sıfırdan-çözüm
    // ~16MiB üretir ve üst-sınırda ölür. Eski baştan-kuyruk varyantı
    // sayısal-boştu: konum baştayken sıfırdan maliyet (≈tailTarget) mesafe
    // (≈tailTarget-1024)+64KB içinde kalıyordu; sessiz-sıfırdan-mutantı yalnız
    // rewind sayacına yakalanıyordu (sayacı da gizlerse yeşil geçerdi).
    {
        constexpr uint64_t kMidTailAhead = 5ULL * 1024 * 1024;
        const uint64_t kMidPos = blobSize - kMidTailAhead;
        stream->clear();
        stream->seekg(static_cast<std::streamoff>(kMidPos), std::ios::beg);
        expect(stream->good(), "Mid-tail positioning seek failed");
        std::vector<char> mid(512, 0);
        stream->read(mid.data(), static_cast<std::streamsize>(mid.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(mid.size()) &&
                   std::memcmp(mid.data(), blob.data() + kMidPos, mid.size()) == 0,
               "Mid-tail positioning read diverges from payload");
        const uint64_t endCurrent =
            static_cast<uint64_t>(static_cast<std::streamoff>(stream->tellg()));
        expect(endCurrent == kMidPos + mid.size(), "Mid-tail tell mismatch");
        const uint64_t tailTarget = blobSize - 512;
        const uint64_t endDistance = tailTarget - endCurrent;
        expect(endDistance == kMidTailAhead - 1024,
               "Mid-tail distance must be 5MiB-1024 (loose-bound regression guard)");
        const uint64_t baseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
        const uint64_t baseDecomp = Rowl::VFS::zstdEntryStreamDecompressedBytes();
        const uint64_t baseComp = Rowl::VFS::zstdEntryStreamCompressedBytes();
        stream->clear();
        stream->seekg(-512, std::ios::end);
        expect(stream->good(), "SEEK_END forward seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() == baseRewinds,
               "SEEK_END forward seek rewound the decoder (must advance without rewind)");
        const uint64_t endDecompDelta =
            Rowl::VFS::zstdEntryStreamDecompressedBytes() - baseDecomp;
        expect(endDecompDelta <= endDistance + kSeekChunkSlack,
               "SEEK_END forward seek re-decompressed beyond target-current+64KB");
        expect(endDecompDelta + kSeekChunkSlack >= endDistance,
               "SEEK_END forward seek made no monotonic decode progress");
        const uint64_t endCompDelta =
            Rowl::VFS::zstdEntryStreamCompressedBytes() - baseComp;
        expect(endCompDelta <= endDecompDelta + kSeekChunkSlack,
               "SEEK_END forward seek consumed compressed bytes without producing output");
        const uint64_t endRemainingComp = blobCompSize - (baseComp - openBaseComp);
        expect(endCompDelta <= endRemainingComp + kSeekChunkSlack,
               "SEEK_END forward seek consumed beyond remaining compressed bytes");
        std::vector<char> tail(512, 0);
        stream->read(tail.data(), static_cast<std::streamsize>(tail.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(tail.size()) &&
                   std::memcmp(tail.data(), blob.data() + blobSize - 512,
                               tail.size()) == 0,
               "SEEK_END forward tail diverges from payload");
        TEST_PASS("Seek lock — SEEK_END forward skips decoder rewind (delta 0, tail exact)");
    }

    // (f) Sınır-aşımı seek: fail-closed (konum oynamaz, failbit kurulur).
    {
        stream->clear();
        stream->seekg(static_cast<std::streamoff>(blobSize + 1), std::ios::beg);
        expect(stream->fail(), "Out-of-range seek must fail closed");
        TEST_PASS("Seek lock — out-of-range seek fails closed");
    }

    // (g) OGG uçtan-uca durum-eşitliği (reset-serbest adım: reset iddiası
    // taşımaz, OGG üretim sayacına bağlanmaz): paket-akışı üzerinden PCM
    // ileri/geri seek'ler bellek-içi referans decode ile bayt-aynıdır.
    // Gerekçe: libvorbis ov_pcm_seek ikili-arama sırasında bayt-düzeyinde
    // meşru geri-seek yapabilir; burada kilitlenen yalnız durum-eşitliğidir.
    {
        using namespace Rowl::Audio;
        Rowl::VFS::VFSManager vfs;
        vfs.mountPackage("", pkgPath.string());
        OggStreamSource src;
        std::string oggError;
        expect(src.open(vfs, oggName, oggError),
               "Seek OGG open failed: " + oggError);
        const uint32_t rate = src.sampleRateHz();
        const uint32_t channels = src.channelCount();
        expect(rate != 0 && channels != 0 && channels <= 8,
               "Seek OGG format invalid");

        // Referans: aynı baytlar bellek-akışından tam-decode (parity deseni).
        std::string oggBytes(reinterpret_cast<const char*>(ogg.data()), ogg.size());
        auto refStream =
            std::make_unique<std::istringstream>(oggBytes, std::ios::binary);
        OggVorbis_File decoder{};
        const ov_callbacks callbacks{parityVorbisRead, parityVorbisSeek,
                                     parityVorbisClose, parityVorbisTell};
        expect(ov_open_callbacks(refStream.get(), &decoder, nullptr, 0,
                                 callbacks) == 0,
               "Seek reference ov_open failed");
        constexpr float kS16ToFloat = 1.0f / 32768.0f;
        std::vector<float> ref;
        {
            std::array<char, 32 * 1024> chunk{};
            int bitstream = 0;
            while (true) {
                const long bytes = ov_read(&decoder, chunk.data(),
                                           static_cast<int>(chunk.size()),
                                           0, 2, 1, &bitstream);
                expect(bytes >= 0, "Seek reference hit corrupt stream");
                if (bytes == 0) break;
                const size_t samples = static_cast<size_t>(bytes) / 2;
                for (size_t i = 0; i < samples; ++i) {
                    int16_t sample = 0;
                    std::memcpy(&sample, chunk.data() + i * 2, sizeof(sample));
                    ref.push_back(static_cast<float>(sample) * kS16ToFloat);
                }
            }
        }
        ov_clear(&decoder);
        expect(!ref.empty() && ref.size() % channels == 0,
               "Seek reference decode invalid");

        // İleri PCM seek (gerçek aralıkta) → referansla bayt-aynı.
        constexpr uint64_t kSeekFrame = 44100;
        expect(src.seekPcmFrame(kSeekFrame), "Seek OGG forward PCM seek failed");
        std::vector<float> fwd(4096u * 8u, 0.0f);
        bool eos = false;
        const size_t got = src.readFloatFrames(fwd.data(), 4096u, eos);
        expect(got == 4096u, "Seek OGG forward read fell short");
        expect(ref.size() >= (kSeekFrame + 4096u) * channels,
               "Seek reference too short for forward window");
        expect(std::memcmp(fwd.data(), ref.data() + kSeekFrame * channels,
                           4096u * channels * sizeof(float)) == 0,
               "Forward PCM seek diverges from reference decode");

        // Geri PCM seek (0) → baş-eşitliği.
        expect(src.seekPcmFrame(0), "Seek OGG backward PCM seek failed");
        std::vector<float> head(64u * 8u, 0.0f);
        const size_t gotHead = src.readFloatFrames(head.data(), 64u, eos);
        expect(gotHead > 0 &&
                   std::memcmp(head.data(), ref.data(),
                               gotHead * channels * sizeof(float)) == 0,
               "Backward PCM seek diverges from stream head");
        src.close();
        TEST_PASS("Seek lock — OGG forward/backward PCM seeks match reference decode (memcmp)");
    }

    // (h) Negatif SEEK_CUR: geri-hedef decoder kurulumu gerektirir
    // (rewind-delta >= 1) ve tam-sıfırdan-decode bandında üretilir
    // (hedef <= decomp-delta <= hedef+64KB); başlangıç-ötesi negatif offset
    // fail-closed'dur (failbit).
    {
        stream->clear();
        constexpr uint64_t kNegAnchor = 1000000;
        stream->seekg(static_cast<std::streamoff>(kNegAnchor), std::ios::beg);
        expect(stream->good(), "Negative-SEEK_CUR anchor seek failed");
        std::vector<char> anchor(512, 0);
        stream->read(anchor.data(), static_cast<std::streamsize>(anchor.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(anchor.size()) &&
                   std::memcmp(anchor.data(), blob.data() + kNegAnchor, anchor.size()) == 0,
               "Negative-SEEK_CUR anchor read diverges from payload");
        const uint64_t negCurrent =
            static_cast<uint64_t>(static_cast<std::streamoff>(stream->tellg()));
        expect(negCurrent == kNegAnchor + anchor.size(), "Negative-SEEK_CUR anchor tell mismatch");
        constexpr uint64_t kNegBack = 500000;
        const uint64_t negTarget = negCurrent - kNegBack;
        const uint64_t negBaseRewinds = Rowl::VFS::zstdEntryStreamRewindCount();
        const uint64_t negBaseDecomp = Rowl::VFS::zstdEntryStreamDecompressedBytes();
        stream->seekg(-static_cast<std::streamoff>(kNegBack), std::ios::cur);
        expect(stream->good(), "Negative SEEK_CUR backward seek failed");
        expect(Rowl::VFS::zstdEntryStreamRewindCount() >= negBaseRewinds + 1,
               "Negative SEEK_CUR backward seek must rewind the decoder");
        const uint64_t negDecompDelta =
            Rowl::VFS::zstdEntryStreamDecompressedBytes() - negBaseDecomp;
        expect(negDecompDelta >= negTarget &&
                   negDecompDelta <= negTarget + kSeekChunkSlack,
               "Backward SEEK_CUR must fully re-decode from scratch (target <= decomp <= target+64KB)");
        expect(static_cast<std::streamoff>(stream->tellg()) ==
                   static_cast<std::streamoff>(negTarget),
               "Negative SEEK_CUR tell mismatch");
        std::vector<char> negBack(512, 0);
        stream->read(negBack.data(), static_cast<std::streamsize>(negBack.size()));
        expect(stream->gcount() == static_cast<std::streamsize>(negBack.size()) &&
                   std::memcmp(negBack.data(), blob.data() + negTarget, negBack.size()) == 0,
               "Negative-SEEK_CUR backward read diverges from payload");
        TEST_PASS("Seek lock — negative SEEK_CUR rewinds with full scratch re-decode (bytes exact)");

        // Başlangıç-ötesi negatif offset: fail-closed.
        stream->clear();
        const uint64_t failPos =
            static_cast<uint64_t>(static_cast<std::streamoff>(stream->tellg()));
        stream->seekg(-static_cast<std::streamoff>(failPos + 1), std::ios::cur);
        expect(stream->fail(), "Before-start negative SEEK_CUR must fail closed");
        TEST_PASS("Seek lock — before-start negative SEEK_CUR fails closed");
    }

    // (i) Bozuk-payload fail-closed: sıkıştırılmış baytı çevrilmiş girişin
    // dizin kaydı sağlam kalır (paket validate olur), ama ilk okuma decoder
    // hatasıyla düşer (failbit, eksik bayt) — sessiz-bozukluk asla bayt
    // üretmez.
    {
        // Çerçeve-başlığı çevrilir (ilk baytlar): ilk fill() çağrısı decoder
        // hatasıyla düşer, akış tek bayt bile üretmeden fail-closed olur.
        std::vector<uint8_t> corruptComp = blobComp;
        corruptComp[0] ^= 0xFF;
        corruptComp[1] ^= 0xFF;
        const uint64_t corruptBlobOffset = oggOffset + oggComp.size();
        const uint64_t corruptIndexOffset = corruptBlobOffset + corruptComp.size();
        const Rowl::VFS::RowlPkgHeader corruptHeader{{'R', 'O', 'W', 'L'}, 1, 2, corruptIndexOffset};
        const Rowl::VFS::RowlPkgEntryRaw corruptOggEntry{
            fnv1a64(oggName), static_cast<uint32_t>(oggName.size()), oggOffset,
            oggComp.size(), ogg.size(), 1};
        const Rowl::VFS::RowlPkgEntryRaw corruptBlobEntry{
            fnv1a64(blobName), static_cast<uint32_t>(blobName.size()), corruptBlobOffset,
            corruptComp.size(), blob.size(), 1};
        const auto corruptPkgPath = root / "seek_corrupt.rowlpkg";
        {
            std::ofstream out(corruptPkgPath, std::ios::binary);
            expect(!!out, "Corrupt seek package could not be created");
            out.write(reinterpret_cast<const char*>(&corruptHeader), sizeof(corruptHeader));
            out.write(reinterpret_cast<const char*>(oggComp.data()),
                      static_cast<std::streamsize>(oggComp.size()));
            out.write(reinterpret_cast<const char*>(corruptComp.data()),
                      static_cast<std::streamsize>(corruptComp.size()));
            out.write(reinterpret_cast<const char*>(&corruptOggEntry), sizeof(corruptOggEntry));
            out.write(oggName.data(), static_cast<std::streamsize>(oggName.size()));
            out.write(reinterpret_cast<const char*>(&corruptBlobEntry), sizeof(corruptBlobEntry));
            out.write(blobName.data(), static_cast<std::streamsize>(blobName.size()));
            out.close();
            expect(!!out, "Corrupt seek package write failed");
        }
        Rowl::VFS::RowlPkgDataSource corruptSource(corruptPkgPath.string());
        expect(corruptSource.isValid(), "Corrupt package index must still validate");
        auto corruptStream = corruptSource.openStream(blobName);
        if (corruptStream && corruptStream->good()) {
            std::vector<char> corruptProbe(1024, 0);
            corruptStream->read(corruptProbe.data(),
                                static_cast<std::streamsize>(corruptProbe.size()));
            expect(corruptStream->gcount() < static_cast<std::streamsize>(corruptProbe.size()) &&
                       corruptStream->fail(),
                   "Corrupt payload read must fail closed (short read + failbit)");
        }
        TEST_PASS("Seek lock — corrupt payload fails closed (no silent bytes)");
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
}
