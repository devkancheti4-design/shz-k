/* shzx.c -- shz with a MAX mode.  THE BODY.
   The fast mode (shz.c, unchanged) codes a flag per guess and falls back to the byte itself.  The max mode codes every
   byte bit by bit: at each bit a mixer weighs
     - every hallucinator's guess (M repeat, T line above, N numbers carried forward, O last two bytes, F field recall,
       P record: the byte one record back, for binary tables) as an opinion on the next bit while its guess still
       agrees with the bits so far, and
     - eight context models (orders 1, 2, 3, 4, 6, the word so far with the word before, the field and place in the line
       above, the column of the record),
   two mixers (selected by how sure the repeat is, and by the bits so far), then two refinement stages (APMs).
   Zero-shot as before: everything starts empty and learns as it reads, the same on both sides.
   Every integer law (L_*) comes from shz_laws.h (machine-authored) or, with -DSHZ_REF, shz_ref.h. */
#define SHZ_NO_MAIN
#include "shz.c"
#ifdef SHZ_REF
#include "shzx_ref.h"
#else
#include "shzx_laws.h"
#endif

enum { XC = 12, XR = 4, XHN = 6 + XR, XIN = 1 + XC + XHN };
enum { HM, HT, HN, HO, HF, HP, HR0 };
static const int RCTX[XR] = {2, 3, 4, 5};   /* which contexts carry a run model: o3, o4, o6, word */

typedef struct { uint8_t chk, cnt; uint16_t p[15]; } XB;    /* a nibble's 15 counters: 32 bytes */

typedef struct {
    Model *m;
    XB *T[XC]; int shift[XC]; uint32_t hh[XC]; XB *b[XC];
    uint32_t HC[XHN * 16 * 16];
    int32_t WA[16 * 8][XIN], WB[256][XIN], WC[256][XIN];
    uint16_t *A2;
    uint32_t word, pword, c4, c8;
    uint32_t last[256], dprev[256], R, rst, stP;
    uint16_t *RT[XR]; int rshift[XR]; uint16_t *rcur[XR];
    int pb[XHN], pq[XHN], fld, pos;
} XModel;

static const uint32_t KX[12] = {0x3C6EF372u, 0xA54FF53Au, 0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u, 0xCBBB9D5Du, 0x629A292Au,
                                0x9159015Au, 0x152FECD8u, 0x67332667u, 0x8EB44A87u};

static XModel *xmodel_new(const uint8_t *buf, size_t n) {
    XModel *x = calloc(1, sizeof(XModel));
    if (!x) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    x->m = model_new(buf);
    int lg = 10; while (lg < 31 && ((size_t)1 << lg) < n) lg++;
    static const int cap[12] = {12, 18, 22, 23, 23, 23, 18, 21, 20, 20, 16, 23};
    for (int j = 0; j < XC; j++) {
        int bits = lg - 3; if (bits > cap[j]) bits = cap[j]; if (bits < 10) bits = 10;
        x->T[j] = calloc((size_t)1 << bits, sizeof(XB)); x->shift[j] = 32 - bits;
        if (!x->T[j]) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    }
    for (int k = 0; k < XHN * 16 * 16; k++) x->HC[k] = 1u << 31;
    for (int r = 0; r < XR; r++) {
        int bits = lg - 2; if (bits > 22) bits = 22; if (bits < 10) bits = 10;
        x->RT[r] = calloc((size_t)1 << bits, 2); x->rshift[r] = 32 - bits;
        if (!x->RT[r]) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    }
        for (int k = 0; k < 16 * 8; k++) for (int j = 0; j < XIN; j++) x->WA[k][j] = 14000;
    for (int k = 0; k < 256; k++) for (int j = 0; j < XIN; j++) x->WB[k][j] = x->WC[k][j] = 14000;
    x->A2 = malloc((size_t)65536 * 33 * 2);
    if (!x->A2) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    for (int j = 0; j < 33; j++) {
        int s = (j - 16) * 128; s = s > 2047 ? 2047 : s < -2047 ? -2047 : s;
        uint16_t v = (uint16_t)(squash(s) * 16);
        for (int c = 0; c < 65536; c++) x->A2[(size_t)c * 33 + j] = v;
    }
    return x;
}
static void xmodel_free(XModel *x) { for (int r = 0; r < XR; r++) free(x->RT[r]); for (int j = 0; j < XC; j++) free(x->T[j]); free(x->A2); model_free(x->m); free(x); }

