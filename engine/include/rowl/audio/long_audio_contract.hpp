/**
 * rowl/audio/long_audio_contract.hpp
 *
 * Faz 4.5 Dilim 3 — long-audio threshold + contract + soak helpers.
 *
 * Read-only companion to the audio decode/playback path. This header NEVER
 * touches decoding or playback state; it owns:
 *   - the 64 MiB decoded-PCM budget and the maxSeconds formula,
 *   - container-header duration probes (WAV / OGG, no full decode),
 *   - the warn + structured-result assessment.
 *
 * The decode translation unit (engine/src/audio/audio_engine.cpp) is
 * intentionally NOT modified: its 64 MiB encoded/decoded caps remain the
 * enforcement point. This header is the advisory contract in front of it.
 *
 * Platform notes: pure header math, no OS calls and no audio-device access,
 * so the Linux + Windows path works identically and no mobile-specific path
 * is introduced or blocked.
 */

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "rowl/core/logger.hpp"

namespace Rowl::Audio {

// Decoded-PCM budget shared with the decode path's kMaxDecodedAudioBytes
// (64 MiB). Kept as a named constant here so unit tests pin the 64 MiB
// basis; the decode TU keeps its own (identical) limit as the enforcement
// point. Do NOT retune one without the other (see LONG_AUDIO_CONTRACT.md).
inline constexpr uint64_t kLongAudioBudgetBytes = 64ULL * 1024 * 1024;

/**
 * maxSeconds = 64 MiB / (sampleRateHz * channelCount * bytesPerSample).
 * bytesPerSample is decoded PCM bytes (WAV: bitsPerSample/8;
 * OGG/Vorbis: the engine decodes to S16, so 2).
 * Degenerate or non-finite inputs yield 0.0 (fail closed: unknown audio
 * never claims a usable threshold).
 */
inline double longAudioThresholdSeconds(uint32_t sampleRateHz,
                                        uint32_t channelCount,
                                        uint32_t bytesPerSample) {
    if (sampleRateHz == 0 || channelCount == 0 || bytesPerSample == 0) return 0.0;
    const double bytesPerSecond = static_cast<double>(sampleRateHz) *
                                  static_cast<double>(channelCount) *
                                  static_cast<double>(bytesPerSample);
    if (!(bytesPerSecond > 0.0) || !std::isfinite(bytesPerSecond)) return 0.0;
    const double seconds =
        static_cast<double>(kLongAudioBudgetBytes) / bytesPerSecond;
    if (!std::isfinite(seconds) || !(seconds > 0.0)) return 0.0;
    return seconds;
}

/** Result of a container-header probe. `known` means the container was
 *  recognized and its format parsed; `durationSeconds` is the probed
 *  duration, or negative when the header does not determine one. */
struct AudioHeaderInfo {
    bool known = false;
    uint32_t sampleRateHz = 0;
    uint32_t channelCount = 0;
    uint32_t bytesPerSample = 0;
    double durationSeconds = -1.0;
};

namespace LongAudioDetail {

inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t readU64LE(const uint8_t* p) {
    uint64_t low = readU32LE(p);
    uint64_t high = readU32LE(p + 4);
    return low | (high << 32);
}

// Exact WAV duration from the fmt/data chunk headers only. The data chunk's
// ADVERTISED size is trusted deliberately: a header that claims more PCM
// than the 64 MiB budget is exactly the over-threshold intent this probe
// must catch, without allocating or decoding a single sample. Reads are
// always clamped to the available buffer (no OOB on truncated inputs).
inline AudioHeaderInfo probeWavHeader(const uint8_t* data, size_t size) {
    AudioHeaderInfo out;
    if (data == nullptr || size < 12) return out;
    if (std::memcmp(data, "RIFF", 4) != 0 ||
        std::memcmp(data + 8, "WAVE", 4) != 0) {
        return out;
    }
    uint32_t sampleRate = 0;
    uint32_t byteRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t dataBytes = 0;
    bool haveFmt = false;
    bool haveData = false;
    size_t offset = 12;
    for (int chunks = 0; chunks < 64 && offset + 8 <= size; ++chunks) {
        const uint32_t chunkSize = readU32LE(data + offset + 4);
        const size_t payload = offset + 8;
        if (std::memcmp(data + offset, "fmt ", 4) == 0) {
            if (payload + 16 <= size) {
                channels = readU16LE(data + payload + 2);
                sampleRate = readU32LE(data + payload + 4);
                byteRate = readU32LE(data + payload + 8);
                bitsPerSample = readU16LE(data + payload + 14);
                haveFmt = true;
            }
        } else if (std::memcmp(data + offset, "data", 4) == 0) {
            dataBytes = chunkSize;
            haveData = true;
        }
        if (haveFmt && haveData) break;
        const uint64_t next =
            static_cast<uint64_t>(payload) + chunkSize + (chunkSize & 1u);
        if (next <= offset || next > static_cast<uint64_t>(size)) break;
        offset = static_cast<size_t>(next);
    }
    if (!haveFmt || sampleRate == 0 || channels == 0) return out;
    out.sampleRateHz = sampleRate;
    out.channelCount = channels;
    if (bitsPerSample >= 8 && bitsPerSample % 8 == 0) {
        out.bytesPerSample = static_cast<uint32_t>(bitsPerSample / 8);
    } else if (byteRate > 0) {
        const uint64_t frame = static_cast<uint64_t>(sampleRate) * channels;
        if (frame > 0 && byteRate % frame == 0) {
            out.bytesPerSample = byteRate / static_cast<uint32_t>(frame);
        }
    }
    if (out.bytesPerSample == 0) return out;
    out.known = true;
    if (haveData) {
        const double bytesPerSecond = static_cast<double>(byteRate) > 0.0
            ? static_cast<double>(byteRate)
            : static_cast<double>(sampleRate) * channels * out.bytesPerSample;
        if (bytesPerSecond > 0.0) {
            out.durationSeconds =
                static_cast<double>(dataBytes) / bytesPerSecond;
        }
    }
    return out;
}

// OGG/Vorbis duration from headers + trailing granule position only.
// Format comes from the first page's Vorbis identification header
// (channels, sample rate); duration comes from the maximum page granule
// position (total PCM samples, Vorbis mapping) divided by the rate.
// No packet is ever decoded. bytesPerSample is 2: the engine decodes
// Vorbis to S16 PCM before float conversion.
inline AudioHeaderInfo probeOggHeader(const uint8_t* data, size_t size) {
    AudioHeaderInfo out;
    if (data == nullptr || size < 27 + 30) return out;
    if (std::memcmp(data, "OggS", 4) != 0) return out;
    const uint8_t firstSegments = data[26];
    const size_t idOffset = static_cast<size_t>(27) + firstSegments;
    if (idOffset + 30 > size) return out;
    const uint8_t* id = data + idOffset;
    if (id[0] != 0x01 || std::memcmp(id + 1, "vorbis", 6) != 0) return out;
    if (readU32LE(id + 7) != 0) return out; // Vorbis version must be 0.
    const uint8_t channels = id[11];
    const uint32_t rate = readU32LE(id + 12);
    if (channels == 0 || channels > 8 || rate == 0) return out;
    out.sampleRateHz = rate;
    out.channelCount = channels;
    out.bytesPerSample = 2;
    out.known = true;
    // Ogg pages are contiguous: walk them and keep the maximum valid
    // granule (0xFFFF... means "no packet ends here", not a duration).
    uint64_t maxGranule = 0;
    bool haveGranule = false;
    size_t offset = 0;
    for (int pages = 0; pages < 1000000 && offset + 27 <= size; ++pages) {
        if (std::memcmp(data + offset, "OggS", 4) != 0) break;
        const uint8_t segCount = data[offset + 26];
        if (offset + 27 + segCount > size) break;
        uint64_t body = 0;
        for (uint32_t i = 0; i < segCount; ++i) body += data[offset + 27 + i];
        const uint64_t granule = readU64LE(data + offset + 6);
        if (granule != UINT64_C(0xFFFFFFFFFFFFFFFF)) {
            if (!haveGranule || granule > maxGranule) maxGranule = granule;
            haveGranule = true;
        }
        const uint64_t next =
            static_cast<uint64_t>(offset) + 27 + segCount + body;
        if (next <= offset || next > size) break;
        offset = static_cast<size_t>(next);
        if (offset == 0) break;
    }
    if (haveGranule) {
        out.durationSeconds =
            static_cast<double>(maxGranule) / static_cast<double>(rate);
    }
    return out;
}

} // namespace LongAudioDetail

/** Container dispatch: WAV ("RIFF....WAVE") or OGG ("OggS") by magic.
 *  Anything else yields `known == false`. Never decodes. */
inline AudioHeaderInfo probeAudioHeaderDuration(const uint8_t* data,
                                                size_t size) {
    if (data != nullptr && size >= 12 && std::memcmp(data, "RIFF", 4) == 0) {
        return LongAudioDetail::probeWavHeader(data, size);
    }
    if (data != nullptr && size >= 4 && std::memcmp(data, "OggS", 4) == 0) {
        return LongAudioDetail::probeOggHeader(data, size);
    }
    return AudioHeaderInfo{};
}

/** Structured assessment: the log warn AND the machine-readable verdict.
 *  Unknown durations and degenerate thresholds stay silent (fail closed);
 *  only a strictly over-threshold duration warns. */
struct LongAudioAssessment {
    double durationSeconds = -1.0;
    double thresholdSeconds = 0.0;
    bool exceedsThreshold = false;
};

inline LongAudioAssessment assessLongAudio(double durationSeconds,
                                           double thresholdSeconds,
                                           const char* assetPathOrNull = nullptr) {
    LongAudioAssessment result{durationSeconds, thresholdSeconds, false};
    if (!(durationSeconds >= 0.0) || !std::isfinite(durationSeconds) ||
        !(thresholdSeconds > 0.0) || !std::isfinite(thresholdSeconds)) {
        return result;
    }
    result.exceedsThreshold = durationSeconds > thresholdSeconds;
    if (result.exceedsThreshold) {
        std::string message =
            "[AudioEngine] Long audio exceeds the 64 MiB contract: ";
        message += (assetPathOrNull != nullptr && *assetPathOrNull != '\0')
            ? assetPathOrNull
            : "<buffer>";
        message += " (duration " + std::to_string(durationSeconds) +
                   "s > threshold " + std::to_string(thresholdSeconds) +
                   "s; header probe, no full decode)";
        ROWL_LOG_WARN(message);
    }
    return result;
}

/** One-step convenience: probe a header buffer, derive its format-native
 *  threshold, and assess. Unknown headers/durations yield a silent
 *  non-exceeding assessment. */
inline LongAudioAssessment probeAndAssessLongAudio(const uint8_t* data,
                                                   size_t size,
                                                   const char* assetPathOrNull = nullptr) {
    const AudioHeaderInfo info = probeAudioHeaderDuration(data, size);
    if (!info.known || !(info.durationSeconds >= 0.0)) {
        return LongAudioAssessment{};
    }
    const double threshold = longAudioThresholdSeconds(
        info.sampleRateHz, info.channelCount, info.bytesPerSample);
    return assessLongAudio(info.durationSeconds, threshold, assetPathOrNull);
}

} // namespace Rowl::Audio
