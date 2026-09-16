/**
 * ogg_stream_source.cpp
 *
 * Faz 5 Dilim 1 — OGG Vorbis artımlı chunk decode (pull-model).
 * Tam decode YOKTUR; thread açmaz. Limitler audio_engine.cpp ile aynıdır.
 */

#include "rowl/audio/ogg_stream_source.hpp"
#include "rowl/vfs/vfs.hpp"

#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>

namespace Rowl::Audio {

namespace {

constexpr uint64_t kMaxEncodedAudioBytes = 64ULL * 1024 * 1024;
constexpr size_t kChunkBytes = 32 * 1024;
constexpr float kS16ToFloat = 1.0f / 32768.0f;

size_t streamRead(void* pointer, size_t size, size_t count, void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream || size == 0 || count == 0) return 0;
    const auto requested = std::min<uint64_t>(
        static_cast<uint64_t>(size) * count, kMaxEncodedAudioBytes);
    stream->read(static_cast<char*>(pointer), static_cast<std::streamsize>(requested));
    return static_cast<size_t>(stream->gcount()) / size;
}

int streamSeek(void* datasource, ogg_int64_t offset, int whence) {
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

int streamClose(void*) { return 0; } // VFS retains ownership of the stream.

long streamTell(void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream) return -1;
    const auto position = stream->tellg();
    return position < 0 || position > LONG_MAX ? -1 : static_cast<long>(position);
}

} // namespace

struct OggStreamSource::Impl {
    std::unique_ptr<std::istream> stream;
    OggVorbis_File decoder{};
    bool decoderOpen = false;
    std::string path;
    std::string error;
    uint32_t sampleRateHz = 0;
    uint32_t channelCount = 0;
};

OggStreamSource::OggStreamSource() : m_impl(std::make_unique<Impl>()) {}
OggStreamSource::~OggStreamSource() { close(); }

bool OggStreamSource::open(Rowl::VFS::VFSManager& vfs, const std::string& path,
                           std::string& error) {
    close();
    m_impl->path = path;
    auto stream = vfs.openReadStream(path);
    if (!stream) {
        m_impl->error = "Ogg/Vorbis stream could not be opened: " + path;
        error = m_impl->error;
        return false;
    }
    m_impl->stream = std::move(stream);
    const ov_callbacks callbacks{streamRead, streamSeek, streamClose, streamTell};
    if (ov_open_callbacks(m_impl->stream.get(), &m_impl->decoder, nullptr, 0,
                          callbacks) < 0) {
        m_impl->error = "Ogg/Vorbis stream could not be opened";
        m_impl->stream.reset();
        error = m_impl->error;
        return false;
    }
    m_impl->decoderOpen = true;
    const vorbis_info* info = ov_info(&m_impl->decoder, -1);
    if (!info || info->channels <= 0 || info->channels > 8 || info->rate <= 0) {
        m_impl->error = "Ogg/Vorbis stream has an unsupported audio format";
        close();
        error = m_impl->error;
        return false;
    }
    m_impl->sampleRateHz = static_cast<uint32_t>(info->rate);
    m_impl->channelCount = static_cast<uint32_t>(info->channels);
    m_impl->error.clear();
    return true;
}

void OggStreamSource::close() {
    if (m_impl->decoderOpen) {
        ov_clear(&m_impl->decoder);
        m_impl->decoderOpen = false;
    }
    m_impl->stream.reset();
    m_impl->sampleRateHz = 0;
    m_impl->channelCount = 0;
}

bool OggStreamSource::isOpen() const {
    return m_impl->decoderOpen && m_impl->stream != nullptr;
}

size_t OggStreamSource::readFloatFrames(float* out, size_t frames, bool& eos) {
    eos = false;
    if (!isOpen() || !out || frames == 0) {
        if (isOpen()) {
            // Boş istek akış sonu değildir; bayrağı değiştirmeyiz.
        } else {
            eos = true;
        }
        return 0;
    }
    const size_t channels = m_impl->channelCount;
    const size_t wantBytes = frames * channels * 2; // bytesPerSample daima 2.
    size_t producedFrames = 0;
    float* cursor = out;
    std::array<char, kChunkBytes> chunk{};
    int bitstream = 0;
    while (producedFrames < frames) {
        const size_t remainingBytes =
            (frames - producedFrames) * channels * 2;
        const int ask = static_cast<int>(
            std::min<size_t>(remainingBytes, chunk.size()));
        // ov_read(...,0,2,1): little-endian (0), 16-bit (2), signed (1).
        // Çıktı baytları daima little-endian S16'dır; big-endian hostta
        // swapsız yorumlanmamalıdır (bu derleme little-endian varsayar).
        const long bytes = ov_read(&m_impl->decoder, chunk.data(), ask, 0, 2, 1,
                                   &bitstream);
        if (bytes == 0) {
            eos = true;
            break;
        }
        if (bytes < 0) {
            m_impl->error = "Ogg/Vorbis stream is corrupt";
            eos = true;
            break;
        }
        // char-alignment=1 buffer: S16 örnekleri hizalı int16_t* ile
        // okunamaz (strict-aliasing + ARMv8 -O2'de tanımsız davranış).
        // Her örnek memcpy ile yerel değişkene alınır (little-endian S16,
        // yukarıdaki ov_read(...,0,2,1) sözleşmesi).
        const size_t samples = static_cast<size_t>(bytes) / 2;
        for (size_t i = 0; i < samples; ++i) {
            int16_t sample = 0;
            std::memcpy(&sample, chunk.data() + i * 2, sizeof(sample));
            *cursor++ = static_cast<float>(sample) * kS16ToFloat;
        }
        producedFrames += samples / channels;
        (void)wantBytes;
    }
    return producedFrames;
}

bool OggStreamSource::seekPcmFrame(uint64_t frame) {
    if (!isOpen()) return false;
    // ov_pcm_seek başarısız olursa konum tanımsız kalır; fail-closed false.
    return ov_pcm_seek(&m_impl->decoder,
                       static_cast<ogg_int64_t>(frame)) == 0;
}

uint32_t OggStreamSource::sampleRateHz() const { return m_impl->sampleRateHz; }
uint32_t OggStreamSource::channelCount() const { return m_impl->channelCount; }

int64_t OggStreamSource::pcmTotal() const {
    if (!isOpen()) return -1;
    const ogg_int64_t total = ov_pcm_total(&m_impl->decoder, -1);
    if (total < 0 || static_cast<uint64_t>(total) > static_cast<uint64_t>(INT64_MAX)) {
        return -1;
    }
    return static_cast<int64_t>(total);
}

double OggStreamSource::durationSeconds() const {
    if (!isOpen() || m_impl->sampleRateHz == 0) return -1.0;
    const int64_t total = pcmTotal();
    if (total < 0) return -1.0;
    return static_cast<double>(total) /
           static_cast<double>(m_impl->sampleRateHz);
}

const std::string& OggStreamSource::path() const { return m_impl->path; }
const std::string& OggStreamSource::lastError() const { return m_impl->error; }

} // namespace Rowl::Audio
