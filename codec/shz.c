/* shz.c -- a structured-hallucination, zero-shot compressor and decompressor.  THE BODY.

   The decoder hallucinates each next byte from the structure of what it has already rebuilt:
     M  the continuation of the longest recent repeat of the current context (a match),
     T  the previous line, aligned token by token (a token is a run of letters/digits, a run of spaces, or one
        punctuation byte), so a log line or a CSV row is predicted from the row above it,
     N  the previous line's numbers carried forward: n + (n - n_before), so counters, sequence numbers and
        timestamps are predicted before they are seen,
     O  what followed the last two bytes the last time.
   The encoder sends only which hallucination was right (a binary flag per candidate, arithmetic-coded with an
   adaptive probability), or the byte itself when none was.  Zero-shot: no training and no dictionary; every model
   starts empty and learns as it goes, identically on both sides.

   Every integer law (L_*) is in shz_laws.h (authored by integer program synthesis) or, with -DSHZ_REF, its plain
   meaning in shz_ref.h.  The body owns the loops, the memory and the control flow. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __APPLE__
#include <pthread.h>
#endif
#ifdef SHZ_REF
#include "shz_ref.h"
#else
#include "shz_laws.h"
#endif

enum { HB = 22, MAXT = 128, SLEN = 24, NP = 5, MINLEN = 5, MAXV = 32, HDR = 17 };
enum { PM, PT, PN, PO, PF };
enum { C_PUNCT = 0, C_ALNUM = 1, C_SPACE = 2 };

typedef struct { uint32_t x1, x2, x; uint8_t *p, *e; } AC;
typedef struct { uint32_t start; uint16_t len; uint8_t cls, num; int16_t al; } Tok;

typedef struct {
    const uint8_t *buf; uint32_t i;
    uint32_t *H, h, mptr, mlen;                     /* M */
    uint8_t R2[65536];                              /* O */
    Tok T[3][MAXT]; int nt[3], cur, prv, pp, full, d, lines;   /* T: this line, the previous, the one before */
    uint8_t S[MAXT][SLEN], sl[MAXT]; int nok;       /* N: per previous-line token, its number carried forward */
    uint32_t st[NP];                                /* each hallucinator's current run of right guesses */
    uint32_t F[3 * 512];                            /* flag counters A: rank x (who agrees, how sure) */
    uint32_t FB[3 * 32 * 32 * 4];                   /* flag counters B: rank x who agrees x field x place in token */
    uint32_t FC[3 * 32 * 256];                      /* flag counters C: rank x who agrees x the last byte */
    uint32_t FD[3 * 32 * 256];                      /* flag counters D: rank x who agrees x the guessed byte */
    uint32_t *FR, fh, fq; int fok;                  /* F: field recall, by (field, token prefix) */
    uint32_t L4[32 * 4][256];                       /* literal counters: field x place in token */
    int32_t WF[3 * 32][4], WL[16][4];               /* mixer weights: flags by rank x who; literals by depth x miss */
    uint32_t L1[256][256], L2[257][256];            /* literal counters: after the last byte; after the miss */
    uint32_t *L3;                                   /* literal counters: after the last two bytes (hashed rows) */
    uint8_t ctab[256], dtab[256];
} Model;

static uint32_t CRC_T[256];
static const uint16_t SQ_T[33] = {1, 2, 3, 6, 10, 16, 27, 45, 73, 120, 194, 310, 488, 747, 1101, 1546, 2047, 2549, 2994,
                                  3348, 3607, 3785, 3901, 3975, 4022, 4050, 4068, 4079, 4085, 4089, 4092, 4093, 4094};
static int16_t STR_T[4096];
static uint16_t SQ_TAB[4095];
static inline uint32_t squash(int x) { return SQ_TAB[x + 2047]; }   /* x in [-2047, 2047] -> p in [1, 4094] */
static void tables_init(void) {
    for (int x = -2047; x <= 2047; x++) {
        int j = (x >> 7) + 16;
        SQ_TAB[x + 2047] = (uint16_t)(SQ_T[j] + L_DELTA((uint32_t)(SQ_T[j + 1] - SQ_T[j]) | (uint32_t)(x & 127) << 12));
    }
    int pi = 0;
    for (int x = -2047; x <= 2047; x++) { int v = (int)squash(x); for (int k = pi; k <= v; k++) STR_T[k] = (int16_t)x; pi = v + 1; }
    for (int k = pi; k < 4096; k++) STR_T[k] = 2047;
}
#ifdef SHZ_STATS
#include <math.h>
double ST_bits[8]; long ST_n[8], ST_hit[NP+1][3], ST_lit, ST_litnoc, ST_pred[NP], ST_predok[NP];
#define STAT(k, bit, p) (ST_bits[k] += -log2((bit) ? (p) / 4096.0 : 1 - (p) / 4096.0), ST_n[k]++)
#else
#define STAT(k, bit, p) ((void)0)
#endif