/* the hallucinators' guesses for the next byte, and the field / place in the line above */
static void xgather(XModel *x) {
    Model *m = x->m; const uint8_t *buf = m->buf; uint32_t i = m->i;
    for (int h = 0; h < XHN; h++) x->pb[h] = -1;
    x->fld = 31; x->pos = 0;
    if (m->mlen) { x->pb[HM] = buf[m->mptr]; x->pq[HM] = (int)L_BKT(m->mlen); }
    if (m->lines && !m->full) {
        const Tok *cu = m->T[m->cur], *pv = m->T[m->prv];
        int np = m->nt[m->prv], t = m->nt[m->cur] - 1, tp = -1, o = 0, tc = -1, nc = -1;
        if (t >= 0) { tp = cu[t].al; o = cu[t].len; }
        if (t >= 0 && cu[t].cls != C_PUNCT && tp >= 0 && o < pv[tp].len && pv[tp].cls == cu[t].cls) {
            tc = buf[pv[tp].start + o]; x->fld = tp < 30 ? tp : 30; x->pos = o < 3 ? o : 3;
        } else {
            int tn = t < 0 ? 0 : (tp >= 0 ? tp + 1 : t + 1 + m->d);
            if (tn >= 0 && tn < np) { tc = buf[pv[tn].start]; if (m->sl[tn]) nc = m->S[tn][0]; }
            else if (tn == np) tc = '\n';
            x->fld = tn >= 0 && tn < 30 ? tn : 30; x->pos = 0;
        }
        if (t >= 0 && cu[t].cls == C_ALNUM && tp >= 0 && m->sl[tp] && m->nok) {
            if (o < m->sl[tp]) nc = m->S[tp][o];
            else if (o == m->sl[tp]) nc = tp + 1 < np ? buf[pv[tp + 1].start] : '\n';
        }
        if (tc >= 0) { x->pb[HT] = tc; x->pq[HT] = (int)L_BKT(m->st[PT]); }
        if (nc >= 0) { x->pb[HN] = nc; x->pq[HN] = (int)L_BKT(m->st[PN]); }
    }
    if (i >= 2) { x->pb[HO] = m->R2[buf[i - 2] << 8 | buf[i - 1]]; x->pq[HO] = (int)L_BKT(m->st[PO]); }
    if (m->fok && m->fq) { x->pb[HF] = buf[m->fq]; x->pq[HF] = (int)L_BKT(m->st[PF]); }
    if (x->R && i >= x->R) { x->pb[HP] = buf[i - x->R]; x->pq[HP] = (int)L_BKT(x->stP); }
}

/* the eight context hashes for the next byte */
static void xcontexts(XModel *x) {
    Model *m = x->m; const uint8_t *buf = m->buf; uint32_t i = m->i, c4 = x->c4;
    uint32_t v[XC];
    v[0] = c4 & 0xFFu; v[1] = c4 & 0xFFFFu; v[2] = c4 & 0xFFFFFFu; v[3] = c4;
    v[4] = L_HM1(c4) + (x->c8 & 0xFFFFu);
    v[5] = x->word;
    v[6] = (uint32_t)x->fld << 10 | (uint32_t)x->pos << 8 | (c4 & 0xFFu);
    v[7] = (x->R && i >= 2 * x->R) ? (x->R << 16 | (uint32_t)buf[i - x->R] << 8 | buf[i - 2 * x->R]) : 0x7FFF0000u;
    v[8] = c4 & 0xFFFF00u;                         /* sparse: the two bytes before the last */
    v[9] = c4 & 0xFF00FF00u;                       /* sparse: bytes 2 and 4 back */
    v[10] = (i & 3u) << 8 | (c4 & 0xFFu);          /* place in a 4-byte word, and the last byte */
    v[11] = x->word ^ L_HM1(x->pword);             /* the word so far with the word before */
    for (int j = 0; j < XC; j++) x->hh[j] = L_HM1(v[j] + KX[j]);
    for (int r = 0; r < XR; r++) {
        uint16_t *e = &x->RT[r][x->hh[RCTX[r]] >> x->rshift[r]];
        x->rcur[r] = e;
        if (*e & 255u) { x->pb[HR0 + r] = *e >> 8; x->pq[HR0 + r] = (int)L_BKT(*e & 255u); }
        else x->pb[HR0 + r] = -1;
    }
}

