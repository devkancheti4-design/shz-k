/* shzb.c -- shza v2 for AI-API transcripts: the float law, the bytes law, field-aware floats.  THE BODY.
   (shza.c, frozen for its sealed test, is the first version: float law only, fields by the key alone.)

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
#include "shzb_ref.h"
#else
#include "shza_laws.h"
#include "shzb_laws.h"
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

typedef struct { uint32_t h[8192]; int id[8192]; int n; } Fields;
/* a field id for a hash: first come, first numbered */
static int fid(Fields *F, uint32_t h) {
    uint32_t s = (h >> 19) & 8191u;
    while (F->id[s] && F->h[s] != h) s = (s + 1) & 8191u;
    if (!F->id[s]) { if (F->n >= 8000) return 0; F->h[s] = h; F->id[s] = ++F->n; }
    return F->id[s] - 1;
}
/* walk the marker text as JSON; give every float marker its field: an object value by its key and its parent list's
   key, a list element by its list's key and its index. Encoder and decoder walk the same text, so they agree. */
typedef struct { uint8_t list; uint32_t key; uint32_t idx; } Frame;
static size_t field_walk(const uint8_t *t, size_t tn, int mf, Fields *F, int *ff, size_t maxf) {
    Frame st[64]; int sp = 0, instr = 0, esc = 0; uint32_t strh = 0, lastkey = 0x9E3779B9u; size_t k = 0;
    for (size_t i = 0; i < tn; i++) {
        uint32_t c = t[i];
        if (instr) {
            if ((int)c == mf && mf) { if (k < maxf) ff[k] = fid(F, L_HM1(lastkey ^ 0x5BD1E995u)); k++; continue; }   /* a number inside a string */
            if (esc) { esc = 0; strh = L_HM1(strh + c); } else if (c == '\\') esc = 1; else if (c == '"') instr = 0; else strh = L_HM1(strh + c);
            continue;
        }
        if (c == '"') { instr = 1; strh = 0x85EBCA6Bu; continue; }
        if (c == ':') { lastkey = strh; continue; }
        if (c == '{' || c == '[') { if (sp < 64) { st[sp].list = c == '['; st[sp].key = lastkey; st[sp].idx = 0; } sp++; continue; }
        if (c == '}' || c == ']') { if (sp) sp--; continue; }
        if (c == ',') { if (sp && sp <= 64 && st[sp - 1].list) st[sp - 1].idx++; continue; }
        if ((int)c == mf && mf) {
            uint32_t h;
            if (sp && sp <= 64 && st[sp - 1].list) h = L_HM1(st[sp - 1].key ^ (st[sp - 1].idx + 1u));
            else h = L_HM1(lastkey + (sp >= 2 && sp <= 64 ? st[sp - 2].key : 0u));
            if (k < maxf) ff[k] = fid(F, h);
            k++;
        }
    }
    return k;
}
/* the bytes law: the JSON string at t (just after its opening quote) as raw bytes (simple escapes; \u gives up) */
static int jstr_bytes(const uint8_t *t, size_t n, uint8_t *out, int cap) {
    int m = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = t[i];
        if (c == '"') return m;
        if (c == '\\') {
            if (++i >= n) return -1;
            switch (t[i]) { case '"': c = '"'; break; case '\\': c = '\\'; break; case '/': c = '/'; break; case 'b': c = 8; break;
                            case 'f': c = 12; break; case 'n': c = 10; break; case 'r': c = 13; break; case 't': c = 9; break; default: return -1; }
        }
        if (m >= cap) return -1;
        out[m++] = c;
    }
    return -1;
}
/* the token string of the nearest '"token": "' before position at (searching back at most 4096 bytes) */
static int token_before(const uint8_t *t, size_t at, uint8_t *out, int cap) {
    static const char pat[] = "\"token\": \"";
    size_t lo = at > 4096 ? at - 4096 : 0;
    for (size_t k = at; k-- > lo; ) {
        if (k + 10 <= at && !memcmp(t + k, pat, 10)) return jstr_bytes(t + k + 10, at - (k + 10), out, cap);
    }
    return -1;
}
/* render a bytes list exactly as Python's json writes it: [72, 101, 108] */
static int render_bytes(const uint8_t *b, int m, char *out) {
    int n = 0; out[n++] = '[';
    for (int k = 0; k < m; k++) {
        uint32_t v = b[k];
        if (k) { out[n++] = ','; out[n++] = ' '; }
        if (v >= 100) { out[n++] = (char)('0' + L_BDIV100(v)); out[n++] = (char)('0' + L_BMOD10(L_BDIV10(v))); out[n++] = (char)('0' + L_BMOD10(v)); }
        else if (v >= 10) { out[n++] = (char)('0' + L_BDIV10(v)); out[n++] = (char)('0' + L_BMOD10(v)); }
        else out[n++] = (char)('0' + v);
    }
    out[n++] = ']'; return n;
}

