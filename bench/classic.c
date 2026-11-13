/* classic.c -- shz against the classic codecs, all in one process (their own libraries), on the same bytes.
   For every file and codec: compressed size, best-of-N compression time and decompression time (each measured until
   at least 0.25 s of repeats or 3 repeats), and a byte-exact round trip.
     clang -std=c11 -O2 -fwrapv -DSHZ_NO_MAIN -I/opt/homebrew/include bench/classic.c codec/shz.c -L/opt/homebrew/lib
           -lzstd -llzma -lbrotlienc -lbrotlidec -llz4 -lz -lbz2 -o bench/classic
     bench/classic SPLIT CATEGORY FILE...   (one CSV row per file and codec on stdout) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <zstd.h>
#include <lzma.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <lz4.h>
#include <lz4hc.h>
#include <zlib.h>
#include <bzlib.h>

size_t shz_bound(size_t n);
size_t shz_compress(const uint8_t *in, size_t n, uint8_t *out);
size_t shz_decompress(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

typedef struct { const char *name; int level; } Codec;
static const Codec CODECS[] = {
    {"shz", 0}, {"zstd-3", 3}, {"zstd-19", 19}, {"zstd-22", 22}, {"xz-6", 6}, {"xz-9e", 9}, {"brotli-11", 11},
    {"gzip-9", 9}, {"bzip2-9", 9}, {"lz4hc-12", 12},
};
enum { NC = sizeof CODECS / sizeof CODECS[0] };

static size_t comp(int k, const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    const char *nm = CODECS[k].name; int lv = CODECS[k].level;
    if (!strcmp(nm, "shz")) return shz_compress(in, n, out);
    if (!strncmp(nm, "zstd", 4)) { size_t z = ZSTD_compress(out, cap, in, n, lv); return ZSTD_isError(z) ? 0 : z; }
    if (!strncmp(nm, "xz", 2)) {
        size_t pos = 0; uint32_t preset = (uint32_t)lv | (strstr(nm, "e") ? LZMA_PRESET_EXTREME : 0);
        return lzma_easy_buffer_encode(preset, LZMA_CHECK_CRC64, NULL, in, n, out, &pos, cap) == LZMA_OK ? pos : 0;
    }
    if (!strncmp(nm, "brotli", 6)) { size_t z = cap; return BrotliEncoderCompress(lv, 24, BROTLI_MODE_GENERIC, n, in, &z, out) ? z : 0; }
    if (!strncmp(nm, "gzip", 4)) { uLongf z = cap; return compress2(out, &z, in, n, lv) == Z_OK ? z : 0; }
    if (!strncmp(nm, "bzip2", 5)) { unsigned z = (unsigned)cap; return BZ2_bzBuffToBuffCompress((char *)out, &z, (char *)in, (unsigned)n, lv, 0, 0) == BZ_OK ? z : 0; }
    if (!strncmp(nm, "lz4hc", 5)) return (size_t)LZ4_compress_HC((const char *)in, (char *)out, (int)n, (int)cap, lv);
    return 0;
}
static size_t decomp(int k, const uint8_t *in, size_t z, uint8_t *out, size_t n) {
    const char *nm = CODECS[k].name;
    if (!strcmp(nm, "shz")) return shz_decompress(in, z, out, n);
    if (!strncmp(nm, "zstd", 4)) { size_t r = ZSTD_decompress(out, n, in, z); return ZSTD_isError(r) ? (size_t)-1 : r; }
    if (!strncmp(nm, "xz", 2)) {
        size_t ip = 0, op = 0; uint64_t lim = UINT64_MAX;
        return lzma_stream_buffer_decode(&lim, 0, NULL, in, &ip, z, out, &op, n) == LZMA_OK ? op : (size_t)-1;
    }
    if (!strncmp(nm, "brotli", 6)) { size_t r = n; return BrotliDecoderDecompress(z, in, &r, out) == BROTLI_DECODER_RESULT_SUCCESS ? r : (size_t)-1; }
    if (!strncmp(nm, "gzip", 4)) { uLongf r = n; return uncompress(out, &r, in, z) == Z_OK ? r : (size_t)-1; }
    if (!strncmp(nm, "bzip2", 5)) { unsigned r = (unsigned)n; return BZ2_bzBuffToBuffDecompress((char *)out, &r, (char *)in, (unsigned)z, 0, 0) == BZ_OK ? r : (size_t)-1; }
    if (!strncmp(nm, "lz4hc", 5)) { int r = LZ4_decompress_safe((const char *)in, (char *)out, (int)z, (int)n); return r < 0 ? (size_t)-1 : (size_t)r; }
    return (size_t)-1;
}

int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc < 4) { fprintf(stderr, "usage: classic SPLIT CATEGORY FILE...\n"); return 2; }
    for (int a = 3; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { perror(argv[a]); return 1; }
        fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *in = malloc(n + 1); if (fread(in, 1, n, f) != n) { perror(argv[a]); return 1; } fclose(f);
        size_t cap = n + n / 2 + (1 << 16); uint8_t *z = malloc(cap), *back = malloc(n + 1);
        const char *nm = strrchr(argv[a], '/'); nm = nm ? nm + 1 : argv[a];
        for (int k = 0; k < NC; k++) {
            size_t zl = 0; double bc = 1e30, bd = 1e30, t0, spent = 0; int reps = 0;
            while (reps < 3 || (spent < 0.25 && reps < 50)) {
                t0 = now(); zl = comp(k, in, n, z, cap); double dt = now() - t0;
                if (dt < bc) bc = dt; spent += dt; reps++;
            }
            int ok = zl > 0; spent = 0; reps = 0;
            while (ok && (reps < 3 || (spent < 0.25 && reps < 50))) {
                memset(back, 0, n > 64 ? 64 : n);
                t0 = now(); size_t bl = decomp(k, z, zl, back, n); double dt = now() - t0;
                if (bl != n || memcmp(in, back, n)) ok = 0;
                if (dt < bd) bd = dt; spent += dt; reps++;
            }
            printf("%s,%s,%s,%zu,%s,%zu,%.6f,%.6f,%d\n", argv[1], argv[2], nm, n, CODECS[k].name, zl, bc, ok ? bd : -1.0, ok);
            fflush(stdout);
        }
        free(in); free(z); free(back);
    }
    return 0;
}
