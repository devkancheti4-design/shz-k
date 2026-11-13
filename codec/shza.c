/* shza.c -- shz for AI transcripts: the float law, and streams.  THE BODY.

   AI services store transcripts as JSON lines full of machine-written numbers. In WildChat every moderation score
   is a 32-bit float printed as the shortest decimal of a 64-bit double ("0.0016908409306779504": 19 digits that
   carry 4 bytes). shza finds every number whose exact text this law reproduces:
       text == shortest round-trip decimal of (double)(float32 bits)
   checks it number by number, replaces it in the text by a marker byte, and keeps its 4 bytes in a float stream
   grouped by field (the JSON key in front of it). The text goes to bsc (block sorting, the cost leader on text);
   the float stream goes to shz (fast or max mode). Decompression regenerates every number's exact digits from its
   4 bytes ("structured hallucination", byte-identical). Integer laws: shz_laws.h, shzx_laws.h, shza_laws.h. */
#define SHZX_NO_MAIN
#include "shzx.c"
#ifdef SHZ_REF
#include "shza_ref.h"
#else
#include "shza_laws.h"
#endif
#include "libbsc/libbsc.h"

/* ---- the law's text side: Python's repr of a finite double, built from C's correctly rounded printf ---- */
static int short_repr(double d, char *out) {
    char e[48]; int n;
    /* the shortest round-trip precision: correctly rounded p digits round-trip for every p >= the shortest, never
       below it (a shorter rounding is never closer), so a binary search over p in [1, 17] finds it in 5 tries */
    int lo = 1, hi = 17;
    while (lo < hi) { int mid = (lo + hi) >> 1; snprintf(e, sizeof e, "%.*e", mid - 1, d); if (strtod(e, NULL) == d) hi = mid; else lo = mid + 1; }
    snprintf(e, sizeof e, "%.*e", lo - 1, d);
    if (strtod(e, NULL) != d) return -1;
    char dig[24]; int nd = 0, neg = e[0] == '-', k = neg;
    for (; e[k] && e[k] != 'e'; k++) if (e[k] >= '0' && e[k] <= '9') dig[nd++] = e[k];
    int ex = atoi(e + k + 1), decpt = ex + 1;
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    n = 0; if (neg) out[n++] = '-';
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) { out[n++] = '0'; out[n++] = '.'; for (int z = 0; z < -decpt; z++) out[n++] = '0'; for (int z = 0; z < nd; z++) out[n++] = dig[z]; }
        else if (decpt >= nd) { for (int z = 0; z < nd; z++) out[n++] = dig[z]; for (int z = nd; z < decpt; z++) out[n++] = '0'; out[n++] = '.'; out[n++] = '0'; }
        else { for (int z = 0; z < decpt; z++) out[n++] = dig[z]; out[n++] = '.'; for (int z = decpt; z < nd; z++) out[n++] = dig[z]; }
    } else {
        out[n++] = dig[0]; if (nd > 1) { out[n++] = '.'; for (int z = 1; z < nd; z++) out[n++] = dig[z]; }
        n += snprintf(out + n, 12, "e%c%02d", decpt - 1 < 0 ? '-' : '+', decpt - 1 < 0 ? 1 - decpt : decpt - 1);
    }
    out[n] = 0; return n;
}