/* ---- the float stream coder: the interleaved stream through shz, or 4 byte planes (sign+exponent byte first), each
   stored raw or coded by bsc or shz, whichever is smallest. The choice is written into the stream. ---- */
static size_t bsc_pack(const uint8_t *in, size_t n, uint8_t *out, int coder) {
    int z = bsc_compress(in, out, (int)n, LIBBSC_DEFAULT_LZPHASHSIZE, LIBBSC_DEFAULT_LZPMINLEN, LIBBSC_BLOCKSORTER_BWT, coder, LIBBSC_FEATURE_FASTMODE);
    return z < 0 ? (size_t)-1 : (size_t)z;
}
static double h0(const uint8_t *p, size_t n) {
    size_t c[256] = {0}; for (size_t i = 0; i < n; i++) c[p[i]]++;
    double h = 0; for (int k = 0; k < 256; k++) if (c[k]) { double q = (double)c[k] / n; h -= q * __builtin_log2(q); }
    return h;
}
static size_t planes_pack(const uint8_t *fs, size_t nb, uint8_t *b, uint8_t *pl, uint8_t *t1, uint8_t *t2, int try_shz) {
    size_t nq = nb / 4, zb = 1; b[0] = 3;
    for (int k = 0; k < 4; k++) {
        for (size_t i = 0; i < nq; i++) pl[i] = fs[4 * i + (size_t)k];
        int kind = 0; size_t z = nq; const uint8_t *src = pl;
        if (h0(pl, nq) < 7.95) {
            size_t z2 = bsc_pack(pl, nq, t2, LIBBSC_CODER_QLFC_ADAPTIVE);
            if (z2 != (size_t)-1 && z2 < z) { kind = 1; z = z2; src = t2; }
            if (try_shz) { size_t z1 = shz_compress(pl, nq, t1); if (z1 < z) { kind = 2; z = z1; src = t1; } }
        }
        b[zb] = (uint8_t)kind; put32(b + zb + 1, (uint32_t)z); memcpy(b + zb + 5, src, z); zb += 5 + z;
    }
    return zb;
}
static size_t floats_pack(const uint8_t *fs, size_t nb, uint8_t *out, int fmode) {
    if (!nb) return 0;
    uint8_t *a = malloc(shz_bound(nb) + 64), *b = malloc(shz_bound(nb) + 64), *pl = malloc(nb / 4 + 16),
            *t1 = malloc(shz_bound(nb / 4) + 64), *t2 = malloc(nb / 4 + LIBBSC_HEADER_SIZE + 64);
    int inter;                                  /* decide on a 64 KB sample, then code the whole stream once */
    size_t sn = nb > 65536 ? 65536 : nb;
    { size_t zi = shz_compress(fs, sn, a + 1), zp = planes_pack(fs, sn, b, pl, t1, t2, 0); inter = zi + 1 <= zp; }
    size_t r;
    if (inter) { size_t za = fmode == 2 ? shzx_compress(fs, nb, a + 1) : shz_compress(fs, nb, a + 1); a[0] = 1; memcpy(out, a, za + 1); r = za + 1; }
    else { size_t zb = planes_pack(fs, nb, b, pl, t1, t2, 0); memcpy(out, b, zb); r = zb; }
    free(a); free(b); free(pl); free(t1); free(t2);
    return r;
}
static int floats_unpack(const uint8_t *in, size_t len, uint8_t *fs, size_t nb) {
    if (!nb) return 0;
    if (len < 1) return -1;
    if (in[0] == 1) return shzx_decompress(in + 1, len - 1, fs, nb) == nb ? 0 : -1;
    if (in[0] != 3) return -1;
    size_t nq = nb / 4, pos = 1; uint8_t *pl = malloc(nq + 16);
    for (int k = 0; k < 4; k++) {
        if (pos + 5 > len) { free(pl); return -1; }
        int kind = in[pos]; size_t z = get32(in + pos + 1); pos += 5;
        if (pos + z > len) { free(pl); return -1; }
        if (kind == 0) { if (z != nq) { free(pl); return -1; } memcpy(pl, in + pos, nq); }
        else if (kind == 2) { if (shzx_decompress(in + pos, z, pl, nq) != nq) { free(pl); return -1; } }
        else { int bs = 0, ds = 0;
               if (bsc_block_info(in + pos, LIBBSC_HEADER_SIZE, &bs, &ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR || (size_t)ds != nq ||
                   bsc_decompress(in + pos, bs, pl, ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR) { free(pl); return -1; } }
        for (size_t i = 0; i < nq; i++) fs[4 * i + (size_t)k] = pl[i];
        pos += z;
    }
    free(pl); return 0;
}

enum { AHDR = 4 + 4 + 4 + 1 + 1 + 4 + 4 + 4 + 1 };
size_t shzb_bound(size_t n) { return n + n / 8 + 4096; }

/* fmode: 1 = floats with shz fast, 2 = with shz max;  coder: LIBBSC_CODER_QLFC_STATIC or _ADAPTIVE */
size_t shzb_compress(const uint8_t *in, size_t n, uint8_t *out, int fmode, int coder) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    static int bsc_ready; if (!bsc_ready) { bsc_init(LIBBSC_FEATURE_FASTMODE); bsc_ready = 1; }
    int used[256] = {0}; for (size_t i = 0; i < n; i++) used[in[i]] = 1;
    int marker = -1, mb = -1; for (int c = 1; c < 32; c++) if (!used[c]) { if (marker < 0) marker = c; else { mb = c; break; } }
    uint8_t *txt = malloc(n + 1); uint32_t *fv = malloc((n / 2 + 16) * sizeof(uint32_t)); int *ff = malloc((n / 2 + 16) * sizeof(int));
    Fields *F = calloc(1, sizeof(Fields));
    size_t tn = 0, nf = 0; char rb[48]; uint8_t tb[1024]; char lb[4200];
    for (size_t i = 0; i < n; ) {
        int c = in[i];
        if (mb > 0 && c == '[' && i >= 9 && !memcmp(in + i - 9, "\"bytes\": ", 9)) {      /* the bytes law */
            size_t j = i + 1; int m = 0, okf = 1; uint8_t bv[1024];
            while (j < n && in[j] != ']') {
                if (m && (j + 1 >= n || in[j] != ',' || in[j + 1] != ' ')) { okf = 0; break; }
                if (m) j += 2;
                size_t q = j; uint32_t v = 0;
                while (j < n && in[j] >= '0' && in[j] <= '9' && j - q < 4) { v = v * 10 + (uint32_t)(in[j] - '0'); j++; }
                if (j == q || v > 255 || m >= 1024 || (in[q] == '0' && j - q > 1)) { okf = 0; break; }
                bv[m++] = (uint8_t)v;
            }
            if (okf && j < n && in[j] == ']') {
                int tl = token_before(in, i - 9, tb, (int)sizeof tb);
                if (tl == m && !memcmp(tb, bv, (size_t)m) && render_bytes(bv, m, lb) == (int)(j + 1 - i) && !memcmp(lb, in + i, j + 1 - i)) {
                    txt[tn++] = (uint8_t)mb; i = j + 1; continue;
                }
            }
        }
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
                    fv[nf] = u; nf++;
                    txt[tn++] = (uint8_t)marker; i = j; continue;
                }
            }
            memcpy(txt + tn, in + i, len ? len : 1); tn += len ? len : 1; i += len ? len : 1; continue;
        }
        txt[tn++] = (uint8_t)c; i++;
    }
    /* the float stream: field by field (first appearance order), each value's ordered bits, big-endian */
    if (field_walk(txt, tn, marker, F, ff, nf) != nf) { fprintf(stderr, "shzb: field walk disagrees\n"); exit(2); }
    int nfld = F->n; size_t *cnt = calloc((size_t)nfld + 1, sizeof(size_t)), *at = calloc((size_t)nfld + 1, sizeof(size_t));
    for (size_t k = 0; k < nf; k++) cnt[ff[k]]++;
    for (int g = 1; g <= nfld; g++) at[g] = at[g - 1] + cnt[g - 1];
    uint8_t *fs = malloc(nf * 4 + 16);
    for (size_t k = 0; k < nf; k++) {
        uint32_t o = L_ORD32(fv[k]); size_t p = 4 * at[ff[k]]++;
        fs[p] = (uint8_t)(o >> 24); fs[p + 1] = (uint8_t)(o >> 16); fs[p + 2] = (uint8_t)(o >> 8); fs[p + 3] = (uint8_t)o;
    }
    memcpy(out, "SHZB", 4); put32(out + 4, (uint32_t)n); put32(out + 8, crc32(in, n));
    out[12] = (uint8_t)(marker < 0 ? 0 : marker); out[13] = (uint8_t)fmode; out[26] = (uint8_t)(mb < 0 ? 0 : mb);
    size_t pos = AHDR;
    int tz = tn ? bsc_compress(txt, out + pos, (int)tn, LIBBSC_DEFAULT_LZPHASHSIZE, LIBBSC_DEFAULT_LZPMINLEN, LIBBSC_BLOCKSORTER_BWT, coder, LIBBSC_FEATURE_FASTMODE) : 0;
    if (tz < 0) { fprintf(stderr, "shza: bsc failed\n"); exit(2); }
    pos += (size_t)tz;
    size_t fz = floats_pack(fs, nf * 4, out + pos, fmode);
    pos += fz;
    put32(out + 14, (uint32_t)tn); put32(out + 18, (uint32_t)tz); put32(out + 22, (uint32_t)nf);
    if (getenv("SHZB_DEBUG")) fprintf(stderr, "shzb: text %zu -> %d, floats %zu (%d fields) -> %zu, mode %d\n", tn, tz, nf, nfld, fz, fz ? out[pos - fz] : -1);
    free(txt); free(fv); free(ff); free(F); free(cnt); free(at); free(fs);
    return pos;
}