/* ---------------- binary arithmetic coder (carryless, 32-bit) ---------------- */
static inline void enc_bit(AC *a, uint32_t bit, uint32_t p) {
    uint32_t xm = a->x1 + L_SPLIT(((a->x2 - a->x1) >> 12) | (p << 20));
    if (bit) a->x2 = xm; else a->x1 = xm + 1;
    while (L_SAMETOP(a->x1 ^ a->x2)) {
        if (a->p < a->e) *a->p = (uint8_t)(a->x2 >> 24);
        a->p++; a->x1 <<= 8; a->x2 = (a->x2 << 8) | 255;
    }
}
static inline uint32_t dec_bit(AC *a, uint32_t p) {
    uint32_t xm = a->x1 + L_SPLIT(((a->x2 - a->x1) >> 12) | (p << 20));
    uint32_t bit = a->x <= xm;
    if (bit) a->x2 = xm; else a->x1 = xm + 1;
    while (L_SAMETOP(a->x1 ^ a->x2)) {
        a->x1 <<= 8; a->x2 = (a->x2 << 8) | 255;
        a->x = (a->x << 8) | (a->p < a->e ? *a->p++ : 0);
    }
    return bit;
}

/* ---------------- adaptive counters: s = p22 << 10 | n ---------------- */
static inline uint32_t ctr_p(uint32_t s) { return L_MAX1(L_TOP12(s)); }
static inline uint32_t ctr_up(uint32_t s, uint32_t bit) {
    uint32_t n = s & 1023u, p = s >> 10, x = p | L_RATE(n) << 22;
    return (bit ? p + L_INC1(x) : p - L_DEC0(x)) << 10 | L_NINC(n);
}

/* ---------------- N: a number carried forward, n + (n - m), in decimal ---------------- */
static inline uint32_t DADD(uint32_t x) { return L_DFIX(L_DSUM(x)); }   /* digit | carry << 4 of a + b + k */
static inline uint32_t DSUB(uint32_t x) { return L_BFIX(L_DDIF(x)); }   /* digit | borrow << 4 of a - b - k */
static int extrap(const uint8_t *a, int la, const uint8_t *b, int lb, uint8_t *out) {
    if (la > 20 || lb > 20) return 0;
    uint8_t A[24] = {0}, B[24] = {0}, D[24] = {0}, R[24] = {0};
    for (int k = 0; k < la; k++) A[k] = (uint8_t)(a[la - 1 - k] - '0');
    for (int k = 0; k < lb; k++) B[k] = (uint8_t)(b[lb - 1 - k] - '0');
    int n = la > lb ? la : lb;
    uint32_t bo = 0;
    for (int k = 0; k < n; k++) { uint32_t r = DSUB(A[k] | B[k] << 4 | bo << 8); D[k] = r & 15; bo = r >> 4; }
    uint32_t neg = bo;
    if (neg) { bo = 0; for (int k = 0; k < n; k++) { uint32_t r = DSUB(B[k] | A[k] << 4 | bo << 8); D[k] = r & 15; bo = r >> 4; } }
    uint32_t cy = 0;
    for (int k = 0; k <= n; k++) {
        uint32_t r = neg ? DSUB(A[k] | D[k] << 4 | cy << 8) : DADD(A[k] | D[k] << 4 | cy << 8);
        R[k] = r & 15; cy = r >> 4;
    }
    if (cy) return 0;                                   /* it would go below zero: no hallucination */
    int top = n; while (top > 0 && R[top] == 0) top--;
    int len = top + 1;
    if (la > 1 && a[0] == '0' && len < la) len = la;    /* a fixed-width field keeps its leading zeros */
    if (len > SLEN) return 0;
    for (int k = 0; k < len; k++) out[k] = (uint8_t)('0' + R[len - 1 - k]);
    return len;
}

