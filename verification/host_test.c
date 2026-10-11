#include "cstar_simd128.h"
#include <stdio.h>

int main(void) {
    uint32_t a[4] = {1, 2, 3, 4};
    uint32_t b[4] = {10, 20, 30, 40};
    uint32_t out[4] = {0};
    uint32_t c[4] = {1, 9, 3, 0};
    uint32_t mask[4] = {0};
    uint16_t ha[8] = {0}, hb[8] = {0}, ho[8] = {0};
    uint8_t ba[16] = {0}, bb[16] = {0}, bo[16] = {0};

    cstar_simd128_u32_add(a, b, out);
    if (out[0] != 11 || out[1] != 22 || out[2] != 33 || out[3] != 44) return 1;
    cstar_simd128_u32_xor(a, b, out);
    if (out[0] != 11 || out[1] != 22 || out[2] != 29 || out[3] != 44) return 2;
    cstar_simd128_u32_sub(b, a, out);
    if (out[0] != 9 || out[1] != 18 || out[2] != 27 || out[3] != 36) return 3;
    cstar_simd128_u32_cmpeq(a, c, mask);
    if (mask[0] == 0 || mask[1] != 0 || mask[2] == 0 || mask[3] != 0) return 4;
    ha[7] = 8; hb[7] = 9;
    cstar_simd128_u16_add(ha, hb, ho);
    if (ho[7] != 17) return 5;
    ba[15] = 250; bb[15] = 10;
    cstar_simd128_u8_add(ba, bb, bo);
    if (bo[15] != 4) return 6;
    puts("simd128 C bridge: ok");
    return 0;
}