size_t shzb_decompress(const uint8_t *in, size_t len, uint8_t *out, size_t cap) {
    if (!CRC_T[1]) { crc_init(); tables_init(); }
    static int bsc_ready; if (!bsc_ready) { bsc_init(LIBBSC_FEATURE_FASTMODE); bsc_ready = 1; }
    if (len < AHDR || memcmp(in, "SHZB", 4)) return (size_t)-1;
    size_t n = get32(in + 4), tn = get32(in + 14), tz = get32(in + 18), nf = get32(in + 22);
    int marker = in[12], mb = in[26];
    if (n > cap || AHDR + tz > len) return (size_t)-1;
    uint8_t *txt = malloc(tn + 1);
    if (tn) {
        int bs = 0, ds = 0;
        if (bsc_block_info(in + AHDR, LIBBSC_HEADER_SIZE, &bs, &ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR || (size_t)ds != tn ||
            bsc_decompress(in + AHDR, bs, txt, ds, LIBBSC_FEATURE_FASTMODE) != LIBBSC_NO_ERROR) { free(txt); return (size_t)-1; }
    }
    uint8_t *fs = malloc(nf * 4 + 16);
    if (floats_unpack(in + AHDR + tz, len - AHDR - tz, fs, nf * 4) != 0) { free(txt); free(fs); return (size_t)-1; }
    /* fields from the text, in order; then each field's floats in order */
    Fields *F = calloc(1, sizeof(Fields)); int *ff = malloc((nf + 1) * sizeof(int));
    if (field_walk(txt, tn, marker, F, ff, nf) != nf) { free(txt); free(fs); free(F); free(ff); return (size_t)-1; }
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
        } else if (txt[i] == mb && mb) {
            uint8_t tb[1024]; char lb[4200];
            if (o < 9 || memcmp(out + o - 9, "\"bytes\": ", 9)) { o = cap + 1; break; }
            int tl = token_before(out, o - 9, tb, (int)sizeof tb);
            if (tl < 0) { o = cap + 1; break; }
            int l = render_bytes(tb, tl, lb);
            if (o + (size_t)l > cap) { o = cap + 1; break; }
            memcpy(out + o, lb, (size_t)l); o += (size_t)l;
        } else { if (o >= cap) { o = cap + 1; break; } out[o++] = txt[i]; }
    }
    free(txt); free(fs); free(F); free(ff); free(cnt); free(at);
    if (o != n) return (size_t)-1;
    return crc32(out, n) == get32(in + 8) ? n : (size_t)-1;
}