static inline XB *xbucket(XModel *x, int j, uint32_t h) {
    XB *b = &x->T[j][h >> x->shift[j]];
    uint8_t chk = (uint8_t)(h | 1u);
    if (b->chk == chk) return b;
    XB *b2 = &x->T[j][(h >> x->shift[j]) ^ 1u];
    if (b2->chk == chk) return b2;
    if (b2->cnt < b->cnt) b = b2;
    if (b->chk != chk) { b->chk = chk; b->cnt = 0; for (int k = 0; k < 15; k++) b->p[k] = 32768; }
    return b;
}

static inline uint16_t up16(uint16_t p, uint32_t bit, uint32_t R) {
    uint32_t x = p | R << 16;
    return (uint16_t)(bit ? p + L_INC16(x) : p - L_DEC16(x));
}

static inline uint32_t apm_p(const uint16_t *t, int s, int *ix) {
    int z = s + 2048, lo = z >> 7, w = z & 127;
    *ix = lo + (w >> 6);
    int a = t[lo] >> 4, b = t[lo + 1] >> 4;
    return (uint32_t)(a + L_SDELTA((uint32_t)(b - a + 4096) | (uint32_t)w << 13));
}

static inline int xcode_byte(XModel *x, AC *a, int c, const int ENC) {
    int c0 = 1, nib = 1;
    uint32_t a2c = L_HM1((x->c4 & 0xFFFFu) << 8 | 0x1000000u) ;
    for (int k = 0; k < 8; k++) {
        if (k == 0) for (int j = 0; j < XC; j++) x->b[j] = xbucket(x, j, x->hh[j]);
        else if (k == 4) for (int j = 0; j < XC; j++) x->b[j] = xbucket(x, j, L_HM1(x->hh[j] + (uint32_t)c0));
        int in[XIN], n = 0, eb[XHN], mq = 0;
        uint32_t *hc[XHN];
        in[n++] = 256;
        for (int j = 0; j < XC; j++) in[n++] = STR_T[x->b[j]->p[nib - 1] >> 4];
        for (int h = 0; h < XHN; h++) {
            int s = 0; hc[h] = 0;
            if (x->pb[h] >= 0 && ((x->pb[h] | 256) >> (8 - k)) == c0) {
                eb[h] = (x->pb[h] >> (7 - k)) & 1;
                hc[h] = &x->HC[((uint32_t)h * 16 + (uint32_t)x->pq[h]) * 16 + (uint32_t)k];
                s = STR_T[ctr_p(*hc[h])]; if (!eb[h]) s = -s;
                if (h == HM) mq = x->pq[HM];
            }
            in[n++] = s;
        }
        int32_t *wa = x->WA[mq * 8 + k], *wb = x->WB[c0], *wc = x->WC[x->c4 & 0xFFu];
        int64_t dA = 0, dB = 0, dC = 0;
        for (int q = 0; q < XIN; q++) { dA += (int64_t)in[q] * wa[q]; dB += (int64_t)in[q] * wb[q]; }
        for (int q = 0; q < XIN; q++) dC += (int64_t)in[q] * wc[q];
        int sC = L_CLAMP12((int32_t)(dC >> 16));
        int sA = L_CLAMP12((int32_t)(dA >> 16)), sB = L_CLAMP12((int32_t)(dB >> 16));
        int sm = ((sA + sB + 2 * sC) * 16384) >> 16;
        uint32_t pA = squash(sA), pB = squash(sB), pC = squash(sC), pm = squash(sm);
        int i2;
        uint16_t *t2 = x->A2 + (size_t)(L_HM1(a2c + (uint32_t)c0) >> 16) * 33;
        uint32_t p2 = apm_p(t2, sm, &i2);
                uint32_t p = L_MAX1((pm + 3 * p2 + 2) >> 2), bit;
        if (ENC) { bit = ((uint32_t)c >> (7 - k)) & 1u; enc_bit(a, bit, p); } else bit = dec_bit(a, p);
        STAT(3, bit, p);
        for (int j = 0; j < XC; j++) {
            uint16_t *pp = &x->b[j]->p[nib - 1];
            *pp = up16(*pp, bit, L_RATE(x->b[j]->cnt));
        }
        if (k == 0 || k == 4) for (int j = 0; j < XC; j++) x->b[j]->cnt = (uint8_t)L_NINC(x->b[j]->cnt);
        for (int h = 0; h < XHN; h++) if (hc[h]) *hc[h] = ctr_up(*hc[h], bit == (uint32_t)eb[h]);
        uint32_t eA = (uint32_t)((int)(bit << 12) - (int)pA + 4096) << 12, eB = (uint32_t)((int)(bit << 12) - (int)pB + 4096) << 12,
                 eC = (uint32_t)((int)(bit << 12) - (int)pC + 4096) << 12; (void)eC;
        for (int q = 0; q < XIN; q++) {
            uint32_t s = (uint32_t)(in[q] + 2048);
            wa[q] = L_WCLAMP(wa[q] + L_WSTEP(s | eA));
            wb[q] = L_WCLAMP(wb[q] + L_WSTEP(s | eB));
            wc[q] = L_WCLAMP(wc[q] + L_WSTEP(s | eC));
        }
        t2[i2] = up16(t2[i2], bit, 7);
        c0 = c0 * 2 + (int)bit; nib = nib * 2 + (int)bit; if (k == 3) nib = 1;
    }
    return c0 - 256;
}