/* format d with exactly p significant digits in Python's repr layout; returns the length */
static int fmt_p(double d, int p, char *out) {
    char e[48]; snprintf(e, sizeof e, "%.*e", p - 1, d);
    char dig[24]; int nd = 0, neg = e[0] == '-', k = neg, n = 0;
    for (; e[k] && e[k] != 'e'; k++) if (e[k] >= '0' && e[k] <= '9') dig[nd++] = e[k];
    int decpt = atoi(e + k + 1) + 1;
    while (nd > 1 && dig[nd - 1] == '0') nd--;
    if (neg) out[n++] = '-';
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) { out[n++] = '0'; out[n++] = '.'; for (int z = 0; z < -decpt; z++) out[n++] = '0'; for (int z = 0; z < nd; z++) out[n++] = dig[z]; }
        else if (decpt >= nd) { for (int z = 0; z < nd; z++) out[n++] = dig[z]; for (int z = nd; z < decpt; z++) out[n++] = '0'; out[n++] = '.'; out[n++] = '0'; }
        else { for (int z = 0; z < decpt; z++) out[n++] = dig[z]; out[n++] = '.'; for (int z = decpt; z < nd; z++) out[n++] = dig[z]; }
    } else {
        out[n++] = dig[0]; if (nd > 1) { out[n++] = '.'; for (int z = 1; z < nd; z++) out[n++] = dig[z]; }
        n += snprintf(out + n, 12, "e%c%02d", decpt - 1 < 0 ? '-' : '+', decpt - 1 < 0 ? 1 - decpt : decpt - 1);
    }
    out[n] = 0; return n;
}
/* is tok (len bytes, value d) exactly Python's repr of d?  2 printf calls and 1 strtod */
static int is_repr(const char *tok, int len, double d) {
    int p0 = 0, lead = 1;
    for (int k = 0; k < len && tok[k] != 'e' && tok[k] != 'E'; k++) {
        if (tok[k] >= '1' && tok[k] <= '9') { lead = 0; p0++; } else if (tok[k] == '0' && !lead) p0++;
    }
    if (p0 < 1 || p0 > 17) return 0;
    char rb[48];
    if (fmt_p(d, p0, rb) != len || memcmp(rb, tok, (size_t)len)) return 0;
    if (p0 > 1) { char e[48]; snprintf(e, sizeof e, "%.*e", p0 - 2, d); if (strtod(e, NULL) == d) return 0; }
    return 1;
}

static inline int wordc(int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '.'; }

/* the field of a number: the JSON key right before it ("key": number) -> a small id, first come first numbered */
typedef struct { uint32_t h[1024]; int id[1024]; int n; } Fields;
static int field_of(Fields *F, const uint8_t *t, size_t at) {
    size_t k = at;
    while (k > 0 && (t[k - 1] == ' ')) k--;
    uint32_t h = 0x9E3779B9u;
    if (k > 0 && t[k - 1] == ':') {
        k--; while (k > 0 && t[k - 1] == ' ') k--;
        if (k > 0 && t[k - 1] == '"') { k--; size_t e = k; while (k > 0 && t[k - 1] != '"' && e - k < 64) k--; for (size_t q = k; q < e; q++) h = L_HM1(h + t[q]); }
    }
    uint32_t s = (h >> 22) & 1023u;
    while (F->id[s] && F->h[s] != h) s = (s + 1) & 1023u;
    if (!F->id[s]) { if (F->n >= 1000) return 0; F->h[s] = h; F->id[s] = ++F->n; }
    return F->id[s] - 1;
}

enum { AHDR = 4 + 4 + 4 + 1 + 1 + 4 + 4 + 4 };
size_t shza_bound(size_t n) { return n + n / 8 + 4096; }

