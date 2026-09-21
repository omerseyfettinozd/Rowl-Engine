/**
 * rowl/audio/ogg_stream_source.hpp
 *
 * Faz 5 Dilim 1 — OGG Vorbis streaming çekirdeği (pull-model).
 *
 * VFS openReadStream ile açılmış seekable bir istream üzerinde
 * ov_open_callbacks / ov_read ile artımlı chunk decode yapar; tam decode
 * YOKTUR, thread açmaz. Sürücü (pump) AudioEngine::update() içinden
 * senkron çağrılır.
 *
 * Limitler audio_engine.cpp ile aynıdır (kMaxEncodedAudioBytes clamp),
 * bytesPerSample daima 2'dir (motor Vorbis'i S16'ya çözer).
 * Corrupt / truncated girdiler fail-closed kapanır (false + hata stringi).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <string>

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Audio {

class OggStreamSource {
public:
    OggStreamSource();
    ~OggStreamSource();

    OggStreamSource(const OggStreamSource&) = delete;
    OggStreamSource& operator=(const OggStreamSource&) = delete;

    /// VFS üzerinden path'i açar. Başarıda true; corrupt/truncated/kayıp
    /// girdide false + error dolar (fail-closed, exception yok).
    bool open(Rowl::VFS::VFSManager& vfs, const std::string& path, std::string& error);
    void close();
    bool isOpen() const;

    /// En fazla `frames` çok-kanallı frame'i interleaved float PCM olarak
    /// yazar. Dönüş: yazılan frame sayısı. `eos`, akış sonuna ulaşıldığında
    /// true olur. Büyük tahsis YOKTUR (sabit chunk-cap).
    size_t readFloatFrames(float* out, size_t frames, bool& eos);

    /// PCM frame konumuna sarar (yalnızca internal loop-wrap ve
    /// device-recovery restore için; C API'si YOKTUR).
    bool seekPcmFrame(uint64_t frame);

    uint32_t sampleRateHz() const;
    uint32_t channelCount() const;
    /// Toplam PCM frame (ov_pcm_total); bilinmiyorsa -1.
    int64_t pcmTotal() const;
    /// Saniye cinsinden süre; bilinmiyorsa negatif.
    double durationSeconds() const;

    const std::string& path() const;
    const std::string& lastError() const;
    // M4 (L3 düzeltmesi): son open-fail'inin sınıfı — true ise open-sınıfı
    // (VFS/ov_open → Device/IoError=7), false ise decode-sınıfı (format/
    // corrupt → Decode/AudioDecodeError=10). Başarıda false.
    bool lastOpenFailed() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Rowl::Audio