/* learn from the byte just coded, then prepare the next */
static inline void xupdate(XModel *x, int c) {
    Model *m = x->m; uint32_t i = m->i;
    uint32_t seen = 0;
    static const int map[6] = {PM, PT, PN, PO, PF, -1};
    for (int h = 0; h < 6; h++) {
        uint32_t *st = map[h] >= 0 ? &m->st[map[h]] : &x->stP;
        if (x->pb[h] >= 0) { seen |= 1u << h; *st = x->pb[h] == c ? (*st < SHZ_CAP ? *st + 1 : SHZ_CAP) : 0; }
        else *st = 0;
    }
    (void)seen;
    uint32_t d = i + 1 - x->last[c];
    if (x->last[c] && d == x->dprev[c] && d >= 2 && d < 65536) {
        if (d == x->R) { if (x->rst < SHZ_CAP) x->rst++; }
        else if (x->rst == 0) x->R = d;
        else x->rst--;
    }
    x->dprev[c] = x->last[c] ? d : 0; x->last[c] = i + 1;
    if (L_ISUPPER((uint32_t)c) | L_ISLOWER((uint32_t)c)) x->word = L_HM1(x->word + ((uint32_t)c | 32u));
    else if (x->word) { x->pword = x->word; x->word = 0; }
    for (int r = 0; r < XR; r++) {
        uint16_t *e = x->rcur[r];
        if ((*e & 255u) && (uint32_t)(*e >> 8) == (uint32_t)c) { if ((*e & 255u) < 255u) (*e)++; }
        else *e = (uint16_t)((uint32_t)c << 8 | 1u);
    }
    x->c8 = x->c8 << 8 | x->c4 >> 24; x->c4 = x->c4 << 8 | (uint32_t)c;
    model_update(m, c);
    xgather(x); xcontexts(x);
}

size_t shzx_compress(const uint8_t *in, size_t n, uint8_t *out) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    if (n >= 0xFFFFFFF0u) { fprintf(stderr, "shz: inputs of 4 GB or more are not supported\n"); exit(2); }
    memcpy(out, "SHZ1", 4); put32(out + 4, (uint32_t)n); put32(out + 8, 0); put32(out + 12, crc32(in, n));
    out[16] = 2;
    if (n) {
        XModel *x = xmodel_new(in, n);
        xgather(x); xcontexts(x);
        AC a = {0, 0xFFFFFFFFu, 0, out + HDR, out + HDR + n + 8};
        for (size_t i = 0; i < n && a.p < a.e; i++) { xcode_byte(x, &a, in[i], 1); xupdate(x, in[i]); }
        for (int k = 0; k < 4; k++) { if (a.p < a.e) *a.p = (uint8_t)(a.x1 >> 24); a.p++; a.x1 <<= 8; }
        xmodel_free(x);
        size_t z = (size_t)(a.p - (out + HDR));
        if (z < n) return HDR + z;
    }
    out[16] = 0; memcpy(out + HDR, in, n);
    return HDR + n;
}

