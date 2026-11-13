/* aib_bench.c -- shzb in-process, the same rows (and the same best-of-N timing) as bench/classic.c and bsc_bench.c.
     clang -O2 -fwrapv -DSHZ_NO_MAIN -Icodec -Ithird_party/libbsc bench/ai_bench.c third_party/libbsc/build/libbsc.a -lc++
     bench/ai_bench SPLIT CATEGORY FILE... */
#define SHZB_NO_MAIN
#include "shzb.c"
static double bnow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    const char *nm[4] = {"shzb-e1-fast", "shzb-e2-fast", "shzb-e1-max", "shzb-e2-max"};
    int fm[4] = {1, 1, 2, 2}, co[4] = {LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE, LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE};
    for (int a = 3; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { perror(argv[a]); return 1; }
        fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
        uint8_t *in = malloc(n + 1), *z = malloc(shzb_bound(n)), *back = malloc(n + 1);
        if (fread(in, 1, n, f) != n) { perror(argv[a]); return 1; } fclose(f);
        const char *fn = strrchr(argv[a], '/'); fn = fn ? fn + 1 : argv[a];
        for (int s = 0; s < 4; s++) {
            size_t zl = 0; double bc = 1e30, bd = 1e30, spent = 0; int reps = 0, ok = 1;
            while (reps < 3 || (spent < 0.25 && reps < 50)) { double t0 = bnow(); zl = shzb_compress(in, n, z, fm[s], co[s]); double dt = bnow() - t0; if (dt < bc) bc = dt; spent += dt; reps++; }
            spent = 0; reps = 0;
            while (ok && (reps < 3 || (spent < 0.25 && reps < 50))) {
                double t0 = bnow(); size_t bl = shzb_decompress(z, zl, back, n); double dt = bnow() - t0;
                if (bl != n || memcmp(in, back, n)) ok = 0;
                if (dt < bd) bd = dt; spent += dt; reps++;
            }
            printf("%s,%s,%s,%zu,%s,%zu,%.6f,%.6f,%d\n", argv[1], argv[2], fn, n, nm[s], zl, bc, ok ? bd : -1.0, ok);
            fflush(stdout);
        }
        free(in); free(z); free(back);
    }
    return 0;
}