static Model *model_new(const uint8_t *buf) {
    Model *m = calloc(1, sizeof(Model));
    m->H = calloc((size_t)1 << HB, sizeof(uint32_t));
    if (!m || !m->H) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    m->buf = buf; m->cur = 0; m->prv = 1; m->pp = 2;
    for (int k = 0; k < 3 * 512; k++) m->F[k] = 1u << 31;
    m->FR = calloc((size_t)1 << 20, 4);
    if (!m->FR) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    for (int r = 0; r < 256; r++) for (int k = 0; k < 256; k++) m->L1[r][k] = 1u << 31;
    for (int r = 0; r < 257; r++) for (int k = 0; k < 256; k++) m->L2[r][k] = 1u << 31;
    for (size_t k = 0; k < sizeof m->FB / 4; k++) m->FB[k] = 1u << 31;
    for (size_t k = 0; k < sizeof m->FC / 4; k++) m->FC[k] = 1u << 31;
    for (size_t k = 0; k < sizeof m->FD / 4; k++) m->FD[k] = 1u << 31;
    for (int r = 0; r < 128; r++) for (int k = 0; k < 256; k++) m->L4[r][k] = 1u << 31;
    for (int k = 0; k < 96; k++) for (int j = 0; j < 4; j++) m->WF[k][j] = 16000;
    for (int k = 0; k < 16; k++) for (int j = 0; j < 4; j++) m->WL[k][j] = 16000;
    m->L3 = malloc(((size_t)1 << 20) * 4);
    if (!m->L3) { fprintf(stderr, "shz: out of memory\n"); exit(2); }
    for (size_t k = 0; k < ((size_t)1 << 20); k++) m->L3[k] = 1u << 31;
    for (uint32_t c = 0; c < 256; c++) {
        uint32_t alnum = L_ISDIG(c) | L_ISUPPER(c) | L_ISLOWER(c) | L_ISUND(c) | (c >> 7);
        m->ctab[c] = (uint8_t)(alnum + 2 * L_ISSP(c)); m->dtab[c] = (uint8_t)L_ISDIG(c);
    }
    return m;
}
static void model_free(Model *m) { free(m->H); free(m->L3); free(m->FR); free(m); }

