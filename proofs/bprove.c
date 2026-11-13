/* bprove.c -- BDIV100, BDIV10, BMOD10 on all of [0, 255], and the decimal text they render for every byte value. */
#include <stdio.h>
#include <stdint.h>
#define SHZ_LANES_ONLY
#include "../codec/shzb_laws.h"
#include "../codec/shzb_ref.h"
int main(void) {
    long bad = 0;
    for (uint32_t b = 0; b < 256; b++) {
        bad += (uint32_t)lw_BDIV100((int32_t)b) != L_BDIV100(b);
        bad += (uint32_t)lw_BDIV10((int32_t)b) != L_BDIV10(b);
        bad += (uint32_t)lw_BMOD10((int32_t)b) != L_BMOD10(b);
        char want[8], got[8]; int n = 0;
        snprintf(want, sizeof want, "%u", b);
        if (b >= 100) { got[n++] = '0' + lw_BDIV100(b); got[n++] = '0' + lw_BMOD10(lw_BDIV10(b)); got[n++] = '0' + lw_BMOD10(b); }
        else if (b >= 10) { got[n++] = '0' + lw_BDIV10(b); got[n++] = '0' + lw_BMOD10(b); }
        else got[n++] = '0' + b;
        got[n] = 0; for (int k = 0; k <= n; k++) if (got[k] != want[k]) { bad++; break; }
    }
    printf("  BDIV100, BDIV10, BMOD10 on all of [0, 255], and the decimal text of every byte value: %ld wrong\n%s\n", bad, bad ? "FAIL" : "PASS");
    return bad != 0;
}
