/* bsc_bench.c -- bsc 3.4.0 (libbsc, single-threaded, no OpenMP) in-process, the same rows as bench/classic.c.
   Two settings, each file as ONE block (bsc's strongest choice): bsc-e1 = the default coder (static QLFC),
   bsc-e2 = adaptive QLFC (bsc's best compression); both with bsc's default LZP preprocessing (hash 15, min length 72).
     clang -O2 bench/bsc_bench.c -Ithird_party/libbsc third_party/libbsc/build/libbsc.a -lc++ -o bench/bsc_bench
     bench/bsc_bench SPLIT CATEGORY FILE... */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "libbsc/libbsc.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (bsc_init(LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR) { fprintf(stderr, "bsc_init failed\n"); return 1; }
    const char *nm[2] = {"bsc-e1", "bsc-e2"}; int coder[2] = {LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE};
    for (int a = 3; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { perror(argv[a]); return 1; }
        fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *in = malloc(n + 1), *z = malloc(n + LIBBSC_HEADER_SIZE + 1024), *back = malloc(n + 1);
        if (fread(in, 1, n, f) != n) { perror(argv[a]); return 1; } fclose(f);
        const char *fn = strrchr(argv[a], '/'); fn = fn ? fn + 1 : argv[a];
        for (int k = 0; k < 2; k++) {
            int zl = 0; double bc = 1e30, bd = 1e30, spent = 0; int reps = 0, ok = 1;
            while (reps < 3 || (spent < 0.25 && reps < 50)) {
                double t0 = now(); zl = bsc_compress(in, z, (int)n, LIBBSC_DEFAULT_LZPHASHSIZE, LIBBSC_DEFAULT_LZPMINLEN,
                                                     LIBBSC_BLOCKSORTER_BWT, coder[k], LIBBSC_FEATURE_FASTMODE);
                double dt = now() - t0; if (dt < bc) bc = dt; spent += dt; reps++;
                if (zl < LIBBSC_NO_ERROR) { ok = 0; break; }
                if (n > 200000000) break;
            }
            spent = 0; reps = 0;
            while (ok && (reps < 3 || (spent < 0.25 && reps < 50))) {
                int bs = 0, ds = 0;
                double t0 = now();
                if (bsc_block_info(z, LIBBSC_HEADER_SIZE, &bs, &ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR || ds != (int)n ||
                    bsc_decompress(z, bs, back, ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR) { ok = 0; break; }
                double dt = now() - t0; if (dt < bd) bd = dt; spent += dt; reps++;
                if (memcmp(in, back, n)) { ok = 0; break; }
                if (n > 200000000) break;
            }
            printf("%s,%s,%s,%zu,%s,%d,%.6f,%.6f,%d\n", argv[1], argv[2], fn, n, nm[k], zl, bc, ok ? bd : -1.0, ok);
            fflush(stdout);
        }
        free(in); free(z); free(back);
    }
    return 0;
}