size_t shzx_decompress(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    if (len < HDR || memcmp(in, "SHZ1", 4)) return (size_t)-1;
    if (in[16] != 2) return shz_decompress(in, len, out, cap);
    size_t n = get32(in + 4);
    if (n > cap || n > (len - HDR) * 32768u + 64u) return (size_t)-1;
    XModel *x = xmodel_new(out, n);
    xgather(x); xcontexts(x);
    AC a = {0, 0xFFFFFFFFu, 0, (uint8_t *)in + HDR, (uint8_t *)in + len};
    for (int k = 0; k < 4; k++) a.x = (a.x << 8) | (a.p < a.e ? *a.p++ : 0);
    for (size_t i = 0; i < n; i++) { int c = xcode_byte(x, &a, 0, 0); out[i] = (uint8_t)c; xupdate(x, c); }
    xmodel_free(x);
    return crc32(out, n) == get32(in + 12) ? n : (size_t)-1;
}

#ifndef SHZX_NO_MAIN
static uint8_t *xslurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)z + 1); if (!b || fread(b, 1, (size_t)z, f) != (size_t)z) { perror(path); exit(1); }
    fclose(f); *n = (size_t)z; return b;
}
static void xspit(const char *path, const uint8_t *b, size_t n) {
    FILE *f = fopen(path, "wb"); if (!f || fwrite(b, 1, n, f) != n) { perror(path); exit(1); } fclose(f);
}
static double xnow(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc >= 4 && argv[1][1] == 0 && (argv[1][0] == 'c' || argv[1][0] == 'x' || argv[1][0] == 'd')) {
        size_t n; uint8_t *in = xslurp(argv[2], &n);
        if (argv[1][0] != 'd') {
            uint8_t *o = malloc(shz_bound(n));
            xspit(argv[3], o, argv[1][0] == 'x' ? shzx_compress(in, n, o) : shz_compress(in, n, o));
        } else {
            size_t cap = n >= 8 ? get32(in + 4) : 0;
            if (n >= 17 && in[16] != 0 && cap > (n - 17) * 32768u + 64u) { fprintf(stderr, "shz: damaged stream\n"); return 1; }
            uint8_t *o = malloc(cap + 1);
            if (!o) { fprintf(stderr, "shz: out of memory\n"); return 1; }
            size_t z = shzx_decompress(in, n, o, cap);
            if (z == (size_t)-1) { fprintf(stderr, "shz: damaged stream\n"); return 1; }
            xspit(argv[3], o, z);
        }
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "t") || !strcmp(argv[1], "tx"))) {
        int mx = argv[1][1] == 'x';
        size_t tin = 0, tout = 0; double tc = 0, td = 0;
        for (int k = 2; k < argc; k++) {
            size_t n; uint8_t *in = xslurp(argv[k], &n), *z = malloc(shz_bound(n)), *back = malloc(n + 1);
            double t0 = xnow(); size_t zl = mx ? shzx_compress(in, n, z) : shz_compress(in, n, z); double t1 = xnow();
            size_t bl = shzx_decompress(z, zl, back, n); double t2 = xnow();
            if (bl != n || memcmp(in, back, n)) { printf("ROUND TRIP FAILED: %s\n", argv[k]); return 1; }
            const char *nm = strrchr(argv[k], '/'); nm = nm ? nm + 1 : argv[k];
            printf("%-34s %10zu -> %9zu  %6.3f bpb  x%6.2f  c %6.2f MB/s  d %6.2f MB/s\n", nm, n, zl, 8.0 * zl / (n ? n : 1),
                   (double)n / zl, n / (t1 - t0) / 1e6, n / (t2 - t1) / 1e6);
            fflush(stdout);
            tin += n; tout += zl; tc += t1 - t0; td += t2 - t1; free(in); free(z); free(back);
        }
        printf("%-34s %10zu -> %9zu  %6.3f bpb  x%6.2f  c %6.2f MB/s  d %6.2f MB/s\n", "TOTAL", tin, tout, 8.0 * tout / tin,
               (double)tin / tout, tin / tc / 1e6, tin / td / 1e6);
        return 0;
    }
    fprintf(stderr, "usage: shzx c|x IN OUT (fast | max) | shzx d IN OUT | shzx t|tx FILE...\n");
    return 2;
}
#endif