#ifndef SHZB_NO_MAIN
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
        const char *nm[4] = {"shzb(e1,fast)", "shzb(e2,fast)", "shzb(e1,max)", "shzb(e2,max)"};
        int fm[4] = {1, 1, 2, 2}, co[4] = {LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE, LIBBSC_CODER_QLFC_STATIC, LIBBSC_CODER_QLFC_ADAPTIVE};
        for (int a = 2; a < argc; a++) {
            size_t n; uint8_t *in = aslurp(argv[a], &n), *z = malloc(shzb_bound(n)), *back = malloc(n + 1);
            const char *fn = strrchr(argv[a], '/'); fn = fn ? fn + 1 : argv[a];
            for (int s = 0; s < 4; s++) {
                double t0 = anow(); size_t zl = shzb_compress(in, n, z, fm[s], co[s]); double t1 = anow();
                size_t bl = shzb_decompress(z, zl, back, n); double t2 = anow();
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
        if (argv[1][0] == 'c') { uint8_t *o = malloc(shzb_bound(n)); aspit(argv[3], o, shzb_compress(in, n, o, 1, LIBBSC_CODER_QLFC_ADAPTIVE)); }
        else {
            size_t cap = n >= 8 ? get32(in + 4) : 0; uint8_t *o = malloc(cap + 1);
            size_t z = shzb_decompress(in, n, o, cap);
            if (z == (size_t)-1) { fprintf(stderr, "shzb: damaged stream\n"); return 1; }
            aspit(argv[3], o, z);
        }
        return 0;
    }
    fprintf(stderr, "usage: shzb c IN OUT | shzb d IN OUT | shzb t FILE...\n");
    return 2;
}
#endif