/* code one byte: ENC sends c, the decoder (ENC = 0) returns it */
static inline int code_byte(Model *m, AC *a, int c, const int ENC) {
    const uint8_t *buf = m->buf;
    int cb[NP], cp[NP], n = 0, fld = 31, pos = 0;
    uint32_t c1 = m->i ? buf[m->i - 1] : 0;
    if (m->mlen) { cb[n] = buf[m->mptr]; cp[n++] = PM; }
    if (m->lines && !m->full) {
        const Tok *cu = m->T[m->cur], *pv = m->T[m->prv];
        int np = m->nt[m->prv], t = m->nt[m->cur] - 1, tp = -1, o = 0, tc = -1, nc = -1;
        if (t >= 0) { tp = cu[t].al; o = cu[t].len; }
        if (t >= 0 && cu[t].cls != C_PUNCT && tp >= 0 && o < pv[tp].len && pv[tp].cls == cu[t].cls) {
            tc = buf[pv[tp].start + o]; fld = tp < 30 ? tp : 30; pos = o < 3 ? o : 3;
        } else {
            int tn = t < 0 ? 0 : (tp >= 0 ? tp + 1 : t + 1 + m->d);
            if (tn >= 0 && tn < np) { tc = buf[pv[tn].start]; if (m->sl[tn]) nc = m->S[tn][0]; }
            else if (tn == np) tc = '\n';
            fld = tn >= 0 && tn < 30 ? tn : 30; pos = 0;
        }
        if (t >= 0 && cu[t].cls == C_ALNUM && tp >= 0 && m->sl[tp] && m->nok) {
            if (o < m->sl[tp]) nc = m->S[tp][o];
            else if (o == m->sl[tp]) nc = tp + 1 < np ? buf[pv[tp + 1].start] : '\n';
        }
        if (tc >= 0) { cb[n] = tc; cp[n++] = PT; }
        if (nc >= 0) { cb[n] = nc; cp[n++] = PN; }
    }
    if (m->i >= 2) { cb[n] = m->R2[buf[m->i - 2] << 8 | buf[m->i - 1]]; cp[n++] = PO; }
    if (m->fok && m->fq) { cb[n] = buf[m->fq]; cp[n++] = PF; }

    /* group equal guesses: who agrees, and the surest of them */
    int gb[NP], ng = 0; uint32_t gc[NP], gp[NP];
    for (int k = 0; k < n; k++) {
        uint32_t q = L_BKT(cp[k] == PM ? m->mlen : m->st[cp[k]]);
        int j = 0; while (j < ng && gb[j] != cb[k]) j++;
        if (j == ng) { gb[ng] = cb[k]; gc[ng] = (1u << cp[k]) << 4 | q; ng++; }
        else { gc[j] |= (1u << cp[k]) << 4; if (q > (gc[j] & 15u)) gc[j] = (gc[j] & ~15u) | q; }
    }
    for (int j = 0; j < ng; j++) gp[j] = ctr_p(m->F[gc[j]]);
    for (int j = 1; j < ng; j++)                          /* most probable first */
        for (int k = j; k > 0 && gp[k] > gp[k - 1]; k--) {
            uint32_t tp = gp[k]; gp[k] = gp[k - 1]; gp[k - 1] = tp;
            uint32_t tq = gc[k]; gc[k] = gc[k - 1]; gc[k - 1] = tq;
            int tb = gb[k]; gb[k] = gb[k - 1]; gb[k - 1] = tb;
        }
    int r;
    for (r = 0; r < ng; r++) {
        uint32_t rk = r < 2 ? (uint32_t)r : 2u, who = gc[r] >> 4;
        uint32_t *fa = &m->F[rk * 512 + gc[r]], *fb = &m->FB[((rk * 32 + who) * 32 + (uint32_t)fld) * 4 + (uint32_t)pos],
                 *fc = &m->FC[(rk * 32 + who) * 256 + c1], *fd = &m->FD[(rk * 32 + who) * 256 + (uint32_t)gb[r]];
        int32_t *w = m->WF[rk * 32 + who];
        int sa = STR_T[ctr_p(*fa)], sb = STR_T[ctr_p(*fb)], sc = STR_T[ctr_p(*fc)], sd = STR_T[ctr_p(*fd)];
        uint32_t p = squash(L_CLAMP12((sa * w[0] + sb * w[1] + sc * w[2] + sd * w[3]) >> 16)), bit;
        if (ENC) { bit = (uint32_t)(c == gb[r]); enc_bit(a, bit, p); } else bit = dec_bit(a, p);
        STAT(r < 2 ? r : 2, bit, p);
        int e = (int)(bit << 12) - (int)p;
        uint32_t ee = (uint32_t)(e + 4096) << 12;
        w[0] = L_WCLAMP(w[0] + L_WSTEP((uint32_t)(sa + 2048) | ee));
        w[1] = L_WCLAMP(w[1] + L_WSTEP((uint32_t)(sb + 2048) | ee));
        w[2] = L_WCLAMP(w[2] + L_WSTEP((uint32_t)(sc + 2048) | ee));
        w[3] = L_WCLAMP(w[3] + L_WSTEP((uint32_t)(sd + 2048) | ee));
        *fa = ctr_up(*fa, bit); *fb = ctr_up(*fb, bit); *fc = ctr_up(*fc, bit); *fd = ctr_up(*fd, bit);
        if (bit) { c = gb[r]; break; }
    }
    if (r == ng) {                                        /* no hallucination was right: the byte itself */
        uint32_t *t1 = m->L1[c1], *t2 = m->L2[ng ? gb[0] : 256], node = 1;
        uint32_t *t3 = m->L3 + ((size_t)L_HASH2((m->i >= 2 ? buf[m->i - 2] : 0) | c1 << 8) << 8), *t4 = m->L4[fld * 4 + pos];
        for (int k = 7; k >= 0; k--) {
            if (k == 0) {                                 /* the last bit is forced if one of the two is excluded */
                int x0 = 0, x1 = 0;
                for (int j = 0; j < ng; j++) { if ((uint32_t)gb[j] == ((node << 1) & 255u)) x0 = 1; if ((uint32_t)gb[j] == (((node << 1) | 1u) & 255u)) x1 = 1; }
                if (x0 || x1) { node = node * 2 + (uint32_t)x0; break; }
            }
            int32_t *w = m->WL[(uint32_t)(7 - k) * 2 + (ng ? 1u : 0u)];
            int s1 = STR_T[ctr_p(t1[node])], s2 = STR_T[ctr_p(t2[node])], s3 = STR_T[ctr_p(t3[node])], s4 = STR_T[ctr_p(t4[node])];
            uint32_t p = squash(L_CLAMP12((s1 * w[0] + s2 * w[1] + s3 * w[2] + s4 * w[3]) >> 16)), bit;
            if (ENC) { bit = ((uint32_t)c >> k) & 1u; enc_bit(a, bit, p); } else bit = dec_bit(a, p);
            STAT(3 + (ng ? 1 : 0), bit, p);
            int e = (int)(bit << 12) - (int)p;
            uint32_t ee = (uint32_t)(e + 4096) << 12;
            w[0] = L_WCLAMP(w[0] + L_WSTEP((uint32_t)(s1 + 2048) | ee));
            w[1] = L_WCLAMP(w[1] + L_WSTEP((uint32_t)(s2 + 2048) | ee));
            w[2] = L_WCLAMP(w[2] + L_WSTEP((uint32_t)(s3 + 2048) | ee));
            w[3] = L_WCLAMP(w[3] + L_WSTEP((uint32_t)(s4 + 2048) | ee));
            t1[node] = ctr_up(t1[node], bit); t2[node] = ctr_up(t2[node], bit); t3[node] = ctr_up(t3[node], bit); t4[node] = ctr_up(t4[node], bit);
            node = node * 2 + bit;
        }
        c = (int)node - 256;
    }
#ifdef SHZ_STATS
    if (ENC) { if (r == ng) { ST_lit++; if (!ng) ST_litnoc++; } for (int k = 0; k < n; k++) { ST_pred[cp[k]]++; ST_predok[cp[k]] += cb[k] == c; } }
#endif
    uint32_t seen = 0;
    for (int k = 0; k < n; k++) { seen |= 1u << cp[k]; m->st[cp[k]] = cb[k] == c ? (m->st[cp[k]] < SHZ_CAP ? m->st[cp[k]] + 1 : SHZ_CAP) : 0; }
    for (int j = 0; j < NP; j++) if (!(seen >> j & 1u)) m->st[j] = 0;
    return c;
}