/* fmode: 1 = floats with shz fast, 2 = with shz max;  coder: LIBBSC_CODER_QLFC_STATIC or _ADAPTIVE */
size_t shza_compress(const uint8_t *in, size_t n, uint8_t *out, int fmode, int coder) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    static int bsc_ready; if (!bsc_ready) { bsc_init(LIBBSC_FEATURE_FASTMODE); bsc_ready = 1; }
    int used[256] = {0}; for (size_t i = 0; i < n; i++) used[in[i]] = 1;
    int marker = -1; for (int c = 1; c < 32; c++) if (!used[c]) { marker = c; break; }
    uint8_t *txt = malloc(n + 1); uint32_t *fv = malloc((n / 2 + 16) * sizeof(uint32_t)); int *ff = malloc((n / 2 + 16) * sizeof(int));
    Fields *F = calloc(1, sizeof(Fields));
    size_t tn = 0, nf = 0; char rb[48];
    for (size_t i = 0; i < n; ) {
        int c = in[i];
        if (marker > 0 && (c == '-' || (c >= '0' && c <= '9')) && (i == 0 || !wordc(in[i - 1]))) {
            size_t j = i + (c == '-'); int dot = 0, ex = 0, digits = 0;
            while (j < n && in[j] >= '0' && in[j] <= '9') { j++; digits++; }
            if (j < n && in[j] == '.') { dot = 1; j++; while (j < n && in[j] >= '0' && in[j] <= '9') { j++; digits++; } }
            if (j < n && (in[j] == 'e' || in[j] == 'E') && digits) {
                size_t q = j + 1; if (q < n && (in[q] == '+' || in[q] == '-')) q++;
                if (q < n && in[q] >= '0' && in[q] <= '9') { ex = 1; j = q; while (j < n && in[j] >= '0' && in[j] <= '9') j++; }
            }
            size_t len = j - i;
            if (digits && (dot || ex) && len < 40 && (j >= n || !wordc(in[j]))) {
                char tok[48]; memcpy(tok, in + i, len); tok[len] = 0;
                double d = strtod(tok, NULL); float f = (float)d;
                if ((double)f == d && is_repr(tok, (int)len, d)) {
                    uint32_t u; memcpy(&u, &f, 4);
                    fv[nf] = u; ff[nf] = field_of(F, in, i); nf++;
                    txt[tn++] = (uint8_t)marker; i = j; continue;
                }
            }
            memcpy(txt + tn, in + i, len ? len : 1); tn += len ? len : 1; i += len ? len : 1; continue;
        }
        txt[tn++] = (uint8_t)c; i++;
    }
    /* the float stream: field by field (first appearance order), each value's ordered bits, big-endian */
    int nfld = F->n; size_t *cnt = calloc((size_t)nfld + 1, sizeof(size_t)), *at = calloc((size_t)nfld + 1, sizeof(size_t));
    for (size_t k = 0; k < nf; k++) cnt[ff[k]]++;
    for (int g = 1; g <= nfld; g++) at[g] = at[g - 1] + cnt[g - 1];
    uint8_t *fs = malloc(nf * 4 + 16);
    for (size_t k = 0; k < nf; k++) {
        uint32_t o = L_ORD32(fv[k]); size_t p = 4 * at[ff[k]]++;
        fs[p] = (uint8_t)(o >> 24); fs[p + 1] = (uint8_t)(o >> 16); fs[p + 2] = (uint8_t)(o >> 8); fs[p + 3] = (uint8_t)o;
    }
    memcpy(out, "SHZA", 4); put32(out + 4, (uint32_t)n); put32(out + 8, crc32(in, n));
    out[12] = (uint8_t)(marker < 0 ? 0 : marker); out[13] = (uint8_t)fmode;
    size_t pos = AHDR;
    int tz = tn ? bsc_compress(txt, out + pos, (int)tn, LIBBSC_DEFAULT_LZPHASHSIZE, LIBBSC_DEFAULT_LZPMINLEN, LIBBSC_BLOCKSORTER_BWT, coder, LIBBSC_FEATURE_FASTMODE) : 0;
    if (tz < 0) { fprintf(stderr, "shza: bsc failed\n"); exit(2); }
    pos += (size_t)tz;
    size_t fz = nf ? (fmode == 2 ? shzx_compress(fs, nf * 4, out + pos) : shz_compress(fs, nf * 4, out + pos)) : 0;
    pos += fz;
    put32(out + 14, (uint32_t)tn); put32(out + 18, (uint32_t)tz); put32(out + 22, (uint32_t)nf);
    free(txt); free(fv); free(ff); free(F); free(cnt); free(at); free(fs);
    return pos;
}

