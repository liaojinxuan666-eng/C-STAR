#include "cstar_simd128.h"

#include <stddef.h>

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#define CSTAR_HAS_NEON 1
#else
#define CSTAR_HAS_NEON 0
#endif

void cstar_simd128_u32_add(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, vaddq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = a[i] + b[i];
#endif
}

void cstar_simd128_u32_sub(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, vsubq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = a[i] - b[i];
#endif
}

void cstar_simd128_u32_xor(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, veorq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = a[i] ^ b[i];
#endif
}

void cstar_simd128_u32_and(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, vandq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = a[i] & b[i];
#endif
}

void cstar_simd128_u32_or(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, vorrq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = a[i] | b[i];
#endif
}

void cstar_simd128_u32_cmpeq(uint32_t *a, uint32_t *b, uint32_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u32(out, vceqq_u32(vld1q_u32(a), vld1q_u32(b)));
#else
    for (size_t i = 0; i < 4; ++i) out[i] = (a[i] == b[i]) ? UINT32_MAX : UINT32_C(0);
#endif
}

void cstar_simd128_u16_add(uint16_t *a, uint16_t *b, uint16_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u16(out, vaddq_u16(vld1q_u16(a), vld1q_u16(b)));
#else
    for (size_t i = 0; i < 8; ++i) out[i] = (uint16_t)(a[i] + b[i]);
#endif
}

void cstar_simd128_u8_add(uint8_t *a, uint8_t *b, uint8_t *out) {
#if CSTAR_HAS_NEON
    vst1q_u8(out, vaddq_u8(vld1q_u8(a), vld1q_u8(b)));
#else
    for (size_t i = 0; i < 16; ++i) out[i] = (uint8_t)(a[i] + b[i]);
#endif
}