/* learn from the byte just coded (buf[i] == c) */
static inline void model_update(Model *m, int c) {
    const uint8_t *buf = m->buf; uint32_t i = m->i;
    if (m->mlen) { if (buf[m->mptr] == c) { m->mptr++; if (m->mlen < SHZ_CAP) m->mlen++; } else m->mlen = 0; }
    m->h = L_HASH(m->h | (uint32_t)c << 22);
    if (!m->mlen && i >= MINLEN) {
        uint32_t p = m->H[m->h];
        if (p) {
            uint32_t L = 0;
            while (L < MAXV && L < p && L <= i && buf[p - 1 - L] == buf[i - L]) L++;
            if (L >= MINLEN) { m->mptr = p; m->mlen = L; }
        }
    }
    m->H[m->h] = i + 1;
    if (i >= 2) m->R2[buf[i - 2] << 8 | buf[i - 1]] = (uint8_t)c;

    if (c == '\n') {
        int old = m->pp; m->pp = m->prv; m->prv = m->cur; m->cur = old;
        m->nt[m->cur] = 0; m->full = 0; m->d = 0; m->lines++; m->nok = 0; m->fok = 0;
        const Tok *pv = m->T[m->prv], *pq = m->T[m->pp];
        int np = m->nt[m->prv], nq = m->lines >= 2 ? m->nt[m->pp] : 0;
        for (int t = 0; t < np; t++) {
            int al = pv[t].al;
            m->sl[t] = (pv[t].num && al >= 0 && al < nq && pq[al].num)
                ? (uint8_t)extrap(buf + pv[t].start, pv[t].len, buf + pq[al].start, pq[al].len, m->S[t]) : 0;
        }
    } else if (!m->full) {
        Tok *cu = m->T[m->cur]; const Tok *pv = m->T[m->prv];
        int np = m->lines ? m->nt[m->prv] : 0, t = m->nt[m->cur] - 1;
        uint32_t cls = m->ctab[c];
        if (t >= 0 && cls != C_PUNCT && cu[t].cls == cls) {
            int o = cu[t].len, tp = cu[t].al;
            if (cu[t].len < 65535) cu[t].len++;
            cu[t].num &= m->dtab[c];
            m->nok = m->nok && tp >= 0 && o < m->sl[tp] && m->S[tp][o] == c;
            m->fh = L_HASHF(m->fh | (uint32_t)c << 20); m->fq = m->FR[m->fh]; m->FR[m->fh] = i + 1; m->fok = cls == C_ALNUM;
        } else if (t + 1 < MAXT) {
            t++; m->nt[m->cur] = t + 1;
            cu[t].start = i; cu[t].len = 1; cu[t].cls = (uint8_t)cls; cu[t].num = m->dtab[c];
            static const int dk[7] = {0, 1, -1, 2, -2, 3, -3};
            int e = t == 0 ? 0 : (cu[t - 1].al >= 0 ? cu[t - 1].al + 1 : t + m->d), al = -1;
            for (int k = 0; k < 7; k++) {
                int j = e + dk[k];
                if (j >= 0 && j < np && pv[j].cls == cls && (cls != C_PUNCT || buf[pv[j].start] == c)) { al = j; break; }
            }
            cu[t].al = (int16_t)al;
            if (al >= 0) m->d = al - t;
            { uint32_t f = (uint32_t)(al >= 0 ? al : t); m->fh = L_HASHF((f < 63 ? f : 63) << 14 | (uint32_t)c << 20); }
            m->fq = m->FR[m->fh]; m->FR[m->fh] = i + 1; m->fok = cls == C_ALNUM;
            m->nok = al >= 0 && m->sl[al] > 0 && m->S[al][0] == c;
        } else { m->full = 1; m->fok = 0; }
    } else m->fok = 0;
    m->i = i + 1;
}

