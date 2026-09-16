/**
 * rowl/audio/audio_streaming.hpp
 *
 * Faz 5 Dilim 1 — StreamMode / StreamInfo / toJson gözlemlenebilirliği.
 *
 * long_audio_contract.hpp'yi SARAR; formül kopyası YOKTUR (eşik ve karar
 * daima long_audio_contract üzerinden hesaplanır). JSON elle string
 * kurulur (bağımlılık yok); non-finite null yazılır, -1 korunur.
 */

#pragma once

#include "rowl/audio/long_audio_contract.hpp"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace Rowl::Audio {

/// 64 MiB decoded-PCM bütçesi (long_audio_contract ile aynı zemin).
inline constexpr uint64_t kStreamThresholdBytes = 64ULL * 1024 * 1024;

/// Ring buffer geometrisi: 4 x 4096 frame, sabit üst bant.
inline constexpr size_t kStreamRingChunks = 4;
inline constexpr size_t kStreamRingChunkFrames = 4096;
inline constexpr size_t kStreamRingCapacityFrames =
    kStreamRingChunks * kStreamRingChunkFrames;

enum class StreamMode {
    Memory,
    Stream,
    Unknown,
};

inline const char* streamModeName(StreamMode mode) {
    switch (mode) {
        case StreamMode::Memory: return "memory";
        case StreamMode::Stream: return "stream";
        case StreamMode::Unknown: return "unknown";
    }
    return "unknown";
}

inline StreamMode streamModeFromName(const std::string& name) {
    if (name == "memory") return StreamMode::Memory;
    if (name == "stream") return StreamMode::Stream;
    return StreamMode::Unknown;
}

struct StreamInfo {
    StreamMode mode = StreamMode::Unknown;
    double durationSeconds = -1.0;
    double thresholdSeconds = 0.0;
    double bufferedSeconds = 0.0;
    /// under_threshold | over_threshold | no_bgm | unknown_header
    std::string reason = "no_bgm";
    /// PlayAudio'ya verilen int (0=Bgm,1=Voice,2=Sfx,3=Ambience,4=Ui).
    int channel = 0;
    std::string asset;
};

namespace StreamingDetail {

inline void appendJsonNumber(std::string& out, double value) {
    // Geçerli JSON: non-finite (NaN/±Infinity) çıplak literal YAZILMAZ;
    // wire-format null yazılır (C# taraf explicit null -> NaN eşler,
    // IsStream fail-closed kalır).
    if (value != value) { // NaN
        out += "null";
        return;
    }
    if (value == std::numeric_limits<double>::infinity()) {
        out += "null";
        return;
    }
    if (value == -std::numeric_limits<double>::infinity()) {
        out += "null";
        return;
    }
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10)
           << value;
    out += stream.str();
}

inline void appendJsonString(std::string& out, const std::string& value) {
    out += '"';
    static constexpr char kHex[] = "0123456789abcdef";
    for (unsigned char uc : value) {
        switch (uc) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    out += "\\u00";
                    out += kHex[(uc >> 4) & 0xF];
                    out += kHex[uc & 0xF];
                } else {
                    out += static_cast<char>(uc);
                }
                break;
        }
    }
    out += '"';
}

} // namespace StreamingDetail

/// StreamInfo -> JSON (tek karar kaynağından üretilir; anahtar kümesi
/// sabittir: mode, duration_seconds, threshold_seconds, threshold_bytes,
/// buffered_seconds, reason, channel, asset).
inline std::string streamInfoToJson(const StreamInfo& info) {
    std::string out = "{\"mode\":";
    StreamingDetail::appendJsonString(out, streamModeName(info.mode));
    out += ",\"duration_seconds\":";
    StreamingDetail::appendJsonNumber(out, info.durationSeconds);
    out += ",\"threshold_seconds\":";
    StreamingDetail::appendJsonNumber(out, info.thresholdSeconds);
    out += ",\"threshold_bytes\":";
    out += std::to_string(kStreamThresholdBytes);
    out += ",\"buffered_seconds\":";
    StreamingDetail::appendJsonNumber(out, info.bufferedSeconds);
    out += ",\"reason\":";
    StreamingDetail::appendJsonString(out, info.reason);
    out += ",\"channel\":";
    out += std::to_string(info.channel);
    out += ",\"asset\":";
    StreamingDetail::appendJsonString(out, info.asset);
    out += "}";
    return out;
}

/// Header baytlarından StreamInfo iskeleti kurar (buffered hariç).
/// Karar operatörü assessLongAudio'dur; burada formül YOKTUR.
/// Bilinmeyen header -> mode=unknown + reason=unknown_header (fail-closed).
inline StreamInfo makeStreamInfoFromHeader(const uint8_t* data, size_t size,
                                           int channel,
                                           const std::string& asset) {
    StreamInfo info;
    info.channel = channel;
    info.asset = asset;
    const AudioHeaderInfo header = probeAudioHeaderDuration(data, size);
    if (!header.known || !(header.durationSeconds >= 0.0)) {
        info.mode = StreamMode::Unknown;
        info.reason = "unknown_header";
        info.durationSeconds = -1.0;
        info.thresholdSeconds = 0.0;
        return info;
    }
    // Eşik, format-native girdilerle sözleşmeden hesaplanır (kopya yok).
    info.thresholdSeconds = longAudioThresholdSeconds(
        header.sampleRateHz, header.channelCount, header.bytesPerSample);
    info.durationSeconds = header.durationSeconds;
    const LongAudioAssessment assessment =
        assessLongAudio(info.durationSeconds, info.thresholdSeconds,
                        asset.empty() ? nullptr : asset.c_str());
    if (assessment.exceedsThreshold) {
        info.mode = StreamMode::Stream;
        info.reason = "over_threshold";
    } else {
        info.mode = StreamMode::Memory;
        info.reason = "under_threshold";
    }
    return info;
}

} // namespace Rowl::Audio
