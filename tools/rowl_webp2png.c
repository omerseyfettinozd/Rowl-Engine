/**
 * rowl_webp2png — deterministic WebP to PNG converter (host tool).
 *
 * Contract (docs/MEDIA_CONVERTERS_CONTRACT.md):
 *  - Decodes with libwebpdecoder (WebPDecodeRGBA); encodes with libpng.
 *  - PNG writer is FIXED: 8-bit RGBA, non-interlaced, compression level 6,
 *    and NEVER writes tIME / tEXt / zTXt / iTXt / pHYs (dpi) / iCCP chunks,
 *    so identical pixels always produce byte-identical files.
 *  - With --sidecar, writes <output>.rowlconv.json (source/output digests).
 *
 * Host-build tool only: never ships in the game package, never links the
 * runtime, never touches Rowl::Audio.
 */

#include <png.h>
#include <webp/decode.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rowl_sha256.h"

#define ROWL_WEBP2PNG_VERSION "1.0.0"
#define ROWL_WEBP2PNG_PNG_WRITER "libpng"
#define ROWL_WEBP2PNG_MAX_WEBP_BYTES (256u * 1024u * 1024u)

static void usage(const char* argv0) {
    fprintf(stderr,
            "Usage: %s -o OUTPUT.png [--sidecar FILE] INPUT.webp\n"
            "  Decodes INPUT.webp and writes a deterministic PNG (fixed libpng\n"
            "  settings, no timestamps, no text chunks, no DPI).\n"
            "  --version prints the converter version.\n",
            argv0);
}

static uint8_t* read_file(const char* path, size_t* outLen) {
    FILE* in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "rowl_webp2png: cannot open '%s'\n", path);
        return NULL;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fprintf(stderr, "rowl_webp2png: cannot seek '%s'\n", path);
        fclose(in);
        return NULL;
    }
    const long size = ftell(in);
    if (size < 0 || (uint64_t)size > ROWL_WEBP2PNG_MAX_WEBP_BYTES) {
        fprintf(stderr, "rowl_webp2png: rejected size of '%s'\n", path);
        fclose(in);
        return NULL;
    }
    rewind(in);
    uint8_t* data = (uint8_t*)malloc((size_t)size > 0 ? (size_t)size : 1u);
    if (!data) {
        fprintf(stderr, "rowl_webp2png: out of memory\n");
        fclose(in);
        return NULL;
    }
    if (size > 0 && fread(data, 1, (size_t)size, in) != (size_t)size) {
        fprintf(stderr, "rowl_webp2png: failed reading '%s'\n", path);
        free(data);
        fclose(in);
        return NULL;
    }
    fclose(in);
    *outLen = (size_t)size;
    return data;
}

typedef struct {
    FILE* file;
    RowlSha256* hash;
} RowlPngIo;

static void png_write_data(png_structp png, png_bytep data, size_t len) {
    RowlPngIo* ctx = (RowlPngIo*)png_get_io_ptr(png);
    fwrite(data, 1, len, ctx->file);
    rowl_sha256_update(ctx->hash, data, len);
}

static void png_flush_data(png_structp png) {
    RowlPngIo* ctx = (RowlPngIo*)png_get_io_ptr(png);
    fflush(ctx->file);
}

