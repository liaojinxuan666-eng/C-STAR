#ifndef CSTAR_SIMD128_H
#define CSTAR_SIMD128_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 128-bit integer SIMD bridge. Buffers must point to 16 bytes of valid data.
 * The API deliberately uses ordinary pointers so it is callable through C*'s
 * current C ABI without introducing a hidden runtime or a vector ABI. */
void cstar_simd128_u32_add(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u32_sub(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u32_xor(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u32_and(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u32_or(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u32_cmpeq(uint32_t *a, uint32_t *b, uint32_t *out);
void cstar_simd128_u16_add(uint16_t *a, uint16_t *b, uint16_t *out);
void cstar_simd128_u8_add(uint8_t *a, uint8_t *b, uint8_t *out);

#ifdef __cplusplus
}
#endif
#endif