size_t shza_decompress(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    static int bsc_ready; if (!bsc_ready) { bsc_init(LIBBSC_FEATURE_FASTMODE); bsc_ready = 1; }
    if (len < AHDR || memcmp(in, "SHZA", 4)) return (size_t)-1;
    size_t n = get32(in + 4), tn = get32(in + 14), tz = get32(in + 18), nf = get32(in + 22);
    int marker = in[12];
    if (n > cap || AHDR + tz > len) return (size_t)-1;
    uint8_t *txt = malloc(tn + 1);
    if (tn) {
        int bs = 0, ds = 0;
        if (bsc_block_info(in + AHDR, LIBBSC_HEADER_SIZE, &bs, &ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR || (size_t)ds != tn ||
            bsc_decompress(in + AHDR, bs, txt, ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR) { free(txt); return (size_t)-1; }
    }
    uint8_t *fs = malloc(nf * 4 + 16);
    if (nf && shzx_decompress(in + AHDR + tz, len - AHDR - tz, fs, nf * 4) != nf * 4) { free(txt); free(fs); return (size_t)-1; }
    /* fields from the text, in order; then each field's floats in order */
    Fields *F = calloc(1, sizeof(Fields)); int *ff = malloc((nf + 1) * sizeof(int)); size_t k = 0;
    for (size_t i = 0; i < tn; i++) if (txt[i] == marker && marker) { if (k >= nf) { k = nf + 1; break; } ff[k++] = field_of(F, txt, i); }
    if (k != nf) { free(txt); free(fs); free(F); free(ff); return (size_t)-1; }
    int nfld = F->n; size_t *cnt = calloc((size_t)nfld + 1, sizeof(size_t)), *at = calloc((size_t)nfld + 1, sizeof(size_t));
    for (size_t q = 0; q < nf; q++) cnt[ff[q]]++;
    for (int g = 1; g <= nfld; g++) at[g] = at[g - 1] + cnt[g - 1];
    size_t o = 0, q = 0; char rb[48];
    for (size_t i = 0; i < tn; i++) {
        if (txt[i] == marker && marker) {
            size_t p = 4 * at[ff[q++]]++;
            uint32_t u = L_UNORD32((uint32_t)fs[p] << 24 | (uint32_t)fs[p + 1] << 16 | (uint32_t)fs[p + 2] << 8 | fs[p + 3]);
            float f; memcpy(&f, &u, 4);
            int l = short_repr((double)f, rb);
            if (l < 0 || o + (size_t)l > cap) { o = cap + 1; break; }
            memcpy(out + o, rb, (size_t)l); o += (size_t)l;
        } else { if (o >= cap) { o = cap + 1; break; } out[o++] = txt[i]; }
    }
    free(txt); free(fs); free(F); free(ff); free(cnt); free(at);
    if (o != n) return (size_t)-1;
    return crc32(out, n) == get32(in + 8) ? n : (size_t)-1;
}

#ifndef SHZA_NO_MAIN
static uint8_t *aslurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)z + 1); if (!b || fread(b, 1, (size_t)z, f) != (size_t)z) { perror(path); exit(1); }
    fclose(f); *n = (size_t)z; return b;
}
static void aspit(const char *path, const uint8_t *b, size_t n) {
    FILE *f = fopen(path, "wb"); if (!f || fwrite(b, 1, n, f) != n) { perror(path); exit(1); } fclose(f);
}
static double anow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc >= 3 && !strcmp(argv[1], "t")) {          /* shza t FILE...: every setting, sizes and speeds */
        const char *nm[4] = {"shza(e1,fast)", "shza(e2,fast)", "shza(e1,max)", "shza(e2,max)"};
        int fm[4] = {1, 1, 2, 2}, co[4] = {LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE, LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE};
        for (int a = 2; a < argc; a++) {
            size_t n; uint8_t *in = aslurp(argv[a], &n), *z = malloc(shza_bound(n)), *back = malloc(n + 1);
            const char *fn = strrchr(argv[a], '/'); fn = fn ? fn + 1 : argv[a];
            for (int s = 0; s < 4; s++) {
                double t0 = anow(); size_t zl = shza_compress(in, n, z, fm[s], co[s]); double t1 = anow();
                size_t bl = shza_decompress(z, zl, back, n); double t2 = anow();
                int ok = bl == n && !memcmp(in, back, n);
                printf("%-28s %-14s %10zu -> %9zu  x%6.2f  c %6.2f MB/s  d %6.2f MB/s  %s\n", fn, nm[s], n, zl, (double)n / zl,
                       n / (t1 - t0) / 1e6, n / (t2 - t1) / 1e6, ok ? "exact" : "ROUND TRIP FAILED");
                fflush(stdout);
            }
            free(in); free(z); free(back);
        }
        return 0;
    }
    if (argc >= 4 && (!strcmp(argv[1], "c") || !strcmp(argv[1], "d"))) {
        size_t n; uint8_t *in = aslurp(argv[2], &n);
        if (argv[1][0] == 'c') { uint8_t *o = malloc(shza_bound(n)); aspit(argv[3], o, shza_compress(in, n, o, 1, LIBBSC_CODER_QLFC_ADAPTIVE)); }
        else {
            size_t cap = n >= 8 ? get32(in + 4) : 0; uint8_t *o = malloc(cap + 1);
            size_t z = shza_decompress(in, n, o, cap);
            if (z == (size_t)-1) { fprintf(stderr, "shza: damaged stream\n"); return 1; }
            aspit(argv[3], o, z);
        }
        return 0;
    }
    fprintf(stderr, "usage: shza c IN OUT | shza d IN OUT | shza t FILE...\n");
    return 2;
}
#endif