int main(int argc, char** argv) {
    const char* outputPath = NULL;
    const char* sidecarPath = NULL;
    const char* inputPath = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--sidecar") == 0 && i + 1 < argc) {
            sidecarPath = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("%s\n", ROWL_WEBP2PNG_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "rowl_webp2png: unknown flag '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else if (!inputPath) {
            inputPath = argv[i];
        } else {
            fprintf(stderr, "rowl_webp2png: unexpected argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 2;
        }
    }
    if (!outputPath || !inputPath) {
        fprintf(stderr, "rowl_webp2png: missing INPUT.webp and/or -o OUTPUT.png\n");
        usage(argv[0]);
        return 2;
    }

    size_t webpLen = 0;
    uint8_t* webp = read_file(inputPath, &webpLen);
    if (!webp) return 1;
    if (webpLen == 0) {
        fprintf(stderr, "rowl_webp2png: empty input '%s'\n", inputPath);
        free(webp);
        return 1;
    }

    uint8_t sourceDigest[32];
    {
        RowlSha256 hash;
        rowl_sha256_init(&hash);
        rowl_sha256_update(&hash, webp, webpLen);
        rowl_sha256_final(&hash, sourceDigest);
    }
    char sourceHex[65];
    rowl_sha256_hex(sourceDigest, sourceHex);

    int width = 0, height = 0;
    if (!WebPGetInfo(webp, webpLen, &width, &height) || width <= 0 || height <= 0) {
        fprintf(stderr, "rowl_webp2png: not a valid WebP image '%s'\n", inputPath);
        free(webp);
        return 1;
    }
    uint8_t* rgba = WebPDecodeRGBA(webp, webpLen, &width, &height);
    free(webp);
    if (!rgba) {
        fprintf(stderr, "rowl_webp2png: WebP decode failed '%s'\n", inputPath);
        return 1;
    }

    FILE* out = fopen(outputPath, "wb");
    if (!out) {
        fprintf(stderr, "rowl_webp2png: cannot open '%s' for writing\n", outputPath);
        WebPFree(rgba);
        return 1;
    }

    int status = 0;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = NULL;
    if (!png) {
        fprintf(stderr, "rowl_webp2png: png_create_write_struct failed\n");
        fclose(out);
        WebPFree(rgba);
        return 1;
    }
    info = png_create_info_struct(png);
    if (!info) {
        fprintf(stderr, "rowl_webp2png: png_create_info_struct failed\n");
        png_destroy_write_struct(&png, NULL);
        fclose(out);
        WebPFree(rgba);
        return 1;
    }
    if (setjmp(png_jmpbuf(png))) {
        fprintf(stderr, "rowl_webp2png: libpng error writing '%s'\n", outputPath);
        png_destroy_write_struct(&png, &info);
        fclose(out);
        WebPFree(rgba);
        return 1;
    }
    {
        RowlPngIo io;
        RowlSha256 outHash;
        rowl_sha256_init(&outHash);
        io.file = out;
        io.hash = &outHash;
        png_set_write_fn(png, &io, png_write_data, png_flush_data);
        /* Fixed writer contract: no tIME/tEXt/pHYs/iCCP ever; level 6 pinned. */
        png_set_IHDR(png, info, (uint32_t)width, (uint32_t)height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        png_set_compression_level(png, 6);
        png_set_compression_mem_level(png, 8);
        png_set_compression_strategy(png, PNG_Z_DEFAULT_STRATEGY);
        png_write_info(png, info);
        for (int y = 0; y < height; ++y) {
            png_write_row(png, rgba + (size_t)y * (size_t)width * 4u);
        }
        png_write_end(png, info);

        if (fflush(out) != 0 || fclose(out) != 0) {
            fprintf(stderr, "rowl_webp2png: failed closing '%s'\n", outputPath);
            status = 1;
        }
        out = NULL;
        if (status == 0 && sidecarPath) {
            uint8_t outputDigest[32];
            rowl_sha256_final(&outHash, outputDigest);
            char outputHex[65];
            rowl_sha256_hex(outputDigest, outputHex);
            FILE* sidecar = fopen(sidecarPath, "w");
            if (!sidecar) {
                fprintf(stderr, "rowl_webp2png: cannot open sidecar '%s'\n", sidecarPath);
                status = 1;
            } else {
                fprintf(sidecar,
                        "{\n"
                        "  \"source_sha256\": \"%s\",\n"
                        "  \"converter_name\": \"rowl_webp2png\",\n"
                        "  \"converter_version\": \"%s\",\n"
                        "  \"settings\": {\n"
                        "    \"png_writer\": \"%s\",\n"
                        "    \"dpi\": null\n"
                        "  },\n"
                        "  \"output_sha256\": \"%s\",\n"
                        "  \"created_by\": \"rowl_webp2png %s\"\n"
                        "}\n",
                        sourceHex, ROWL_WEBP2PNG_VERSION, ROWL_WEBP2PNG_PNG_WRITER, outputHex,
                        ROWL_WEBP2PNG_VERSION);
                if (fclose(sidecar) != 0) {
                    fprintf(stderr, "rowl_webp2png: failed closing sidecar '%s'\n", sidecarPath);
                    status = 1;
                }
            }
        }
    }
    png_destroy_write_struct(&png, &info);
    if (out) fclose(out);
    WebPFree(rgba);
    return status;
}