/* ---------------- container ---------------- */
static void crc_init(void) {
    for (uint32_t v = 0; v < 256; v++) { uint32_t c = v; for (int k = 0; k < 8; k++) c = L_CRCBIT(c); CRC_T[v] = c; }
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = CRC_T[(c ^ *p++) & 255u] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void put32(uint8_t *p, uint32_t v) { for (int k = 0; k < 4; k++) p[k] = (uint8_t)(v >> (8 * k)); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

size_t shz_bound(size_t n) { return n + HDR + 8; }

/* returns the compressed size (always <= shz_bound(n)) */
size_t shz_compress(const uint8_t *in, size_t n, uint8_t *out) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    if (n >= 0xFFFFFFF0u) { fprintf(stderr, "shz: inputs of 4 GB or more are not supported\n"); exit(2); }
    memcpy(out, "SHZ1", 4); put32(out + 4, (uint32_t)n); put32(out + 8, 0); put32(out + 12, crc32(in, n));
    out[16] = 1;
    size_t cap = n + 8;
    if (n) {
        Model *m = model_new(in);
        AC a = {0, 0xFFFFFFFFu, 0, out + HDR, out + HDR + cap};
        for (size_t i = 0; i < n && a.p < a.e; i++) { code_byte(m, &a, in[i], 1); model_update(m, in[i]); }
        for (int k = 0; k < 4; k++) { if (a.p < a.e) *a.p = (uint8_t)(a.x1 >> 24); a.p++; a.x1 <<= 8; }
        model_free(m);
        size_t z = (size_t)(a.p - (out + HDR));
        if (z < n) return HDR + z;
    }
    out[16] = 0; memcpy(out + HDR, in, n);              /* incompressible: stored */
    return HDR + n;
}

/* returns the decompressed size, or (size_t)-1 if the stream is damaged */
size_t shz_decompress(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    if (len < HDR || memcmp(in, "SHZ1", 4)) return (size_t)-1;
    size_t n = get32(in + 4);
    if (n > cap) return (size_t)-1;
    /* every byte costs at least -log2(4095/4096) bits, so a coded stream can never hold more than about 22,700 bytes
       per coded byte: a larger length is a damaged header (refuse it before decoding garbage) */
    if (in[16] != 0 && n > (len - HDR) * 32768u + 64u) return (size_t)-1;
    if (in[16] == 0) { if (len != HDR + n) return (size_t)-1; memcpy(out, in + HDR, n); }
    else {
        Model *m = model_new(out);
        AC a = {0, 0xFFFFFFFFu, 0, (uint8_t *)in + HDR, (uint8_t *)in + len};
        for (int k = 0; k < 4; k++) a.x = (a.x << 8) | (a.p < a.e ? *a.p++ : 0);
        for (size_t i = 0; i < n; i++) { int c = code_byte(m, &a, 0, 0); out[i] = (uint8_t)c; model_update(m, c); }
        model_free(m);
    }
    return crc32(out, n) == get32(in + 12) ? n : (size_t)-1;
}

#ifndef SHZ_NO_MAIN
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); long z = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)z + 1); if (fread(b, 1, (size_t)z, f) != (size_t)z) { perror(path); exit(1); }
    fclose(f); *n = (size_t)z; return b;
}
static void spit(const char *path, const uint8_t *b, size_t n) {
    FILE *f = fopen(path, "wb"); if (!f || fwrite(b, 1, n, f) != n) { perror(path); exit(1); } fclose(f);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(int argc, char **argv) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    if (argc >= 4 && (argv[1][0] == 'c' || argv[1][0] == 'd') && argv[1][1] == 0) {
        size_t n; uint8_t *in = slurp(argv[2], &n);
        if (argv[1][0] == 'c') { uint8_t *o = malloc(shz_bound(n)); spit(argv[3], o, shz_compress(in, n, o)); }
        else {
            size_t cap = n >= 8 ? get32(in + 4) : 0;
            if (n >= 17 && in[16] != 0 && cap > (n - 17) * 32768u + 64u) { fprintf(stderr, "shz: damaged stream\n"); return 1; }
            uint8_t *o = malloc(cap + 1);
            if (!o) { fprintf(stderr, "shz: out of memory\n"); return 1; }
            size_t z = shz_decompress(in, n, o, cap);
            if (z == (size_t)-1) { fprintf(stderr, "shz: damaged stream\n"); return 1; }
            spit(argv[3], o, z);
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "t")) {          /* round trip, sizes and speed for each file */
        size_t tin = 0, tout = 0; double tc = 0, td = 0;
        for (int k = 2; k < argc; k++) {
            size_t n; uint8_t *in = slurp(argv[k], &n), *z = malloc(shz_bound(n)), *back = malloc(n + 1);
            size_t zl = 0; double bc = 1e9, bd = 1e9;
            int reps = n < (1 << 20) ? 5 : 2;
            for (int r = 0; r < reps; r++) { double t0 = now(); zl = shz_compress(in, n, z); double t1 = now(); if (t1 - t0 < bc) bc = t1 - t0; }
            for (int r = 0; r < reps; r++) {
                double t0 = now(); size_t bl = shz_decompress(z, zl, back, n); double t1 = now(); if (t1 - t0 < bd) bd = t1 - t0;
                if (bl != n || memcmp(in, back, n)) { printf("ROUND TRIP FAILED: %s\n", argv[k]); return 1; }
            }
            const char *nm = strrchr(argv[k], '/'); nm = nm ? nm + 1 : argv[k];
            printf("%-34s %10zu -> %9zu  %6.3f bpb  x%6.2f  c %6.1f MB/s  d %6.1f MB/s\n", nm, n, zl, 8.0 * zl / (n ? n : 1),
                   (double)n / zl, n / bc / 1e6, n / bd / 1e6);
            tin += n; tout += zl; tc += bc; td += bd; free(in); free(z); free(back);
        }
#ifdef SHZ_STATS
        { const char *nm[5] = {"flag r0", "flag r1", "flag r2+", "lit (no cand)", "lit (after miss)"}; double tb = 0;
          for (int k = 0; k < 5; k++) tb += ST_bits[k];
          for (int k = 0; k < 5; k++) printf("  %-18s %10ld decisions %10.0f bytes  %5.1f%%\n", nm[k], ST_n[k], ST_bits[k] / 8, 100 * ST_bits[k] / tb);
          printf("  literals %ld (no candidate %ld) of %zu bytes\n", ST_lit, ST_litnoc, tin);
          const char *pn[4] = {"M", "T", "N", "O"};
          for (int k = 0; k < NP; k++) printf("  %s offered %8ld  right %5.1f%%\n", pn[k], ST_pred[k], 100.0 * ST_predok[k] / (ST_pred[k] ? ST_pred[k] : 1)); }
#endif
        printf("%-34s %10zu -> %9zu  %6.3f bpb  x%6.2f  c %6.1f MB/s  d %6.1f MB/s\n", "TOTAL", tin, tout, 8.0 * tout / tin,
               (double)tin / tout, tin / tc / 1e6, tin / td / 1e6);
        return 0;
    }
    fprintf(stderr, "usage: shz c IN OUT | shz d IN OUT | shz t FILE...\n");
    return 2;
}
#endif
