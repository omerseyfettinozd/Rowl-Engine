/**
 * rowl_oggenc — deterministic raw-PCM to OGG Vorbis converter (host tool).
 *
 * Contract (docs/MEDIA_CONVERTERS_CONTRACT.md):
 *  - Input: raw s16le PCM (file or stdin), --rate/--channels describe it.
 *  - Quality is FIXED at -q 4 (base 0.4); v1 exposes no quality override.
 *  - Vendor string is FIXED ("RowlEngine rowl_oggenc <semver>").
 *  - Ogg serial is FIXED: big-endian uint32 of the input's SHA-256[0..3].
 *  - Same input bytes always produce byte-identical output.
 *  - With --sidecar, writes <output>.rowlconv.json (source/output digests).
 *
 * Host-build tool only: never ships in the game package, never links the
 * runtime, never touches Rowl::Audio. No ffmpeg CLI encode path exists here
 * (ffmpeg encode output is not byte-deterministic — decode-only usage).
 */

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rowl_sha256.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#define ROWL_OGGENC_VERSION "1.0.0"
#define ROWL_OGGENC_VENDOR "RowlEngine rowl_oggenc " ROWL_OGGENC_VERSION
#define ROWL_OGGENC_QUALITY_BASE 0.4f /* -q 4 */
#define ROWL_OGGENC_QUALITY_Q 4
#define ROWL_OGGENC_MAX_PCM_BYTES (512u * 1024u * 1024u)

static void usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s [--rate HZ] [--channels N] -o OUTPUT.ogg [--sidecar FILE] [INPUT.pcm|-]\n"
            "  Reads raw s16le PCM from INPUT.pcm (or stdin with '-') and writes a\n"
            "  deterministic OGG Vorbis file (fixed -q 4, fixed vendor, input-derived serial).\n"
            "  Defaults: --rate 44100 --channels 2. --version prints the vendor string.\n",
            argv0);
}

static int write_page(FILE* out, const ogg_page* page, RowlSha256* hash) {
    if (fwrite(page->header, 1, (size_t)page->header_len, out) != (size_t)page->header_len) return -1;
    if (fwrite(page->body, 1, (size_t)page->body_len, out) != (size_t)page->body_len) return -1;
    rowl_sha256_update(hash, page->header, (size_t)page->header_len);
    rowl_sha256_update(hash, page->body, (size_t)page->body_len);
    return 0;
}

static int16_t read_s16le(const uint8_t* p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint8_t* read_all(FILE* in, size_t* outLen, const char* what) {
    size_t cap = 1u << 20, len = 0;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        fprintf(stderr, "rowl_oggenc: out of memory\n");
        return NULL;
    }
    for (;;) {
        if (len == cap) {
            if (cap >= ROWL_OGGENC_MAX_PCM_BYTES) {
                fprintf(stderr, "rowl_oggenc: %s exceeds %u bytes\n", what, ROWL_OGGENC_MAX_PCM_BYTES);
                free(data);
                return NULL;
            }
            cap *= 2;
            uint8_t* grown = (uint8_t*)realloc(data, cap);
            if (!grown) {
                fprintf(stderr, "rowl_oggenc: out of memory\n");
                free(data);
                return NULL;
            }
            data = grown;
        }
        const size_t n = fread(data + len, 1, cap - len, in);
        len += n;
        if (n == 0) {
            if (ferror(in)) {
                fprintf(stderr, "rowl_oggenc: failed reading %s\n", what);
                free(data);
                return NULL;
            }
            break;
        }
    }
    *outLen = len;
    return data;
}

int main(int argc, char** argv) {
    long rate = 44100;
    long channels = 2;
    const char* outputPath = NULL;
    const char* sidecarPath = NULL;
    const char* inputPath = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rate") == 0 && i + 1 < argc) {
            rate = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--channels") == 0 && i + 1 < argc) {
            channels = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--sidecar") == 0 && i + 1 < argc) {
            sidecarPath = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("%s\n", ROWL_OGGENC_VENDOR);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-") == 0 && !inputPath) {
            inputPath = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rowl_oggenc: unknown flag '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else if (!inputPath) {
            inputPath = argv[i];
        } else {
            fprintf(stderr, "rowl_oggenc: unexpected argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    if (!outputPath) {
        fprintf(stderr, "rowl_oggenc: missing -o OUTPUT.ogg\n");
        usage(argv[0]);
        return 2;
    }
    if (rate <= 0 || rate > 192000 || channels <= 0 || channels > 8) {
        fprintf(stderr, "rowl_oggenc: invalid rate/channels (%ld/%ld)\n", rate, channels);
        return 2;
    }

    FILE* in = stdin;
    int useStdin = !inputPath || strcmp(inputPath, "-") == 0;
    if (!useStdin) {
        in = fopen(inputPath, "rb");
        if (!in) {
            fprintf(stderr, "rowl_oggenc: cannot open '%s'\n", inputPath);
            return 1;
        }
    }
#if defined(_WIN32)
    /* stdin is text-mode by default on Windows; PCM bytes must pass through raw. */
    if (useStdin) _setmode(_fileno(stdin), _O_BINARY);
#endif
    size_t pcmLen = 0;
    uint8_t* pcm = read_all(in, &pcmLen, useStdin ? "stdin" : inputPath);
    if (!useStdin) fclose(in);
    if (!pcm) return 1;
    if (pcmLen == 0 || (pcmLen % ((size_t)channels * 2u)) != 0) {
        fprintf(stderr, "rowl_oggenc: PCM length %zu is not a whole frame (%ld ch)\n", pcmLen, channels);
        free(pcm);
        return 1;
    }

    /* Fixed serial: big-endian uint32 of SHA-256(input)[0..3]. */
    uint8_t sourceDigest[32];
    {
        RowlSha256 hash;
        rowl_sha256_init(&hash);
        rowl_sha256_update(&hash, pcm, pcmLen);
        rowl_sha256_final(&hash, sourceDigest);
    }
    char sourceHex[65];
    rowl_sha256_hex(sourceDigest, sourceHex);
    const int serial = (int)(((uint32_t)sourceDigest[0] << 24) | ((uint32_t)sourceDigest[1] << 16) |
                             ((uint32_t)sourceDigest[2] << 8) | (uint32_t)sourceDigest[3]);

    vorbis_info vi;
    vorbis_info_init(&vi);
    if (vorbis_encode_init_vbr(&vi, (int)channels, rate, ROWL_OGGENC_QUALITY_BASE) != 0) {
        fprintf(stderr, "rowl_oggenc: vorbis_encode_init_vbr failed\n");
        free(pcm);
        return 1;
    }
    vorbis_comment vc;
    vorbis_comment_init(&vc);
    free(vc.vendor);
    vc.vendor = strdup(ROWL_OGGENC_VENDOR);
    if (!vc.vendor) {
        fprintf(stderr, "rowl_oggenc: out of memory\n");
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        free(pcm);
        return 1;
    }

    vorbis_dsp_state vd;
    vorbis_block vb;
    if (vorbis_analysis_init(&vd, &vi) != 0 || vorbis_block_init(&vd, &vb) != 0) {
        fprintf(stderr, "rowl_oggenc: analysis init failed\n");
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        free(pcm);
        return 1;
    }

    ogg_stream_state os;
    if (ogg_stream_init(&os, serial) != 0) {
        fprintf(stderr, "rowl_oggenc: ogg_stream_init failed\n");
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        free(pcm);
        return 1;
    }

    FILE* out = fopen(outputPath, "wb");
    if (!out) {
        fprintf(stderr, "rowl_oggenc: cannot open '%s' for writing\n", outputPath);
        ogg_stream_clear(&os);
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        free(pcm);
        return 1;
    }

    RowlSha256 outHash;
    rowl_sha256_init(&outHash);
    int status = 0;
    ogg_packet header, headerComm, headerCode;

    vorbis_analysis_headerout(&vd, &vc, &header, &headerComm, &headerCode);
    ogg_stream_packetin(&os, &header);
    ogg_stream_packetin(&os, &headerComm);
    ogg_stream_packetin(&os, &headerCode);
    {
        ogg_page page;
        while (ogg_stream_flush(&os, &page) != 0) {
            if (write_page(out, &page, &outHash) != 0) {
                fprintf(stderr, "rowl_oggenc: failed writing '%s'\n", outputPath);
                status = 1;
                break;
            }
        }
    }

    /* Fixed-size analysis blocks; identical input always yields identical packets. */
    const size_t frameBytes = (size_t)channels * 2u;
    const size_t totalFrames = pcmLen / frameBytes;
    size_t frame = 0;
    const long kBlockFrames = 4096;
    while (status == 0 && frame < totalFrames) {
        const long want = (long)((totalFrames - frame) < (size_t)kBlockFrames ? (totalFrames - frame)
                                                                              : (size_t)kBlockFrames);
        float** buffer = vorbis_analysis_buffer(&vd, want);
        for (long i = 0; i < want; ++i) {
            for (long ch = 0; ch < channels; ++ch) {
                const size_t sampleIndex = (frame + (size_t)i) * (size_t)channels + (size_t)ch;
                buffer[ch][i] = (float)read_s16le(pcm + sampleIndex * 2u) / 32768.0f;
            }
        }
        vorbis_analysis_wrote(&vd, want);
        frame += (size_t)want;
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&vd, &packet) != 0) {
                ogg_stream_packetin(&os, &packet);
                ogg_page page;
                while (ogg_stream_pageout(&os, &page) != 0) {
                    if (write_page(out, &page, &outHash) != 0) {
                        fprintf(stderr, "rowl_oggenc: failed writing '%s'\n", outputPath);
                        status = 1;
                        break;
                    }
                }
                if (status != 0) break;
            }
            if (status != 0) break;
        }
    }

    if (status == 0) {
        vorbis_analysis_wrote(&vd, 0);
        while (vorbis_analysis_blockout(&vd, &vb) == 1) {
            vorbis_analysis(&vb, NULL);
            vorbis_bitrate_addblock(&vb);
            ogg_packet packet;
            while (vorbis_bitrate_flushpacket(&vd, &packet) != 0) {
                ogg_stream_packetin(&os, &packet);
                ogg_page page;
                while (ogg_stream_pageout(&os, &page) != 0) {
                    if (write_page(out, &page, &outHash) != 0) {
                        fprintf(stderr, "rowl_oggenc: failed writing '%s'\n", outputPath);
                        status = 1;
                        break;
                    }
                }
                if (status != 0) break;
            }
            if (status != 0) break;
        }
        ogg_page page;
        while (status == 0 && ogg_stream_flush(&os, &page) != 0) {
            if (write_page(out, &page, &outHash) != 0) {
                fprintf(stderr, "rowl_oggenc: failed writing '%s'\n", outputPath);
                status = 1;
                break;
            }
        }
    }

    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);
    free(pcm);
    if (fclose(out) != 0) {
        fprintf(stderr, "rowl_oggenc: failed closing '%s'\n", outputPath);
        status = 1;
    }

    if (status == 0 && sidecarPath) {
        uint8_t outputDigest[32];
        rowl_sha256_final(&outHash, outputDigest);
        char outputHex[65];
        rowl_sha256_hex(outputDigest, outputHex);
        FILE* sidecar = fopen(sidecarPath, "w");
        if (!sidecar) {
            fprintf(stderr, "rowl_oggenc: cannot open sidecar '%s'\n", sidecarPath);
            return 1;
        }
        fprintf(sidecar,
                "{\n"
                "  \"source_sha256\": \"%s\",\n"
                "  \"converter_name\": \"rowl_oggenc\",\n"
                "  \"converter_version\": \"%s\",\n"
                "  \"settings\": {\n"
                "    \"quality_q\": %d,\n"
                "    \"sample_rate_hz\": %ld,\n"
                "    \"channels\": %ld,\n"
                "    \"serial\": %d\n"
                "  },\n"
                "  \"output_sha256\": \"%s\",\n"
                "  \"created_by\": \"rowl_oggenc %s\"\n"
                "}\n",
                sourceHex, ROWL_OGGENC_VERSION, ROWL_OGGENC_QUALITY_Q, rate, channels, serial,
                outputHex, ROWL_OGGENC_VERSION);
        if (fclose(sidecar) != 0) {
            fprintf(stderr, "rowl_oggenc: failed closing sidecar '%s'\n", sidecarPath);
            return 1;
        }
    }
    return status;
}
