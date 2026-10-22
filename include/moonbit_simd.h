/*
 * Copyright 2026 International Digital Economy Academy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef moonbit_simd_h_INCLUDED
#define moonbit_simd_h_INCLUDED

#include "moonbit.h"
#include "moonbit_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Hardware fast paths for the wasm simd instructions used by the     */
/* v128 intrinsics. Each definition is followed by a self-referential */
/* marker macro; for ops without a fast path on the target ISA the    */
/* compiler binds the Moonbit_simd_* name to the MoonBit fallback     */
/* implementation compiled into the same translation unit.            */
/* Lane semantics follow wasm (little-endian).                        */
/* ------------------------------------------------------------------ */

#if defined(MOONBIT_V128_NEON) && defined(__aarch64__)

/* ---- v128 bitwise ---- */
#define Moonbit_simd_v128_not(a) (vmvnq_u8((a)))
#define Moonbit_simd_v128_and(a, b) (vandq_u8((a), (b)))
#define Moonbit_simd_v128_andnot(a, b) (vbicq_u8((a), (b)))
#define Moonbit_simd_v128_or(a, b) (vorrq_u8((a), (b)))
#define Moonbit_simd_v128_xor(a, b) (veorq_u8((a), (b)))
#define Moonbit_simd_v128_bitselect(a, b, m) (vbslq_u8((m), (a), (b)))
#define Moonbit_simd_v128_any_true(a) (vmaxvq_u8((a)) != 0)

/* ---- splat ---- */
#define Moonbit_simd_i8x16_splat(x) (vdupq_n_u8((uint8_t)(x)))
#define Moonbit_simd_i16x8_splat(x) (vreinterpretq_u8_u16(vdupq_n_u16((uint16_t)(x))))
#define Moonbit_simd_i32x4_splat(x) (vreinterpretq_u8_u32(vdupq_n_u32((x))))
#define Moonbit_simd_i64x2_splat(x) (vreinterpretq_u8_u64(vdupq_n_u64((x))))
#define Moonbit_simd_f32x4_splat(x) (vreinterpretq_u8_f32(vdupq_n_f32((x))))
#define Moonbit_simd_f64x2_splat(x) (vreinterpretq_u8_f64(vdupq_n_f64((x))))

/* ---- swizzle and shuffle ---- */
#define Moonbit_simd_i8x16_swizzle(a, s) (vqtbl1q_u8((a), (s)))
static inline moonbit_v128_t moonbit_simd_i8x16_shuffle(
    moonbit_v128_t a, moonbit_v128_t b, int l0, int l1, int l2, int l3, int l4,
    int l5, int l6, int l7, int l8, int l9, int l10, int l11, int l12, int l13,
    int l14, int l15) {
  uint8_t idx[16];
  uint8x16x2_t t;
  idx[0] = (uint8_t)l0; idx[1] = (uint8_t)l1; idx[2] = (uint8_t)l2;
  idx[3] = (uint8_t)l3; idx[4] = (uint8_t)l4; idx[5] = (uint8_t)l5;
  idx[6] = (uint8_t)l6; idx[7] = (uint8_t)l7; idx[8] = (uint8_t)l8;
  idx[9] = (uint8_t)l9; idx[10] = (uint8_t)l10; idx[11] = (uint8_t)l11;
  idx[12] = (uint8_t)l12; idx[13] = (uint8_t)l13; idx[14] = (uint8_t)l14;
  idx[15] = (uint8_t)l15;
  t.val[0] = a;
  t.val[1] = b;
  return vqtbl2q_u8(t, vld1q_u8(idx));
}
#define Moonbit_simd_i8x16_shuffle(a, b, l0, l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15) moonbit_simd_i8x16_shuffle((a), (b), (l0), (l1), (l2), (l3), (l4), (l5), (l6), (l7), (l8), (l9), (l10), (l11), (l12), (l13), (l14), (l15))

/* ---- i8x16 ---- */
#define Moonbit_simd_i8x16_eq(a, b) (vceqq_u8((a), (b)))
#define Moonbit_simd_i8x16_ne(a, b) (vmvnq_u8(vceqq_u8((a), (b))))
#define Moonbit_simd_i8x16_lt_s(a, b) (vcltq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b))))
#define Moonbit_simd_i8x16_lt_u(a, b) (vcltq_u8((a), (b)))
#define Moonbit_simd_i8x16_gt_s(a, b) (vcgtq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b))))
#define Moonbit_simd_i8x16_gt_u(a, b) (vcgtq_u8((a), (b)))
#define Moonbit_simd_i8x16_le_s(a, b) (vcleq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b))))
#define Moonbit_simd_i8x16_le_u(a, b) (vcleq_u8((a), (b)))
#define Moonbit_simd_i8x16_ge_s(a, b) (vcgeq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b))))
#define Moonbit_simd_i8x16_ge_u(a, b) (vcgeq_u8((a), (b)))
#define Moonbit_simd_i8x16_abs(a) (vreinterpretq_u8_s8(vabsq_s8(vreinterpretq_s8_u8((a)))))
#define Moonbit_simd_i8x16_neg(a) (vreinterpretq_u8_s8(vnegq_s8(vreinterpretq_s8_u8((a)))))
#define Moonbit_simd_i8x16_popcnt(a) (vcntq_u8((a)))
#define Moonbit_simd_i8x16_all_true(a) (vminvq_u8((a)) != 0)
#define Moonbit_simd_i8x16_narrow_i16x8_s(a, b) (vreinterpretq_u8_s8(vcombine_s8(vqmovn_s16(vreinterpretq_s16_u8((a))), vqmovn_s16(vreinterpretq_s16_u8((b))))))
#define Moonbit_simd_i8x16_narrow_i16x8_u(a, b) (vcombine_u8(vqmovun_s16(vreinterpretq_s16_u8((a))), vqmovun_s16(vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i8x16_shl(a, n) (vshlq_u8((a), vdupq_n_s8((int8_t)((n) & 7))))
#define Moonbit_simd_i8x16_shr_s(a, n) (vreinterpretq_u8_s8( vshlq_s8(vreinterpretq_s8_u8((a)), vdupq_n_s8((int8_t)-((n) & 7)))))
#define Moonbit_simd_i8x16_shr_u(a, n) (vshlq_u8((a), vdupq_n_s8((int8_t)-((n) & 7))))
#define Moonbit_simd_i8x16_add(a, b) (vaddq_u8((a), (b)))
#define Moonbit_simd_i8x16_sub(a, b) (vsubq_u8((a), (b)))
#define Moonbit_simd_i8x16_add_sat_s(a, b) (vreinterpretq_u8_s8( vqaddq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b)))))
#define Moonbit_simd_i8x16_add_sat_u(a, b) (vqaddq_u8((a), (b)))
#define Moonbit_simd_i8x16_sub_sat_s(a, b) (vreinterpretq_u8_s8( vqsubq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b)))))
#define Moonbit_simd_i8x16_sub_sat_u(a, b) (vqsubq_u8((a), (b)))
#define Moonbit_simd_i8x16_min_s(a, b) (vreinterpretq_u8_s8( vminq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b)))))
#define Moonbit_simd_i8x16_min_u(a, b) (vminq_u8((a), (b)))
#define Moonbit_simd_i8x16_max_s(a, b) (vreinterpretq_u8_s8( vmaxq_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b)))))
#define Moonbit_simd_i8x16_max_u(a, b) (vmaxq_u8((a), (b)))
#define Moonbit_simd_i8x16_avgr_u(a, b) (vrhaddq_u8((a), (b)))

/* ---- i16x8 ---- */
#define Moonbit_simd_i16x8_eq(a, b) (vreinterpretq_u8_u16( vceqq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_ne(a, b) (vmvnq_u8(Moonbit_simd_i16x8_eq((a), (b))))
#define Moonbit_simd_i16x8_lt_s(a, b) (vreinterpretq_u8_u16( vcltq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_lt_u(a, b) (vreinterpretq_u8_u16( vcltq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_gt_s(a, b) (vreinterpretq_u8_u16( vcgtq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_gt_u(a, b) (vreinterpretq_u8_u16( vcgtq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_le_s(a, b) (vreinterpretq_u8_u16( vcleq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_le_u(a, b) (vreinterpretq_u8_u16( vcleq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_ge_s(a, b) (vreinterpretq_u8_u16( vcgeq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_ge_u(a, b) (vreinterpretq_u8_u16( vcgeq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_abs(a) (vreinterpretq_u8_s16(vabsq_s16(vreinterpretq_s16_u8((a)))))
#define Moonbit_simd_i16x8_neg(a) (vreinterpretq_u8_s16(vnegq_s16(vreinterpretq_s16_u8((a)))))
#define Moonbit_simd_i16x8_q15mulr_sat_s(a, b) (vreinterpretq_u8_s16( vqrdmulhq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_all_true(a) (vminvq_u16(vreinterpretq_u16_u8((a))) != 0)
#define Moonbit_simd_i16x8_narrow_i32x4_s(a, b) (vreinterpretq_u8_s16(vcombine_s16(vqmovn_s32(vreinterpretq_s32_u8((a))), vqmovn_s32(vreinterpretq_s32_u8((b))))))
#define Moonbit_simd_i16x8_narrow_i32x4_u(a, b) (vreinterpretq_u8_u16(vcombine_u16(vqmovun_s32(vreinterpretq_s32_u8((a))), vqmovun_s32(vreinterpretq_s32_u8((b))))))
#define Moonbit_simd_i16x8_extend_low_i8x16_s(a) (vreinterpretq_u8_s16(vmovl_s8(vget_low_s8(vreinterpretq_s8_u8((a))))))
#define Moonbit_simd_i16x8_extend_high_i8x16_s(a) (vreinterpretq_u8_s16(vmovl_high_s8(vreinterpretq_s8_u8((a)))))
#define Moonbit_simd_i16x8_extend_low_i8x16_u(a) (vreinterpretq_u8_u16(vmovl_u8(vget_low_u8((a)))))
#define Moonbit_simd_i16x8_extend_high_i8x16_u(a) (vreinterpretq_u8_u16(vmovl_high_u8((a))))
#define Moonbit_simd_i16x8_shl(a, n) (vreinterpretq_u8_u16( vshlq_u16(vreinterpretq_u16_u8((a)), vdupq_n_s16((int16_t)((n) & 15)))))
#define Moonbit_simd_i16x8_shr_s(a, n) (vreinterpretq_u8_s16( vshlq_s16(vreinterpretq_s16_u8((a)), vdupq_n_s16((int16_t)-((n) & 15)))))
#define Moonbit_simd_i16x8_shr_u(a, n) (vreinterpretq_u8_u16( vshlq_u16(vreinterpretq_u16_u8((a)), vdupq_n_s16((int16_t)-((n) & 15)))))
#define Moonbit_simd_i16x8_add(a, b) (vreinterpretq_u8_u16( vaddq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_sub(a, b) (vreinterpretq_u8_u16( vsubq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_mul(a, b) (vreinterpretq_u8_u16( vmulq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_add_sat_s(a, b) (vreinterpretq_u8_s16( vqaddq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_add_sat_u(a, b) (vreinterpretq_u8_u16( vqaddq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_sub_sat_s(a, b) (vreinterpretq_u8_s16( vqsubq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_sub_sat_u(a, b) (vreinterpretq_u8_u16( vqsubq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_min_s(a, b) (vreinterpretq_u8_s16( vminq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_min_u(a, b) (vreinterpretq_u8_u16( vminq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_max_s(a, b) (vreinterpretq_u8_s16( vmaxq_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i16x8_max_u(a, b) (vreinterpretq_u8_u16( vmaxq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_avgr_u(a, b) (vreinterpretq_u8_u16( vrhaddq_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i16x8_extmul_low_i8x16_s(a, b) (vreinterpretq_u8_s16(vmull_s8(vget_low_s8(vreinterpretq_s8_u8((a))), vget_low_s8(vreinterpretq_s8_u8((b))))))
#define Moonbit_simd_i16x8_extmul_high_i8x16_s(a, b) (vreinterpretq_u8_s16( vmull_high_s8(vreinterpretq_s8_u8((a)), vreinterpretq_s8_u8((b)))))
#define Moonbit_simd_i16x8_extmul_low_i8x16_u(a, b) (vreinterpretq_u8_u16(vmull_u8(vget_low_u8((a)), vget_low_u8((b)))))
#define Moonbit_simd_i16x8_extmul_high_i8x16_u(a, b) (vreinterpretq_u8_u16(vmull_high_u8((a), (b))))
#define Moonbit_simd_i16x8_extadd_pairwise_i8x16_s(a) (vreinterpretq_u8_s16(vpaddlq_s8(vreinterpretq_s8_u8((a)))))
#define Moonbit_simd_i16x8_extadd_pairwise_i8x16_u(a) (vreinterpretq_u8_u16(vpaddlq_u8((a))))
static inline moonbit_v128_t moonbit_simd_i16x8_relaxed_dot_i8x16_i7x16_s(
    moonbit_v128_t a, moonbit_v128_t b) {
  int16x8_t lo = vmull_s8(vget_low_s8(vreinterpretq_s8_u8(a)),
                          vget_low_s8(vreinterpretq_s8_u8(b)));
  int16x8_t hi = vmull_high_s8(vreinterpretq_s8_u8(a), vreinterpretq_s8_u8(b));
  return vreinterpretq_u8_s16(vpaddq_s16(lo, hi));
}
#define Moonbit_simd_i16x8_relaxed_dot_i8x16_i7x16_s(a, b) moonbit_simd_i16x8_relaxed_dot_i8x16_i7x16_s((a), (b))

/* ---- i32x4 ---- */
#define Moonbit_simd_i32x4_eq(a, b) (vreinterpretq_u8_u32( vceqq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_ne(a, b) (vmvnq_u8(Moonbit_simd_i32x4_eq((a), (b))))
#define Moonbit_simd_i32x4_lt_s(a, b) (vreinterpretq_u8_u32( vcltq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_lt_u(a, b) (vreinterpretq_u8_u32( vcltq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_gt_s(a, b) (vreinterpretq_u8_u32( vcgtq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_gt_u(a, b) (vreinterpretq_u8_u32( vcgtq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_le_s(a, b) (vreinterpretq_u8_u32( vcleq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_le_u(a, b) (vreinterpretq_u8_u32( vcleq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_ge_s(a, b) (vreinterpretq_u8_u32( vcgeq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_ge_u(a, b) (vreinterpretq_u8_u32( vcgeq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_abs(a) (vreinterpretq_u8_s32(vabsq_s32(vreinterpretq_s32_u8((a)))))
#define Moonbit_simd_i32x4_neg(a) (vreinterpretq_u8_s32(vnegq_s32(vreinterpretq_s32_u8((a)))))
#define Moonbit_simd_i32x4_all_true(a) (vminvq_u32(vreinterpretq_u32_u8((a))) != 0)
#define Moonbit_simd_i32x4_extend_low_i16x8_s(a) (vreinterpretq_u8_s32(vmovl_s16(vget_low_s16(vreinterpretq_s16_u8((a))))))
#define Moonbit_simd_i32x4_extend_high_i16x8_s(a) (vreinterpretq_u8_s32(vmovl_high_s16(vreinterpretq_s16_u8((a)))))
#define Moonbit_simd_i32x4_extend_low_i16x8_u(a) (vreinterpretq_u8_u32(vmovl_u16(vget_low_u16(vreinterpretq_u16_u8((a))))))
#define Moonbit_simd_i32x4_extend_high_i16x8_u(a) (vreinterpretq_u8_u32(vmovl_high_u16(vreinterpretq_u16_u8((a)))))
#define Moonbit_simd_i32x4_shl(a, n) (vreinterpretq_u8_u32( vshlq_u32(vreinterpretq_u32_u8((a)), vdupq_n_s32((n) & 31))))
#define Moonbit_simd_i32x4_shr_s(a, n) (vreinterpretq_u8_s32( vshlq_s32(vreinterpretq_s32_u8((a)), vdupq_n_s32(-((n) & 31)))))
#define Moonbit_simd_i32x4_shr_u(a, n) (vreinterpretq_u8_u32( vshlq_u32(vreinterpretq_u32_u8((a)), vdupq_n_s32(-((n) & 31)))))
#define Moonbit_simd_i32x4_add(a, b) (vreinterpretq_u8_u32( vaddq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_sub(a, b) (vreinterpretq_u8_u32( vsubq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_mul(a, b) (vreinterpretq_u8_u32( vmulq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_min_s(a, b) (vreinterpretq_u8_s32( vminq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_min_u(a, b) (vreinterpretq_u8_u32( vminq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
#define Moonbit_simd_i32x4_max_s(a, b) (vreinterpretq_u8_s32( vmaxq_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i32x4_max_u(a, b) (vreinterpretq_u8_u32( vmaxq_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))
static inline moonbit_v128_t moonbit_simd_i32x4_dot_i16x8_s(moonbit_v128_t a,
                                                            moonbit_v128_t b) {
  int32x4_t lo = vmull_s16(vget_low_s16(vreinterpretq_s16_u8(a)),
                           vget_low_s16(vreinterpretq_s16_u8(b)));
  int32x4_t hi = vmull_high_s16(vreinterpretq_s16_u8(a), vreinterpretq_s16_u8(b));
  return vreinterpretq_u8_s32(vpaddq_s32(lo, hi));
}
#define Moonbit_simd_i32x4_dot_i16x8_s(a, b) moonbit_simd_i32x4_dot_i16x8_s((a), (b))
#define Moonbit_simd_i32x4_extmul_low_i16x8_s(a, b) (vreinterpretq_u8_s32(vmull_s16(vget_low_s16(vreinterpretq_s16_u8((a))), vget_low_s16(vreinterpretq_s16_u8((b))))))
#define Moonbit_simd_i32x4_extmul_high_i16x8_s(a, b) (vreinterpretq_u8_s32( vmull_high_s16(vreinterpretq_s16_u8((a)), vreinterpretq_s16_u8((b)))))
#define Moonbit_simd_i32x4_extmul_low_i16x8_u(a, b) (vreinterpretq_u8_u32(vmull_u16(vget_low_u16(vreinterpretq_u16_u8((a))), vget_low_u16(vreinterpretq_u16_u8((b))))))
#define Moonbit_simd_i32x4_extmul_high_i16x8_u(a, b) (vreinterpretq_u8_u32( vmull_high_u16(vreinterpretq_u16_u8((a)), vreinterpretq_u16_u8((b)))))
#define Moonbit_simd_i32x4_extadd_pairwise_i16x8_s(a) (vreinterpretq_u8_s32(vpaddlq_s16(vreinterpretq_s16_u8((a)))))
#define Moonbit_simd_i32x4_extadd_pairwise_i16x8_u(a) (vreinterpretq_u8_u32(vpaddlq_u16(vreinterpretq_u16_u8((a)))))
static inline moonbit_v128_t moonbit_simd_i32x4_relaxed_dot_i8x16_i7x16_add_s(
    moonbit_v128_t a, moonbit_v128_t b, moonbit_v128_t acc) {
  int8x16_t as = vreinterpretq_s8_u8(a);
  int8x16_t bs = vreinterpretq_s8_u8(b);
  int16x8_t lo = vmull_s8(vget_low_s8(as), vget_low_s8(bs));
  int16x8_t hi = vmull_high_s8(as, bs);
  int16x8_t dot16 = vpaddq_s16(lo, hi);
  int32x4_t dot32 = vpaddlq_s16(dot16);
  return vreinterpretq_u8_s32(vaddq_s32(vreinterpretq_s32_u8(acc), dot32));
}
#define Moonbit_simd_i32x4_relaxed_dot_i8x16_i7x16_add_s(a, b, acc) moonbit_simd_i32x4_relaxed_dot_i8x16_i7x16_add_s((a), (b), (acc))

/* ---- i64x2 ---- */
#define Moonbit_simd_i64x2_eq(a, b) (vreinterpretq_u8_u64( vceqq_u64(vreinterpretq_u64_u8((a)), vreinterpretq_u64_u8((b)))))
#define Moonbit_simd_i64x2_ne(a, b) (vmvnq_u8(Moonbit_simd_i64x2_eq((a), (b))))
#define Moonbit_simd_i64x2_lt_s(a, b) (vreinterpretq_u8_u64( vcltq_s64(vreinterpretq_s64_u8((a)), vreinterpretq_s64_u8((b)))))
#define Moonbit_simd_i64x2_gt_s(a, b) (vreinterpretq_u8_u64( vcgtq_s64(vreinterpretq_s64_u8((a)), vreinterpretq_s64_u8((b)))))
#define Moonbit_simd_i64x2_le_s(a, b) (vreinterpretq_u8_u64( vcleq_s64(vreinterpretq_s64_u8((a)), vreinterpretq_s64_u8((b)))))
#define Moonbit_simd_i64x2_ge_s(a, b) (vreinterpretq_u8_u64( vcgeq_s64(vreinterpretq_s64_u8((a)), vreinterpretq_s64_u8((b)))))
#define Moonbit_simd_i64x2_abs(a) (vreinterpretq_u8_s64(vabsq_s64(vreinterpretq_s64_u8((a)))))
#define Moonbit_simd_i64x2_neg(a) (vreinterpretq_u8_s64(vnegq_s64(vreinterpretq_s64_u8((a)))))
#define Moonbit_simd_i64x2_extend_low_i32x4_s(a) (vreinterpretq_u8_s64(vmovl_s32(vget_low_s32(vreinterpretq_s32_u8((a))))))
#define Moonbit_simd_i64x2_extend_high_i32x4_s(a) (vreinterpretq_u8_s64(vmovl_high_s32(vreinterpretq_s32_u8((a)))))
#define Moonbit_simd_i64x2_extend_low_i32x4_u(a) (vreinterpretq_u8_u64(vmovl_u32(vget_low_u32(vreinterpretq_u32_u8((a))))))
#define Moonbit_simd_i64x2_extend_high_i32x4_u(a) (vreinterpretq_u8_u64(vmovl_high_u32(vreinterpretq_u32_u8((a)))))
#define Moonbit_simd_i64x2_shl(a, n) (vreinterpretq_u8_u64( vshlq_u64(vreinterpretq_u64_u8((a)), vdupq_n_s64((n) & 63))))
#define Moonbit_simd_i64x2_shr_s(a, n) (vreinterpretq_u8_s64( vshlq_s64(vreinterpretq_s64_u8((a)), vdupq_n_s64(-((n) & 63)))))
#define Moonbit_simd_i64x2_shr_u(a, n) (vreinterpretq_u8_u64( vshlq_u64(vreinterpretq_u64_u8((a)), vdupq_n_s64(-((n) & 63)))))
#define Moonbit_simd_i64x2_add(a, b) (vreinterpretq_u8_u64( vaddq_u64(vreinterpretq_u64_u8((a)), vreinterpretq_u64_u8((b)))))
#define Moonbit_simd_i64x2_sub(a, b) (vreinterpretq_u8_u64( vsubq_u64(vreinterpretq_u64_u8((a)), vreinterpretq_u64_u8((b)))))
#define Moonbit_simd_i64x2_extmul_low_i32x4_s(a, b) (vreinterpretq_u8_s64(vmull_s32(vget_low_s32(vreinterpretq_s32_u8((a))), vget_low_s32(vreinterpretq_s32_u8((b))))))
#define Moonbit_simd_i64x2_extmul_high_i32x4_s(a, b) (vreinterpretq_u8_s64( vmull_high_s32(vreinterpretq_s32_u8((a)), vreinterpretq_s32_u8((b)))))
#define Moonbit_simd_i64x2_extmul_low_i32x4_u(a, b) (vreinterpretq_u8_u64(vmull_u32(vget_low_u32(vreinterpretq_u32_u8((a))), vget_low_u32(vreinterpretq_u32_u8((b))))))
#define Moonbit_simd_i64x2_extmul_high_i32x4_u(a, b) (vreinterpretq_u8_u64( vmull_high_u32(vreinterpretq_u32_u8((a)), vreinterpretq_u32_u8((b)))))

/* ---- f32x4 ---- */
#define Moonbit_simd_f32x4_abs(a) (vreinterpretq_u8_f32(vabsq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_neg(a) (vreinterpretq_u8_f32(vnegq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_sqrt(a) (vreinterpretq_u8_f32(vsqrtq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_ceil(a) (vreinterpretq_u8_f32(vrndpq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_floor(a) (vreinterpretq_u8_f32(vrndmq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_trunc(a) (vreinterpretq_u8_f32(vrndq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_nearest(a) (vreinterpretq_u8_f32(vrndnq_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_f32x4_add(a, b) (vreinterpretq_u8_f32( vaddq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_sub(a, b) (vreinterpretq_u8_f32( vsubq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_mul(a, b) (vreinterpretq_u8_f32( vmulq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_div(a, b) (vreinterpretq_u8_f32( vdivq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_min(a, b) (vreinterpretq_u8_f32( vminq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_max(a, b) (vreinterpretq_u8_f32( vmaxq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
static inline moonbit_v128_t moonbit_simd_f32x4_pmin(moonbit_v128_t a, moonbit_v128_t b) {
  uint32x4_t m = vcltq_f32(vreinterpretq_f32_u8(b), vreinterpretq_f32_u8(a));
  return vbslq_u8(vreinterpretq_u8_u32(m), b, a);
}
#define Moonbit_simd_f32x4_pmin(a, b) moonbit_simd_f32x4_pmin((a), (b))
static inline moonbit_v128_t moonbit_simd_f32x4_pmax(moonbit_v128_t a, moonbit_v128_t b) {
  uint32x4_t m = vcltq_f32(vreinterpretq_f32_u8(a), vreinterpretq_f32_u8(b));
  return vbslq_u8(vreinterpretq_u8_u32(m), b, a);
}
#define Moonbit_simd_f32x4_pmax(a, b) moonbit_simd_f32x4_pmax((a), (b))
#define Moonbit_simd_f32x4_eq(a, b) (vreinterpretq_u8_u32( vceqq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_ne(a, b) (vmvnq_u8(Moonbit_simd_f32x4_eq((a), (b))))
#define Moonbit_simd_f32x4_lt(a, b) (vreinterpretq_u8_u32( vcltq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_gt(a, b) (vreinterpretq_u8_u32( vcgtq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_le(a, b) (vreinterpretq_u8_u32( vcleq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_ge(a, b) (vreinterpretq_u8_u32( vcgeq_f32(vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_relaxed_madd(a, b, c) (vreinterpretq_u8_f32(vfmaq_f32(vreinterpretq_f32_u8((c)), vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))
#define Moonbit_simd_f32x4_relaxed_nmadd(a, b, c) (vreinterpretq_u8_f32(vfmsq_f32(vreinterpretq_f32_u8((c)), vreinterpretq_f32_u8((a)), vreinterpretq_f32_u8((b)))))

/* ---- f64x2 ---- */
#define Moonbit_simd_f64x2_abs(a) (vreinterpretq_u8_f64(vabsq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_neg(a) (vreinterpretq_u8_f64(vnegq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_sqrt(a) (vreinterpretq_u8_f64(vsqrtq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_ceil(a) (vreinterpretq_u8_f64(vrndpq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_floor(a) (vreinterpretq_u8_f64(vrndmq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_trunc(a) (vreinterpretq_u8_f64(vrndq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_nearest(a) (vreinterpretq_u8_f64(vrndnq_f64(vreinterpretq_f64_u8((a)))))
#define Moonbit_simd_f64x2_add(a, b) (vreinterpretq_u8_f64( vaddq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_sub(a, b) (vreinterpretq_u8_f64( vsubq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_mul(a, b) (vreinterpretq_u8_f64( vmulq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_div(a, b) (vreinterpretq_u8_f64( vdivq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_min(a, b) (vreinterpretq_u8_f64( vminq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_max(a, b) (vreinterpretq_u8_f64( vmaxq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
static inline moonbit_v128_t moonbit_simd_f64x2_pmin(moonbit_v128_t a, moonbit_v128_t b) {
  uint64x2_t m = vcltq_f64(vreinterpretq_f64_u8(b), vreinterpretq_f64_u8(a));
  return vbslq_u8(vreinterpretq_u8_u64(m), b, a);
}
#define Moonbit_simd_f64x2_pmin(a, b) moonbit_simd_f64x2_pmin((a), (b))
static inline moonbit_v128_t moonbit_simd_f64x2_pmax(moonbit_v128_t a, moonbit_v128_t b) {
  uint64x2_t m = vcltq_f64(vreinterpretq_f64_u8(a), vreinterpretq_f64_u8(b));
  return vbslq_u8(vreinterpretq_u8_u64(m), b, a);
}
#define Moonbit_simd_f64x2_pmax(a, b) moonbit_simd_f64x2_pmax((a), (b))
#define Moonbit_simd_f64x2_eq(a, b) (vreinterpretq_u8_u64( vceqq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_ne(a, b) (vmvnq_u8(Moonbit_simd_f64x2_eq((a), (b))))
#define Moonbit_simd_f64x2_lt(a, b) (vreinterpretq_u8_u64( vcltq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_gt(a, b) (vreinterpretq_u8_u64( vcgtq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_le(a, b) (vreinterpretq_u8_u64( vcleq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_ge(a, b) (vreinterpretq_u8_u64( vcgeq_f64(vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_relaxed_madd(a, b, c) (vreinterpretq_u8_f64(vfmaq_f64(vreinterpretq_f64_u8((c)), vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))
#define Moonbit_simd_f64x2_relaxed_nmadd(a, b, c) (vreinterpretq_u8_f64(vfmsq_f64(vreinterpretq_f64_u8((c)), vreinterpretq_f64_u8((a)), vreinterpretq_f64_u8((b)))))

/* ---- conversions ---- */
#define Moonbit_simd_i32x4_trunc_sat_f32x4_s(a) (vreinterpretq_u8_s32(vcvtq_s32_f32(vreinterpretq_f32_u8((a)))))
#define Moonbit_simd_i32x4_trunc_sat_f32x4_u(a) (vreinterpretq_u8_u32(vcvtq_u32_f32(vreinterpretq_f32_u8((a)))))
static inline moonbit_v128_t moonbit_simd_i32x4_trunc_sat_f64x2_s_zero(moonbit_v128_t a) {
  int32x2_t n = vqmovn_s64(vcvtq_s64_f64(vreinterpretq_f64_u8(a)));
  return vreinterpretq_u8_s32(vcombine_s32(n, vdup_n_s32(0)));
}
#define Moonbit_simd_i32x4_trunc_sat_f64x2_s_zero(a) moonbit_simd_i32x4_trunc_sat_f64x2_s_zero((a))
static inline moonbit_v128_t moonbit_simd_i32x4_trunc_sat_f64x2_u_zero(moonbit_v128_t a) {
  uint32x2_t n = vqmovn_u64(vcvtq_u64_f64(vreinterpretq_f64_u8(a)));
  return vreinterpretq_u8_u32(vcombine_u32(n, vdup_n_u32(0)));
}
#define Moonbit_simd_i32x4_trunc_sat_f64x2_u_zero(a) moonbit_simd_i32x4_trunc_sat_f64x2_u_zero((a))
#define Moonbit_simd_f32x4_convert_i32x4_s(a) (vreinterpretq_u8_f32(vcvtq_f32_s32(vreinterpretq_s32_u8((a)))))
#define Moonbit_simd_f32x4_convert_i32x4_u(a) (vreinterpretq_u8_f32(vcvtq_f32_u32(vreinterpretq_u32_u8((a)))))
#define Moonbit_simd_f64x2_convert_low_i32x4_s(a) (vreinterpretq_u8_f64( vcvtq_f64_s64(vmovl_s32(vget_low_s32(vreinterpretq_s32_u8((a)))))))
#define Moonbit_simd_f64x2_convert_low_i32x4_u(a) (vreinterpretq_u8_f64( vcvtq_f64_u64(vmovl_u32(vget_low_u32(vreinterpretq_u32_u8((a)))))))
#define Moonbit_simd_f32x4_demote_f64x2_zero(a) (vreinterpretq_u8_f32( vcombine_f32(vcvt_f32_f64(vreinterpretq_f64_u8((a))), vdup_n_f32(0.0f))))
#define Moonbit_simd_f64x2_promote_low_f32x4(a) (vreinterpretq_u8_f64(vcvt_f64_f32(vget_low_f32(vreinterpretq_f32_u8((a))))))

/* ---- memory ---- */
static inline moonbit_v128_t moonbit_simd_v128_load(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vld1q_u8(p);
}
#define Moonbit_simd_v128_load(p0, offset) moonbit_simd_v128_load((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load_u16(const uint16_t *p0, int32_t offset) {
  const uint16_t *p = p0 + offset;
  return vld1q_u8((const uint8_t *)p);
}
#define Moonbit_simd_v128_load_u16(p0, offset) moonbit_simd_v128_load_u16((p0), (offset))
static inline int32_t moonbit_simd_v128_store(uint8_t *p0, int32_t offset, moonbit_v128_t v) {
  uint8_t *p = p0 + offset;
  vst1q_u8(p, v);
  return 0;
}
#define Moonbit_simd_v128_store(p0, offset, v) moonbit_simd_v128_store((p0), (offset), (v))
static inline int32_t moonbit_simd_v128_store_u16(uint16_t *p0, int32_t offset, moonbit_v128_t v) {
  uint16_t *p = p0 + offset;
  vst1q_u8((uint8_t *)p, v);
  return 0;
}
#define Moonbit_simd_v128_store_u16(p0, offset, v) moonbit_simd_v128_store_u16((p0), (offset), (v))
static inline moonbit_v128_t moonbit_simd_v128_load8x8_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_s16(vmovl_s8(vld1_s8((const int8_t *)p)));
}
#define Moonbit_simd_v128_load8x8_s(p0, offset) moonbit_simd_v128_load8x8_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load8x8_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u16(vmovl_u8(vld1_u8(p)));
}
#define Moonbit_simd_v128_load8x8_u(p0, offset) moonbit_simd_v128_load8x8_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load8_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vld1q_dup_u8(p);
}
#define Moonbit_simd_v128_load8_splat(p0, offset) moonbit_simd_v128_load8_splat((p0), (offset))

/* ---- simulated with short instruction combinations ---- */
static inline int32_t moonbit_simd_i8x16_bitmask(moonbit_v128_t a) {
  uint16x8_t high_bits = vreinterpretq_u16_u8(vshrq_n_u8(a, 7));
  uint32x4_t paired16 = vreinterpretq_u32_u16(vsraq_n_u16(high_bits, high_bits, 7));
  uint64x2_t paired32 = vreinterpretq_u64_u32(vsraq_n_u32(paired16, paired16, 14));
  uint8x16_t paired64 = vreinterpretq_u8_u64(vsraq_n_u64(paired32, paired32, 28));
  return (int32_t)vgetq_lane_u8(paired64, 0) |
         ((int32_t)vgetq_lane_u8(paired64, 8) << 8);
}
#define Moonbit_simd_i8x16_bitmask(a) moonbit_simd_i8x16_bitmask((a))
static inline int32_t moonbit_simd_i16x8_bitmask(moonbit_v128_t a) {
  static const int16_t shifts[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  uint16x8_t bits = vshrq_n_u16(vreinterpretq_u16_u8(a), 15);
  return (int32_t)vaddvq_u16(vshlq_u16(bits, vld1q_s16(shifts)));
}
#define Moonbit_simd_i16x8_bitmask(a) moonbit_simd_i16x8_bitmask((a))
static inline int32_t moonbit_simd_i32x4_bitmask(moonbit_v128_t a) {
  static const int32_t shifts[4] = {0, 1, 2, 3};
  uint32x4_t bits = vshrq_n_u32(vreinterpretq_u32_u8(a), 31);
  return (int32_t)vaddvq_u32(vshlq_u32(bits, vld1q_s32(shifts)));
}
#define Moonbit_simd_i32x4_bitmask(a) moonbit_simd_i32x4_bitmask((a))
static inline int32_t moonbit_simd_i64x2_all_true(moonbit_v128_t a) {
  /* no vminvq_u64 exists: build a per-lane is-zero mask instead and
     check that no mask bit is set */
  return vmaxvq_u32(vreinterpretq_u32_u64(vceqzq_u64(vreinterpretq_u64_u8(a)))) == 0;
}
#define Moonbit_simd_i64x2_all_true(a) moonbit_simd_i64x2_all_true((a))
static inline int32_t moonbit_simd_i64x2_bitmask(moonbit_v128_t a) {
  static const int64_t shifts[2] = {0, 1};
  uint64x2_t bits = vshrq_n_u64(vreinterpretq_u64_u8(a), 63);
  return (int32_t)vaddvq_u64(vshlq_u64(bits, vld1q_s64(shifts)));
}
#define Moonbit_simd_i64x2_bitmask(a) moonbit_simd_i64x2_bitmask((a))
static inline moonbit_v128_t moonbit_simd_i64x2_mul(moonbit_v128_t a, moonbit_v128_t b) {
  uint64x2_t ua = vreinterpretq_u64_u8(a);
  uint64x2_t ub = vreinterpretq_u64_u8(b);
  uint32x2_t a_lo = vmovn_u64(ua);
  uint32x2_t b_lo = vmovn_u64(ub);
  uint32x4_t a32 = vreinterpretq_u32_u64(ua);
  uint32x4_t b_swap = vrev64q_u32(vreinterpretq_u32_u64(ub));
  uint64x2_t cross = vshlq_n_u64(vpaddlq_u32(vmulq_u32(a32, b_swap)), 32);
  return vreinterpretq_u8_u64(vmlal_u32(cross, a_lo, b_lo));
}
#define Moonbit_simd_i64x2_mul(a, b) moonbit_simd_i64x2_mul((a), (b))
static inline moonbit_v128_t moonbit_simd_v128_load16x4_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_s32(vmovl_s16(vld1_s16((const int16_t *)p)));
}
#define Moonbit_simd_v128_load16x4_s(p0, offset) moonbit_simd_v128_load16x4_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load16x4_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u32(vmovl_u16(vld1_u16((const uint16_t *)p)));
}
#define Moonbit_simd_v128_load16x4_u(p0, offset) moonbit_simd_v128_load16x4_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32x2_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_s64(vmovl_s32(vld1_s32((const int32_t *)p)));
}
#define Moonbit_simd_v128_load32x2_s(p0, offset) moonbit_simd_v128_load32x2_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32x2_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u64(vmovl_u32(vld1_u32((const uint32_t *)p)));
}
#define Moonbit_simd_v128_load32x2_u(p0, offset) moonbit_simd_v128_load32x2_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load16_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u16(vld1q_dup_u16((const uint16_t *)p));
}
#define Moonbit_simd_v128_load16_splat(p0, offset) moonbit_simd_v128_load16_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u32(vld1q_dup_u32((const uint32_t *)p));
}
#define Moonbit_simd_v128_load32_splat(p0, offset) moonbit_simd_v128_load32_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load64_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return vreinterpretq_u8_u64(vld1q_dup_u64((const uint64_t *)p));
}
#define Moonbit_simd_v128_load64_splat(p0, offset) moonbit_simd_v128_load64_splat((p0), (offset))


/* Lane accesses: the compiler emits these ops with literal lane
   immediates only (a call with a non-constant lane is never specialized
   and goes to the MoonBit implementation directly), so the fast paths
   are function-like macros and the lane arrives as the compile-time
   constant the lane intrinsics require. Arguments are emitted as simple
   variables or constants, so multiple expansion is safe. */
#define Moonbit_simd_i8x16_extract_lane_s(v, lane) \
  ((int32_t)vgetq_lane_s8(vreinterpretq_s8_u8((v)), (lane)))
#define Moonbit_simd_i8x16_extract_lane_u(v, lane) \
  ((uint32_t)vgetq_lane_u8((v), (lane)))
#define Moonbit_simd_i16x8_extract_lane_s(v, lane) \
  ((int32_t)vgetq_lane_s16(vreinterpretq_s16_u8((v)), (lane)))
#define Moonbit_simd_i16x8_extract_lane_u(v, lane) \
  ((uint32_t)vgetq_lane_u16(vreinterpretq_u16_u8((v)), (lane)))
#define Moonbit_simd_i32x4_extract_lane(v, lane) \
  (vgetq_lane_u32(vreinterpretq_u32_u8((v)), (lane)))
#define Moonbit_simd_i64x2_extract_lane(v, lane) \
  (vgetq_lane_u64(vreinterpretq_u64_u8((v)), (lane)))
#define Moonbit_simd_f32x4_extract_lane(v, lane) \
  (vgetq_lane_f32(vreinterpretq_f32_u8((v)), (lane)))
#define Moonbit_simd_f64x2_extract_lane(v, lane) \
  (vgetq_lane_f64(vreinterpretq_f64_u8((v)), (lane)))
#define Moonbit_simd_i8x16_replace_lane(v, x, lane) \
  (vsetq_lane_u8((uint8_t)(x), (v), (lane)))
#define Moonbit_simd_i16x8_replace_lane(v, x, lane) \
  (vreinterpretq_u8_u16(vsetq_lane_u16((uint16_t)(x), vreinterpretq_u16_u8((v)), (lane))))
#define Moonbit_simd_i32x4_replace_lane(v, x, lane) \
  (vreinterpretq_u8_u32(vsetq_lane_u32((uint32_t)(x), vreinterpretq_u32_u8((v)), (lane))))
#define Moonbit_simd_i64x2_replace_lane(v, x, lane) \
  (vreinterpretq_u8_u64(vsetq_lane_u64((uint64_t)(x), vreinterpretq_u64_u8((v)), (lane))))
#define Moonbit_simd_f32x4_replace_lane(v, x, lane) \
  (vreinterpretq_u8_f32(vsetq_lane_f32((float)(x), vreinterpretq_f32_u8((v)), (lane))))
#define Moonbit_simd_f64x2_replace_lane(v, x, lane) \
  (vreinterpretq_u8_f64(vsetq_lane_f64((double)(x), vreinterpretq_f64_u8((v)), (lane))))

#elif defined(MOONBIT_V128_SSE2)

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif

#define MOONBIT_SSE_PS(v) _mm_castsi128_ps(v)
#define MOONBIT_SSE_PD(v) _mm_castsi128_pd(v)
#define MOONBIT_SSE_FROM_PS(v) _mm_castps_si128(v)
#define MOONBIT_SSE_FROM_PD(v) _mm_castpd_si128(v)

/* ---- v128 bitwise ---- */
#define Moonbit_simd_v128_not(a) (_mm_xor_si128((a), _mm_set1_epi32(-1)))
#define Moonbit_simd_v128_and(a, b) (_mm_and_si128((a), (b)))
#define Moonbit_simd_v128_andnot(a, b) (_mm_andnot_si128((b), (a)))
#define Moonbit_simd_v128_or(a, b) (_mm_or_si128((a), (b)))
#define Moonbit_simd_v128_xor(a, b) (_mm_xor_si128((a), (b)))
static inline moonbit_v128_t moonbit_simd_v128_bitselect(moonbit_v128_t a,
                                                         moonbit_v128_t b,
                                                         moonbit_v128_t m) {
  return _mm_or_si128(_mm_and_si128(m, a), _mm_andnot_si128(m, b));
}
#define Moonbit_simd_v128_bitselect(a, b, m) moonbit_simd_v128_bitselect((a), (b), (m))
#define Moonbit_simd_v128_any_true(a) (_mm_movemask_epi8(_mm_cmpeq_epi8((a), _mm_setzero_si128())) != 0xffff)

/* ---- splat ---- */
#define Moonbit_simd_i8x16_splat(x) (_mm_set1_epi8((char)(x)))
#define Moonbit_simd_i16x8_splat(x) (_mm_set1_epi16((short)(x)))
#define Moonbit_simd_i32x4_splat(x) (_mm_set1_epi32((int)(x)))
#define Moonbit_simd_i64x2_splat(x) (_mm_set1_epi64x((long long)(x)))
#define Moonbit_simd_f32x4_splat(x) (MOONBIT_SSE_FROM_PS(_mm_set1_ps((x))))
#define Moonbit_simd_f64x2_splat(x) (MOONBIT_SSE_FROM_PD(_mm_set1_pd((x))))

/* ---- i8x16 (signed compares only; unsigned ones use the fallback) ---- */
#define Moonbit_simd_i8x16_eq(a, b) (_mm_cmpeq_epi8((a), (b)))
#define Moonbit_simd_i8x16_ne(a, b) (_mm_xor_si128(_mm_cmpeq_epi8((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i8x16_lt_s(a, b) (_mm_cmpgt_epi8((b), (a)))
#define Moonbit_simd_i8x16_gt_s(a, b) (_mm_cmpgt_epi8((a), (b)))
#define Moonbit_simd_i8x16_le_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi8((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i8x16_ge_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi8((b), (a)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i8x16_neg(a) (_mm_sub_epi8(_mm_setzero_si128(), (a)))
#define Moonbit_simd_i8x16_all_true(a) (_mm_movemask_epi8(_mm_cmpeq_epi8((a), _mm_setzero_si128())) == 0)
#define Moonbit_simd_i8x16_bitmask(a) (_mm_movemask_epi8((a)))
#define Moonbit_simd_i8x16_narrow_i16x8_s(a, b) (_mm_packs_epi16((a), (b)))
#define Moonbit_simd_i8x16_narrow_i16x8_u(a, b) (_mm_packus_epi16((a), (b)))
#define Moonbit_simd_i8x16_add(a, b) (_mm_add_epi8((a), (b)))
#define Moonbit_simd_i8x16_sub(a, b) (_mm_sub_epi8((a), (b)))
#define Moonbit_simd_i8x16_add_sat_s(a, b) (_mm_adds_epi8((a), (b)))
#define Moonbit_simd_i8x16_add_sat_u(a, b) (_mm_adds_epu8((a), (b)))
#define Moonbit_simd_i8x16_sub_sat_s(a, b) (_mm_subs_epi8((a), (b)))
#define Moonbit_simd_i8x16_sub_sat_u(a, b) (_mm_subs_epu8((a), (b)))
#define Moonbit_simd_i8x16_min_u(a, b) (_mm_min_epu8((a), (b)))
#define Moonbit_simd_i8x16_max_u(a, b) (_mm_max_epu8((a), (b)))
#define Moonbit_simd_i8x16_avgr_u(a, b) (_mm_avg_epu8((a), (b)))

/* ---- i16x8 ---- */
#define Moonbit_simd_i16x8_eq(a, b) (_mm_cmpeq_epi16((a), (b)))
#define Moonbit_simd_i16x8_ne(a, b) (_mm_xor_si128(_mm_cmpeq_epi16((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i16x8_lt_s(a, b) (_mm_cmpgt_epi16((b), (a)))
#define Moonbit_simd_i16x8_gt_s(a, b) (_mm_cmpgt_epi16((a), (b)))
#define Moonbit_simd_i16x8_le_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi16((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i16x8_ge_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi16((b), (a)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i16x8_neg(a) (_mm_sub_epi16(_mm_setzero_si128(), (a)))
#define Moonbit_simd_i16x8_all_true(a) (_mm_movemask_epi8(_mm_cmpeq_epi16((a), _mm_setzero_si128())) == 0)
#define Moonbit_simd_i16x8_bitmask(a) (_mm_movemask_epi8(_mm_packs_epi16((a), _mm_setzero_si128())) & 0xff)
#define Moonbit_simd_i16x8_narrow_i32x4_s(a, b) (_mm_packs_epi32((a), (b)))
static inline moonbit_v128_t moonbit_simd_i16x8_extend_low_i8x16_s(moonbit_v128_t a) {
  return _mm_srai_epi16(_mm_unpacklo_epi8(a, a), 8);
}
#define Moonbit_simd_i16x8_extend_low_i8x16_s(a) moonbit_simd_i16x8_extend_low_i8x16_s((a))
static inline moonbit_v128_t moonbit_simd_i16x8_extend_high_i8x16_s(moonbit_v128_t a) {
  return _mm_srai_epi16(_mm_unpackhi_epi8(a, a), 8);
}
#define Moonbit_simd_i16x8_extend_high_i8x16_s(a) moonbit_simd_i16x8_extend_high_i8x16_s((a))
#define Moonbit_simd_i16x8_extend_low_i8x16_u(a) (_mm_unpacklo_epi8((a), _mm_setzero_si128()))
#define Moonbit_simd_i16x8_extend_high_i8x16_u(a) (_mm_unpackhi_epi8((a), _mm_setzero_si128()))
#define Moonbit_simd_i16x8_shl(a, n) (_mm_sll_epi16((a), _mm_cvtsi32_si128((n) & 15)))
#define Moonbit_simd_i16x8_shr_s(a, n) (_mm_sra_epi16((a), _mm_cvtsi32_si128((n) & 15)))
#define Moonbit_simd_i16x8_shr_u(a, n) (_mm_srl_epi16((a), _mm_cvtsi32_si128((n) & 15)))
#define Moonbit_simd_i16x8_add(a, b) (_mm_add_epi16((a), (b)))
#define Moonbit_simd_i16x8_sub(a, b) (_mm_sub_epi16((a), (b)))
#define Moonbit_simd_i16x8_mul(a, b) (_mm_mullo_epi16((a), (b)))
#define Moonbit_simd_i16x8_add_sat_s(a, b) (_mm_adds_epi16((a), (b)))
#define Moonbit_simd_i16x8_add_sat_u(a, b) (_mm_adds_epu16((a), (b)))
#define Moonbit_simd_i16x8_sub_sat_s(a, b) (_mm_subs_epi16((a), (b)))
#define Moonbit_simd_i16x8_sub_sat_u(a, b) (_mm_subs_epu16((a), (b)))
#define Moonbit_simd_i16x8_min_s(a, b) (_mm_min_epi16((a), (b)))
#define Moonbit_simd_i16x8_max_s(a, b) (_mm_max_epi16((a), (b)))
#define Moonbit_simd_i16x8_avgr_u(a, b) (_mm_avg_epu16((a), (b)))

/* ---- i32x4 ---- */
#define Moonbit_simd_i32x4_eq(a, b) (_mm_cmpeq_epi32((a), (b)))
#define Moonbit_simd_i32x4_ne(a, b) (_mm_xor_si128(_mm_cmpeq_epi32((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i32x4_lt_s(a, b) (_mm_cmpgt_epi32((b), (a)))
#define Moonbit_simd_i32x4_gt_s(a, b) (_mm_cmpgt_epi32((a), (b)))
#define Moonbit_simd_i32x4_le_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi32((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i32x4_ge_s(a, b) (_mm_xor_si128(_mm_cmpgt_epi32((b), (a)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i32x4_neg(a) (_mm_sub_epi32(_mm_setzero_si128(), (a)))
#define Moonbit_simd_i32x4_all_true(a) (_mm_movemask_epi8(_mm_cmpeq_epi32((a), _mm_setzero_si128())) == 0)
#define Moonbit_simd_i32x4_bitmask(a) (_mm_movemask_ps(MOONBIT_SSE_PS((a))))
static inline moonbit_v128_t moonbit_simd_i32x4_extend_low_i16x8_s(moonbit_v128_t a) {
  return _mm_srai_epi32(_mm_unpacklo_epi16(a, a), 16);
}
#define Moonbit_simd_i32x4_extend_low_i16x8_s(a) moonbit_simd_i32x4_extend_low_i16x8_s((a))
static inline moonbit_v128_t moonbit_simd_i32x4_extend_high_i16x8_s(moonbit_v128_t a) {
  return _mm_srai_epi32(_mm_unpackhi_epi16(a, a), 16);
}
#define Moonbit_simd_i32x4_extend_high_i16x8_s(a) moonbit_simd_i32x4_extend_high_i16x8_s((a))
#define Moonbit_simd_i32x4_extend_low_i16x8_u(a) (_mm_unpacklo_epi16((a), _mm_setzero_si128()))
#define Moonbit_simd_i32x4_extend_high_i16x8_u(a) (_mm_unpackhi_epi16((a), _mm_setzero_si128()))
#define Moonbit_simd_i32x4_shl(a, n) (_mm_sll_epi32((a), _mm_cvtsi32_si128((n) & 31)))
#define Moonbit_simd_i32x4_shr_s(a, n) (_mm_sra_epi32((a), _mm_cvtsi32_si128((n) & 31)))
#define Moonbit_simd_i32x4_shr_u(a, n) (_mm_srl_epi32((a), _mm_cvtsi32_si128((n) & 31)))
#define Moonbit_simd_i32x4_add(a, b) (_mm_add_epi32((a), (b)))
#define Moonbit_simd_i32x4_sub(a, b) (_mm_sub_epi32((a), (b)))
#define Moonbit_simd_i32x4_dot_i16x8_s(a, b) (_mm_madd_epi16((a), (b)))
#define Moonbit_simd_i32x4_extadd_pairwise_i16x8_s(a) (_mm_madd_epi16((a), _mm_set1_epi16(1)))

/* ---- i64x2 ---- */
#define Moonbit_simd_i64x2_neg(a) (_mm_sub_epi64(_mm_setzero_si128(), (a)))
#define Moonbit_simd_i64x2_bitmask(a) (_mm_movemask_pd(MOONBIT_SSE_PD((a))))
static inline moonbit_v128_t moonbit_simd_i64x2_extend_low_i32x4_s(moonbit_v128_t a) {
  return _mm_unpacklo_epi32(a, _mm_srai_epi32(a, 31));
}
#define Moonbit_simd_i64x2_extend_low_i32x4_s(a) moonbit_simd_i64x2_extend_low_i32x4_s((a))
static inline moonbit_v128_t moonbit_simd_i64x2_extend_high_i32x4_s(moonbit_v128_t a) {
  return _mm_unpackhi_epi32(a, _mm_srai_epi32(a, 31));
}
#define Moonbit_simd_i64x2_extend_high_i32x4_s(a) moonbit_simd_i64x2_extend_high_i32x4_s((a))
#define Moonbit_simd_i64x2_extend_low_i32x4_u(a) (_mm_unpacklo_epi32((a), _mm_setzero_si128()))
#define Moonbit_simd_i64x2_extend_high_i32x4_u(a) (_mm_unpackhi_epi32((a), _mm_setzero_si128()))
#define Moonbit_simd_i64x2_shl(a, n) (_mm_sll_epi64((a), _mm_cvtsi32_si128((n) & 63)))
#define Moonbit_simd_i64x2_shr_u(a, n) (_mm_srl_epi64((a), _mm_cvtsi32_si128((n) & 63)))
#define Moonbit_simd_i64x2_add(a, b) (_mm_add_epi64((a), (b)))
#define Moonbit_simd_i64x2_sub(a, b) (_mm_sub_epi64((a), (b)))

/* ---- f32x4 ---- */
#define Moonbit_simd_f32x4_abs(a) (_mm_and_si128((a), _mm_set1_epi32(0x7fffffff)))
#define Moonbit_simd_f32x4_neg(a) (_mm_xor_si128((a), _mm_set1_epi32((int)0x80000000u)))
#define Moonbit_simd_f32x4_sqrt(a) (MOONBIT_SSE_FROM_PS(_mm_sqrt_ps(MOONBIT_SSE_PS((a)))))
#define Moonbit_simd_f32x4_add(a, b) (MOONBIT_SSE_FROM_PS(_mm_add_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_sub(a, b) (MOONBIT_SSE_FROM_PS(_mm_sub_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_mul(a, b) (MOONBIT_SSE_FROM_PS(_mm_mul_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_div(a, b) (MOONBIT_SSE_FROM_PS(_mm_div_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
static inline moonbit_v128_t moonbit_simd_f32x4_pmin(moonbit_v128_t a, moonbit_v128_t b) {
  /* wasm pmin: b < a ? b : a; minps returns the second operand on NaN,
     so the operand order makes the semantics match exactly */
  return MOONBIT_SSE_FROM_PS(_mm_min_ps(MOONBIT_SSE_PS(b), MOONBIT_SSE_PS(a)));
}
#define Moonbit_simd_f32x4_pmin(a, b) moonbit_simd_f32x4_pmin((a), (b))
#define Moonbit_simd_f32x4_pmax(a, b) (MOONBIT_SSE_FROM_PS(_mm_max_ps(MOONBIT_SSE_PS((b)), MOONBIT_SSE_PS((a)))))
#define Moonbit_simd_f32x4_eq(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmpeq_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_ne(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmpneq_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_lt(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmplt_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_gt(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmpgt_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_le(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmple_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_ge(a, b) (MOONBIT_SSE_FROM_PS(_mm_cmpge_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b)))))
#define Moonbit_simd_f32x4_relaxed_madd(a, b, c) (MOONBIT_SSE_FROM_PS( _mm_add_ps(_mm_mul_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b))), MOONBIT_SSE_PS((c)))))
#define Moonbit_simd_f32x4_relaxed_nmadd(a, b, c) (MOONBIT_SSE_FROM_PS( _mm_sub_ps(MOONBIT_SSE_PS((c)), _mm_mul_ps(MOONBIT_SSE_PS((a)), MOONBIT_SSE_PS((b))))))
#define Moonbit_simd_f32x4_convert_i32x4_s(a) (MOONBIT_SSE_FROM_PS(_mm_cvtepi32_ps((a))))

/* ---- f64x2 ---- */
#define Moonbit_simd_f64x2_abs(a) (_mm_and_si128((a), _mm_set1_epi64x(0x7fffffffffffffffll)))
#define Moonbit_simd_f64x2_neg(a) (_mm_xor_si128((a), _mm_set1_epi64x((long long)0x8000000000000000ull)))
#define Moonbit_simd_f64x2_sqrt(a) (MOONBIT_SSE_FROM_PD(_mm_sqrt_pd(MOONBIT_SSE_PD((a)))))
#define Moonbit_simd_f64x2_add(a, b) (MOONBIT_SSE_FROM_PD(_mm_add_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_sub(a, b) (MOONBIT_SSE_FROM_PD(_mm_sub_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_mul(a, b) (MOONBIT_SSE_FROM_PD(_mm_mul_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_div(a, b) (MOONBIT_SSE_FROM_PD(_mm_div_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_pmin(a, b) (MOONBIT_SSE_FROM_PD(_mm_min_pd(MOONBIT_SSE_PD((b)), MOONBIT_SSE_PD((a)))))
#define Moonbit_simd_f64x2_pmax(a, b) (MOONBIT_SSE_FROM_PD(_mm_max_pd(MOONBIT_SSE_PD((b)), MOONBIT_SSE_PD((a)))))
#define Moonbit_simd_f64x2_eq(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmpeq_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_ne(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmpneq_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_lt(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmplt_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_gt(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmpgt_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_le(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmple_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_ge(a, b) (MOONBIT_SSE_FROM_PD(_mm_cmpge_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b)))))
#define Moonbit_simd_f64x2_relaxed_madd(a, b, c) (MOONBIT_SSE_FROM_PD( _mm_add_pd(_mm_mul_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b))), MOONBIT_SSE_PD((c)))))
#define Moonbit_simd_f64x2_relaxed_nmadd(a, b, c) (MOONBIT_SSE_FROM_PD( _mm_sub_pd(MOONBIT_SSE_PD((c)), _mm_mul_pd(MOONBIT_SSE_PD((a)), MOONBIT_SSE_PD((b))))))
#define Moonbit_simd_f64x2_convert_low_i32x4_s(a) (MOONBIT_SSE_FROM_PD(_mm_cvtepi32_pd((a))))
#define Moonbit_simd_f32x4_demote_f64x2_zero(a) (MOONBIT_SSE_FROM_PS(_mm_cvtpd_ps(MOONBIT_SSE_PD((a)))))
#define Moonbit_simd_f64x2_promote_low_f32x4(a) (MOONBIT_SSE_FROM_PD(_mm_cvtps_pd(MOONBIT_SSE_PS((a)))))

/* ---- memory ---- */
static inline moonbit_v128_t moonbit_simd_v128_load(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_loadu_si128((const __m128i *)p);
}
#define Moonbit_simd_v128_load(p0, offset) moonbit_simd_v128_load((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load_u16(const uint16_t *p0, int32_t offset) {
  const uint16_t *p = p0 + offset;
  return _mm_loadu_si128((const __m128i *)p);
}
#define Moonbit_simd_v128_load_u16(p0, offset) moonbit_simd_v128_load_u16((p0), (offset))
static inline int32_t moonbit_simd_v128_store(uint8_t *p0, int32_t offset, moonbit_v128_t v) {
  uint8_t *p = p0 + offset;
  _mm_storeu_si128((__m128i *)p, v);
  return 0;
}
#define Moonbit_simd_v128_store(p0, offset, v) moonbit_simd_v128_store((p0), (offset), (v))
static inline int32_t moonbit_simd_v128_store_u16(uint16_t *p0, int32_t offset, moonbit_v128_t v) {
  uint16_t *p = p0 + offset;
  _mm_storeu_si128((__m128i *)p, v);
  return 0;
}
#define Moonbit_simd_v128_store_u16(p0, offset, v) moonbit_simd_v128_store_u16((p0), (offset), (v))

/* ---- simulated with short instruction combinations ---- */

/* unsigned compares: flip the sign bit, then compare signed */
#define MOONBIT_SSE_BIAS8 _mm_set1_epi8((char)0x80)
#define MOONBIT_SSE_BIAS16 _mm_set1_epi16((short)0x8000)
#define MOONBIT_SSE_BIAS32 _mm_set1_epi32((int)0x80000000u)
#define Moonbit_simd_i8x16_lt_u(a, b) (_mm_cmpgt_epi8(_mm_xor_si128((b), MOONBIT_SSE_BIAS8), _mm_xor_si128((a), MOONBIT_SSE_BIAS8)))
#define Moonbit_simd_i8x16_gt_u(a, b) (Moonbit_simd_i8x16_lt_u((b), (a)))
static inline moonbit_v128_t moonbit_simd_i8x16_le_u(moonbit_v128_t a, moonbit_v128_t b) {
  /* a <= b iff min_u(a, b) == a */
  return _mm_cmpeq_epi8(a, _mm_min_epu8(a, b));
}
#define Moonbit_simd_i8x16_le_u(a, b) moonbit_simd_i8x16_le_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i8x16_ge_u(moonbit_v128_t a, moonbit_v128_t b) {
  /* a >= b iff max_u(a, b) == a */
  return _mm_cmpeq_epi8(a, _mm_max_epu8(a, b));
}
#define Moonbit_simd_i8x16_ge_u(a, b) moonbit_simd_i8x16_ge_u((a), (b))
#define Moonbit_simd_i16x8_lt_u(a, b) (_mm_cmpgt_epi16(_mm_xor_si128((b), MOONBIT_SSE_BIAS16), _mm_xor_si128((a), MOONBIT_SSE_BIAS16)))
#define Moonbit_simd_i16x8_gt_u(a, b) (Moonbit_simd_i16x8_lt_u((b), (a)))
static inline moonbit_v128_t moonbit_simd_i16x8_le_u(moonbit_v128_t a, moonbit_v128_t b) {
  /* a <= b iff a -sat_u b == 0 */
  return _mm_cmpeq_epi16(_mm_subs_epu16(a, b), _mm_setzero_si128());
}
#define Moonbit_simd_i16x8_le_u(a, b) moonbit_simd_i16x8_le_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_ge_u(moonbit_v128_t a, moonbit_v128_t b) {
  /* a >= b iff b -sat_u a == 0 */
  return _mm_cmpeq_epi16(_mm_subs_epu16(b, a), _mm_setzero_si128());
}
#define Moonbit_simd_i16x8_ge_u(a, b) moonbit_simd_i16x8_ge_u((a), (b))
#define Moonbit_simd_i32x4_lt_u(a, b) (_mm_cmpgt_epi32(_mm_xor_si128((b), MOONBIT_SSE_BIAS32), _mm_xor_si128((a), MOONBIT_SSE_BIAS32)))
#define Moonbit_simd_i32x4_gt_u(a, b) (Moonbit_simd_i32x4_lt_u((b), (a)))
#define Moonbit_simd_i32x4_le_u(a, b) (_mm_xor_si128(Moonbit_simd_i32x4_gt_u((a), (b)), _mm_set1_epi32(-1)))
#define Moonbit_simd_i32x4_ge_u(a, b) (_mm_xor_si128(Moonbit_simd_i32x4_lt_u((a), (b)), _mm_set1_epi32(-1)))

/* min/max via compare and blend (or the subs trick for u16) */
static inline moonbit_v128_t moonbit_simd_i8x16_min_s(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = _mm_cmpgt_epi8(a, b);
  return _mm_or_si128(_mm_and_si128(m, b), _mm_andnot_si128(m, a));
}
#define Moonbit_simd_i8x16_min_s(a, b) moonbit_simd_i8x16_min_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i8x16_max_s(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = _mm_cmpgt_epi8(a, b);
  return _mm_or_si128(_mm_and_si128(m, a), _mm_andnot_si128(m, b));
}
#define Moonbit_simd_i8x16_max_s(a, b) moonbit_simd_i8x16_max_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_min_u(moonbit_v128_t a, moonbit_v128_t b) {
  return _mm_sub_epi16(a, _mm_subs_epu16(a, b));
}
#define Moonbit_simd_i16x8_min_u(a, b) moonbit_simd_i16x8_min_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_max_u(moonbit_v128_t a, moonbit_v128_t b) {
  return _mm_add_epi16(b, _mm_subs_epu16(a, b));
}
#define Moonbit_simd_i16x8_max_u(a, b) moonbit_simd_i16x8_max_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_min_s(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = _mm_cmpgt_epi32(a, b);
  return _mm_or_si128(_mm_and_si128(m, b), _mm_andnot_si128(m, a));
}
#define Moonbit_simd_i32x4_min_s(a, b) moonbit_simd_i32x4_min_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_max_s(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = _mm_cmpgt_epi32(a, b);
  return _mm_or_si128(_mm_and_si128(m, a), _mm_andnot_si128(m, b));
}
#define Moonbit_simd_i32x4_max_s(a, b) moonbit_simd_i32x4_max_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_min_u(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = Moonbit_simd_i32x4_gt_u(a, b);
  return _mm_or_si128(_mm_and_si128(m, b), _mm_andnot_si128(m, a));
}
#define Moonbit_simd_i32x4_min_u(a, b) moonbit_simd_i32x4_min_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_max_u(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i m = Moonbit_simd_i32x4_gt_u(a, b);
  return _mm_or_si128(_mm_and_si128(m, a), _mm_andnot_si128(m, b));
}
#define Moonbit_simd_i32x4_max_u(a, b) moonbit_simd_i32x4_max_u((a), (b))

/* 8-bit shifts via 16-bit shifts plus masking */
static inline moonbit_v128_t moonbit_simd_i8x16_shl(moonbit_v128_t a, int32_t n) {
  int c = n & 7;
  return _mm_and_si128(_mm_sll_epi16(a, _mm_cvtsi32_si128(c)),
                       _mm_set1_epi8((char)(0xffu << c)));
}
#define Moonbit_simd_i8x16_shl(a, n) moonbit_simd_i8x16_shl((a), (n))
static inline moonbit_v128_t moonbit_simd_i8x16_shr_u(moonbit_v128_t a, int32_t n) {
  int c = n & 7;
  return _mm_and_si128(_mm_srl_epi16(a, _mm_cvtsi32_si128(c)),
                       _mm_set1_epi8((char)(0xffu >> c)));
}
#define Moonbit_simd_i8x16_shr_u(a, n) moonbit_simd_i8x16_shr_u((a), (n))
static inline moonbit_v128_t moonbit_simd_i8x16_shr_s(moonbit_v128_t a, int32_t n) {
  __m128i cnt = _mm_cvtsi32_si128((n & 7) + 8);
  __m128i lo = _mm_sra_epi16(_mm_unpacklo_epi8(a, a), cnt);
  __m128i hi = _mm_sra_epi16(_mm_unpackhi_epi8(a, a), cnt);
  return _mm_packs_epi16(lo, hi);
}
#define Moonbit_simd_i8x16_shr_s(a, n) moonbit_simd_i8x16_shr_s((a), (n))

/* abs via sign-mask blend */
static inline moonbit_v128_t moonbit_simd_i8x16_abs(moonbit_v128_t a) {
  __m128i m = _mm_cmpgt_epi8(_mm_setzero_si128(), a);
  return _mm_sub_epi8(_mm_xor_si128(a, m), m);
}
#define Moonbit_simd_i8x16_abs(a) moonbit_simd_i8x16_abs((a))
static inline moonbit_v128_t moonbit_simd_i16x8_abs(moonbit_v128_t a) {
  return _mm_max_epi16(a, _mm_sub_epi16(_mm_setzero_si128(), a));
}
#define Moonbit_simd_i16x8_abs(a) moonbit_simd_i16x8_abs((a))
static inline moonbit_v128_t moonbit_simd_i32x4_abs(moonbit_v128_t a) {
  __m128i m = _mm_srai_epi32(a, 31);
  return _mm_sub_epi32(_mm_xor_si128(a, m), m);
}
#define Moonbit_simd_i32x4_abs(a) moonbit_simd_i32x4_abs((a))

/* 32/64-bit multiplies built from mul_epu32 */
static inline moonbit_v128_t moonbit_simd_i32x4_mul(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i evens = _mm_mul_epu32(a, b);
  __m128i odds = _mm_mul_epu32(_mm_srli_si128(a, 4), _mm_srli_si128(b, 4));
  return _mm_unpacklo_epi32(_mm_shuffle_epi32(evens, _MM_SHUFFLE(0, 0, 2, 0)),
                            _mm_shuffle_epi32(odds, _MM_SHUFFLE(0, 0, 2, 0)));
}
#define Moonbit_simd_i32x4_mul(a, b) moonbit_simd_i32x4_mul((a), (b))
static inline moonbit_v128_t moonbit_simd_i64x2_mul(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i lo_mul = _mm_mul_epu32(a, b);
  __m128i cross = _mm_add_epi64(_mm_mul_epu32(_mm_srli_epi64(a, 32), b),
                                _mm_mul_epu32(a, _mm_srli_epi64(b, 32)));
  return _mm_add_epi64(lo_mul, _mm_slli_epi64(cross, 32));
}
#define Moonbit_simd_i64x2_mul(a, b) moonbit_simd_i64x2_mul((a), (b))

/* extended multiplies via mullo/mulhi interleave (16->32) and extend+mullo (8->16) */
static inline moonbit_v128_t moonbit_simd_i32x4_extmul_low_i16x8_s(moonbit_v128_t a,
                                                                   moonbit_v128_t b) {
  return _mm_unpacklo_epi16(_mm_mullo_epi16(a, b), _mm_mulhi_epi16(a, b));
}
#define Moonbit_simd_i32x4_extmul_low_i16x8_s(a, b) moonbit_simd_i32x4_extmul_low_i16x8_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_extmul_high_i16x8_s(moonbit_v128_t a,
                                                                    moonbit_v128_t b) {
  return _mm_unpackhi_epi16(_mm_mullo_epi16(a, b), _mm_mulhi_epi16(a, b));
}
#define Moonbit_simd_i32x4_extmul_high_i16x8_s(a, b) moonbit_simd_i32x4_extmul_high_i16x8_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_extmul_low_i16x8_u(moonbit_v128_t a,
                                                                   moonbit_v128_t b) {
  return _mm_unpacklo_epi16(_mm_mullo_epi16(a, b), _mm_mulhi_epu16(a, b));
}
#define Moonbit_simd_i32x4_extmul_low_i16x8_u(a, b) moonbit_simd_i32x4_extmul_low_i16x8_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i32x4_extmul_high_i16x8_u(moonbit_v128_t a,
                                                                    moonbit_v128_t b) {
  return _mm_unpackhi_epi16(_mm_mullo_epi16(a, b), _mm_mulhi_epu16(a, b));
}
#define Moonbit_simd_i32x4_extmul_high_i16x8_u(a, b) moonbit_simd_i32x4_extmul_high_i16x8_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_extmul_low_i8x16_s(moonbit_v128_t a,
                                                                   moonbit_v128_t b) {
  return _mm_mullo_epi16(_mm_srai_epi16(_mm_unpacklo_epi8(a, a), 8),
                         _mm_srai_epi16(_mm_unpacklo_epi8(b, b), 8));
}
#define Moonbit_simd_i16x8_extmul_low_i8x16_s(a, b) moonbit_simd_i16x8_extmul_low_i8x16_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_extmul_high_i8x16_s(moonbit_v128_t a,
                                                                    moonbit_v128_t b) {
  return _mm_mullo_epi16(_mm_srai_epi16(_mm_unpackhi_epi8(a, a), 8),
                         _mm_srai_epi16(_mm_unpackhi_epi8(b, b), 8));
}
#define Moonbit_simd_i16x8_extmul_high_i8x16_s(a, b) moonbit_simd_i16x8_extmul_high_i8x16_s((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_extmul_low_i8x16_u(moonbit_v128_t a,
                                                                   moonbit_v128_t b) {
  __m128i z = _mm_setzero_si128();
  return _mm_mullo_epi16(_mm_unpacklo_epi8(a, z), _mm_unpacklo_epi8(b, z));
}
#define Moonbit_simd_i16x8_extmul_low_i8x16_u(a, b) moonbit_simd_i16x8_extmul_low_i8x16_u((a), (b))
static inline moonbit_v128_t moonbit_simd_i16x8_extmul_high_i8x16_u(moonbit_v128_t a,
                                                                    moonbit_v128_t b) {
  __m128i z = _mm_setzero_si128();
  return _mm_mullo_epi16(_mm_unpackhi_epi8(a, z), _mm_unpackhi_epi8(b, z));
}
#define Moonbit_simd_i16x8_extmul_high_i8x16_u(a, b) moonbit_simd_i16x8_extmul_high_i8x16_u((a), (b))
#define Moonbit_simd_i64x2_extmul_low_i32x4_u(a, b) (_mm_mul_epu32(_mm_shuffle_epi32((a), _MM_SHUFFLE(1, 1, 1, 0)), _mm_shuffle_epi32((b), _MM_SHUFFLE(1, 1, 1, 0))))
#define Moonbit_simd_i64x2_extmul_high_i32x4_u(a, b) (_mm_mul_epu32(_mm_shuffle_epi32((a), _MM_SHUFFLE(3, 3, 3, 2)), _mm_shuffle_epi32((b), _MM_SHUFFLE(3, 3, 3, 2))))

/* pairwise extending adds */
static inline moonbit_v128_t moonbit_simd_i16x8_extadd_pairwise_i8x16_s(moonbit_v128_t a) {
  __m128i even = _mm_srai_epi16(_mm_slli_epi16(a, 8), 8);
  __m128i odd = _mm_srai_epi16(a, 8);
  return _mm_add_epi16(even, odd);
}
#define Moonbit_simd_i16x8_extadd_pairwise_i8x16_s(a) moonbit_simd_i16x8_extadd_pairwise_i8x16_s((a))
static inline moonbit_v128_t moonbit_simd_i16x8_extadd_pairwise_i8x16_u(moonbit_v128_t a) {
  __m128i even = _mm_and_si128(a, _mm_set1_epi16(0xff));
  __m128i odd = _mm_srli_epi16(a, 8);
  return _mm_add_epi16(even, odd);
}
#define Moonbit_simd_i16x8_extadd_pairwise_i8x16_u(a) moonbit_simd_i16x8_extadd_pairwise_i8x16_u((a))
static inline moonbit_v128_t moonbit_simd_i32x4_extadd_pairwise_i16x8_u(moonbit_v128_t a) {
  __m128i even = _mm_and_si128(a, _mm_set1_epi32(0xffff));
  __m128i odd = _mm_srli_epi32(a, 16);
  return _mm_add_epi32(even, odd);
}
#define Moonbit_simd_i32x4_extadd_pairwise_i16x8_u(a) moonbit_simd_i32x4_extadd_pairwise_i16x8_u((a))

/* q15mulr: widen to 32-bit products, round, and let packs saturate the
   single -32768 * -32768 overflow lane */
static inline moonbit_v128_t moonbit_simd_i16x8_q15mulr_sat_s(moonbit_v128_t a,
                                                              moonbit_v128_t b) {
  __m128i lo = _mm_mullo_epi16(a, b);
  __m128i hi = _mm_mulhi_epi16(a, b);
  __m128i round = _mm_set1_epi32(0x4000);
  __m128i p_lo = _mm_srai_epi32(_mm_add_epi32(_mm_unpacklo_epi16(lo, hi), round), 15);
  __m128i p_hi = _mm_srai_epi32(_mm_add_epi32(_mm_unpackhi_epi16(lo, hi), round), 15);
  return _mm_packs_epi32(p_lo, p_hi);
}
#define Moonbit_simd_i16x8_q15mulr_sat_s(a, b) moonbit_simd_i16x8_q15mulr_sat_s((a), (b))

/* i64 equality via 32-bit equality of both halves */
static inline moonbit_v128_t moonbit_simd_i64x2_eq(moonbit_v128_t a, moonbit_v128_t b) {
  __m128i t = _mm_cmpeq_epi32(a, b);
  return _mm_and_si128(t, _mm_shuffle_epi32(t, _MM_SHUFFLE(2, 3, 0, 1)));
}
#define Moonbit_simd_i64x2_eq(a, b) moonbit_simd_i64x2_eq((a), (b))
#define Moonbit_simd_i64x2_ne(a, b) (_mm_xor_si128(Moonbit_simd_i64x2_eq((a), (b)), _mm_set1_epi32(-1)))

/* unsigned narrow: clamp into [0, 0xffff] first (the bias below must not
   wrap for inputs close to INT32_MIN), then bias into the signed range
   of packs */
static inline __m128i moonbit_simd_sse2_clamp_u16(__m128i v) {
  __m128i limit = _mm_set1_epi32(0xffff);
  __m128i m;
  v = _mm_andnot_si128(_mm_srai_epi32(v, 31), v);
  m = _mm_cmpgt_epi32(v, limit);
  return _mm_or_si128(_mm_and_si128(m, limit), _mm_andnot_si128(m, v));
}
static inline moonbit_v128_t moonbit_simd_i16x8_narrow_i32x4_u(moonbit_v128_t a,
                                                               moonbit_v128_t b) {
  __m128i bias = _mm_set1_epi32(0x8000);
  return _mm_xor_si128(
      _mm_packs_epi32(_mm_sub_epi32(moonbit_simd_sse2_clamp_u16(a), bias),
                      _mm_sub_epi32(moonbit_simd_sse2_clamp_u16(b), bias)),
      _mm_set1_epi16((short)0x8000));
}
#define Moonbit_simd_i16x8_narrow_i32x4_u(a, b) moonbit_simd_i16x8_narrow_i32x4_u((a), (b))

/* signed trunc_sat: cvtt gives 0x80000000 for NaN and out-of-range lanes;
   flip the positive-overflow lanes to INT32_MAX and zero the NaN lanes */
static inline moonbit_v128_t moonbit_simd_i32x4_trunc_sat_f32x4_s(moonbit_v128_t a) {
  __m128 fa = MOONBIT_SSE_PS(a);
  __m128i res = _mm_cvttps_epi32(fa);
  __m128i too_big =
      MOONBIT_SSE_FROM_PS(_mm_cmpge_ps(fa, _mm_set1_ps(2147483648.0f)));
  __m128i nan_mask = MOONBIT_SSE_FROM_PS(_mm_cmpunord_ps(fa, fa));
  res = _mm_xor_si128(res, too_big);
  return _mm_andnot_si128(nan_mask, res);
}
#define Moonbit_simd_i32x4_trunc_sat_f32x4_s(a) moonbit_simd_i32x4_trunc_sat_f32x4_s((a))
static inline moonbit_v128_t moonbit_simd_i32x4_trunc_sat_f64x2_s_zero(moonbit_v128_t a) {
  __m128d fa = MOONBIT_SSE_PD(a);
  __m128i res = _mm_cvttpd_epi32(fa);
  __m128i big64 =
      MOONBIT_SSE_FROM_PD(_mm_cmpge_pd(fa, _mm_set1_pd(2147483648.0)));
  __m128i nan64 = MOONBIT_SSE_FROM_PD(_mm_cmpunord_pd(fa, fa));
  __m128i low_mask = _mm_set_epi32(0, 0, -1, -1);
  __m128i big32 =
      _mm_and_si128(_mm_shuffle_epi32(big64, _MM_SHUFFLE(3, 3, 2, 0)), low_mask);
  __m128i nan32 =
      _mm_and_si128(_mm_shuffle_epi32(nan64, _MM_SHUFFLE(3, 3, 2, 0)), low_mask);
  res = _mm_xor_si128(res, big32);
  return _mm_andnot_si128(nan32, res);
}
#define Moonbit_simd_i32x4_trunc_sat_f64x2_s_zero(a) moonbit_simd_i32x4_trunc_sat_f64x2_s_zero((a))

/* extending and splatting loads */
static inline moonbit_v128_t moonbit_simd_v128_load8x8_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  __m128i t = _mm_loadl_epi64((const __m128i *)p);
  return _mm_srai_epi16(_mm_unpacklo_epi8(t, t), 8);
}
#define Moonbit_simd_v128_load8x8_s(p0, offset) moonbit_simd_v128_load8x8_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load8x8_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)p), _mm_setzero_si128());
}
#define Moonbit_simd_v128_load8x8_u(p0, offset) moonbit_simd_v128_load8x8_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load16x4_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  __m128i t = _mm_loadl_epi64((const __m128i *)p);
  return _mm_srai_epi32(_mm_unpacklo_epi16(t, t), 16);
}
#define Moonbit_simd_v128_load16x4_s(p0, offset) moonbit_simd_v128_load16x4_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load16x4_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_unpacklo_epi16(_mm_loadl_epi64((const __m128i *)p), _mm_setzero_si128());
}
#define Moonbit_simd_v128_load16x4_u(p0, offset) moonbit_simd_v128_load16x4_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32x2_s(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  __m128i t = _mm_loadl_epi64((const __m128i *)p);
  return _mm_unpacklo_epi32(t, _mm_srai_epi32(t, 31));
}
#define Moonbit_simd_v128_load32x2_s(p0, offset) moonbit_simd_v128_load32x2_s((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32x2_u(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_unpacklo_epi32(_mm_loadl_epi64((const __m128i *)p), _mm_setzero_si128());
}
#define Moonbit_simd_v128_load32x2_u(p0, offset) moonbit_simd_v128_load32x2_u((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load8_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_set1_epi8((char)p[0]);
}
#define Moonbit_simd_v128_load8_splat(p0, offset) moonbit_simd_v128_load8_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load16_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_set1_epi16((short)READ_UINT16_LE(p));
}
#define Moonbit_simd_v128_load16_splat(p0, offset) moonbit_simd_v128_load16_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_set1_epi32((int)READ_UINT32_LE(p));
}
#define Moonbit_simd_v128_load32_splat(p0, offset) moonbit_simd_v128_load32_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load64_splat(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_set1_epi64x((long long)READ_UINT64_LE(p));
}
#define Moonbit_simd_v128_load64_splat(p0, offset) moonbit_simd_v128_load64_splat((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load32_zero(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_cvtsi32_si128((int)READ_UINT32_LE(p));
}
#define Moonbit_simd_v128_load32_zero(p0, offset) moonbit_simd_v128_load32_zero((p0), (offset))
static inline moonbit_v128_t moonbit_simd_v128_load64_zero(const uint8_t *p0, int32_t offset) {
  const uint8_t *p = p0 + offset;
  return _mm_loadl_epi64((const __m128i *)p);
}
#define Moonbit_simd_v128_load64_zero(p0, offset) moonbit_simd_v128_load64_zero((p0), (offset))


/* Lane accesses: same contract as the NEON macros — the lane is always
   a literal at the call site, and arguments are simple variables or
   constants, so multiple expansion is safe. */
static inline uint32_t moonbit_simd_f32_to_bits(float x) {
  union { float f; uint32_t u; } u;
  u.f = x;
  return u.u;
}
static inline uint64_t moonbit_simd_f64_to_bits(double x) {
  union { double f; uint64_t u; } u;
  u.f = x;
  return u.u;
}
#define Moonbit_simd_i16x8_extract_lane_u(v, lane) \
  ((uint32_t)(uint16_t)_mm_extract_epi16((v), (lane)))
#define Moonbit_simd_i16x8_extract_lane_s(v, lane) \
  ((int32_t)(int16_t)_mm_extract_epi16((v), (lane)))
#define Moonbit_simd_i8x16_extract_lane_u(v, lane) \
  (((uint32_t)_mm_extract_epi16((v), (lane) >> 1) >> (((lane) & 1) * 8)) & 0xff)
#define Moonbit_simd_i8x16_extract_lane_s(v, lane) \
  ((int32_t)(int8_t)Moonbit_simd_i8x16_extract_lane_u((v), (lane)))
#define Moonbit_simd_i32x4_extract_lane(v, lane) \
  ((lane) == 0 \
       ? (uint32_t)_mm_cvtsi128_si32((v)) \
       : (uint32_t)_mm_cvtsi128_si32( \
             _mm_shuffle_epi32((v), _MM_SHUFFLE((lane), (lane), (lane), (lane)))))
#define Moonbit_simd_i64x2_extract_lane(v, lane) \
  ((lane) == 0 ? (uint64_t)_mm_cvtsi128_si64((v)) \
               : (uint64_t)_mm_cvtsi128_si64(_mm_unpackhi_epi64((v), (v))))
#define Moonbit_simd_f32x4_extract_lane(v, lane) \
  ((lane) == 0 ? _mm_cvtss_f32(_mm_castsi128_ps((v))) \
               : _mm_cvtss_f32(_mm_castsi128_ps(_mm_shuffle_epi32( \
                     (v), _MM_SHUFFLE((lane), (lane), (lane), (lane))))))
#define Moonbit_simd_f64x2_extract_lane(v, lane) \
  ((lane) == 0 ? _mm_cvtsd_f64(_mm_castsi128_pd((v))) \
               : _mm_cvtsd_f64(_mm_castsi128_pd(_mm_unpackhi_epi64((v), (v)))))
#define Moonbit_simd_i16x8_replace_lane(v, x, lane) \
  (_mm_insert_epi16((v), (int)(x), (lane)))
#define Moonbit_simd_i8x16_replace_lane(v, x, lane) \
  (_mm_insert_epi16( \
      (v), \
      (((lane) & 1) \
           ? ((_mm_extract_epi16((v), (lane) >> 1) & 0x00ff) | (((int)(x) & 0xff) << 8)) \
           : ((_mm_extract_epi16((v), (lane) >> 1) & 0xff00) | ((int)(x) & 0xff))), \
      (lane) >> 1))
#define Moonbit_simd_i32x4_replace_lane(v, x, lane) \
  (_mm_insert_epi16(_mm_insert_epi16((v), (int)((uint32_t)(x) & 0xffff), (lane) * 2), \
                    (int)((uint32_t)(x) >> 16), (lane) * 2 + 1))
#define Moonbit_simd_i64x2_replace_lane(v, x, lane) \
  ((lane) == 0 ? _mm_unpacklo_epi64(_mm_cvtsi64_si128((int64_t)(uint64_t)(x)), \
                                    _mm_unpackhi_epi64((v), (v))) \
               : _mm_unpacklo_epi64((v), _mm_cvtsi64_si128((int64_t)(uint64_t)(x))))
#define Moonbit_simd_f32x4_replace_lane(v, x, lane) \
  Moonbit_simd_i32x4_replace_lane((v), moonbit_simd_f32_to_bits((x)), (lane))
#define Moonbit_simd_f64x2_replace_lane(v, x, lane) \
  Moonbit_simd_i64x2_replace_lane((v), moonbit_simd_f64_to_bits((x)), (lane))

#ifdef __SSSE3__
/* pshufb zeroes a lane when the index MSB is set; saturating-add 0x70
   turns any index >= 16 into >= 0x80, which matches wasm swizzle */
#define Moonbit_simd_i8x16_swizzle(a, s) (_mm_shuffle_epi8((a), _mm_adds_epu8((s), _mm_set1_epi8(0x70))))
static inline moonbit_v128_t moonbit_simd_i8x16_shuffle(
    moonbit_v128_t a, moonbit_v128_t b, int l0, int l1, int l2, int l3, int l4,
    int l5, int l6, int l7, int l8, int l9, int l10, int l11, int l12, int l13,
    int l14, int l15) {
  uint8_t idx[16];
  __m128i vidx, lo, hi;
  idx[0] = (uint8_t)l0; idx[1] = (uint8_t)l1; idx[2] = (uint8_t)l2;
  idx[3] = (uint8_t)l3; idx[4] = (uint8_t)l4; idx[5] = (uint8_t)l5;
  idx[6] = (uint8_t)l6; idx[7] = (uint8_t)l7; idx[8] = (uint8_t)l8;
  idx[9] = (uint8_t)l9; idx[10] = (uint8_t)l10; idx[11] = (uint8_t)l11;
  idx[12] = (uint8_t)l12; idx[13] = (uint8_t)l13; idx[14] = (uint8_t)l14;
  idx[15] = (uint8_t)l15;
  vidx = _mm_loadu_si128((const __m128i *)idx);
  lo = _mm_shuffle_epi8(a, _mm_adds_epu8(vidx, _mm_set1_epi8(0x70)));
  hi = _mm_shuffle_epi8(
      b, _mm_adds_epu8(_mm_sub_epi8(vidx, _mm_set1_epi8(16)),
                       _mm_set1_epi8(0x70)));
  return _mm_or_si128(lo, hi);
}
#define Moonbit_simd_i8x16_shuffle(a, b, l0, l1, l2, l3, l4, l5, l6, l7, l8, l9, l10, l11, l12, l13, l14, l15) moonbit_simd_i8x16_shuffle((a), (b), (l0), (l1), (l2), (l3), (l4), (l5), (l6), (l7), (l8), (l9), (l10), (l11), (l12), (l13), (l14), (l15))
#endif /* __SSSE3__ */


#endif /* MOONBIT_V128_NEON / MOONBIT_V128_SSE2 */

/* Memory lane accesses and zero-extending loads compose from the lane
   and splat fast paths; like those, the lane immediate is always a
   literal at the call site. */
#if defined(Moonbit_simd_i8x16_replace_lane) && !defined(Moonbit_simd_v128_load8_lane)
#define Moonbit_simd_v128_load8_lane(p, off, v, lane) \
  Moonbit_simd_i8x16_replace_lane((v), (uint32_t)(p)[(off)], (lane))
#endif
#if defined(Moonbit_simd_i16x8_replace_lane) && !defined(Moonbit_simd_v128_load16_lane)
#define Moonbit_simd_v128_load16_lane(p, off, v, lane) \
  Moonbit_simd_i16x8_replace_lane((v), READ_UINT16_LE((p) + (off)), (lane))
#endif
#if defined(Moonbit_simd_i32x4_replace_lane) && !defined(Moonbit_simd_v128_load32_lane)
#define Moonbit_simd_v128_load32_lane(p, off, v, lane) \
  Moonbit_simd_i32x4_replace_lane((v), READ_UINT32_LE((p) + (off)), (lane))
#endif
#if defined(Moonbit_simd_i64x2_replace_lane) && !defined(Moonbit_simd_v128_load64_lane)
#define Moonbit_simd_v128_load64_lane(p, off, v, lane) \
  Moonbit_simd_i64x2_replace_lane((v), READ_UINT64_LE((p) + (off)), (lane))
#endif
#if defined(Moonbit_simd_i8x16_extract_lane_u) && !defined(Moonbit_simd_v128_store8_lane)
#define Moonbit_simd_v128_store8_lane(p, off, v, lane) \
  ((p)[(off)] = (uint8_t)Moonbit_simd_i8x16_extract_lane_u((v), (lane)), 0)
#endif
#if defined(Moonbit_simd_i16x8_extract_lane_u) && !defined(Moonbit_simd_v128_store16_lane)
#define Moonbit_simd_v128_store16_lane(p, off, v, lane) \
  (WRITE_UINT16_LE((p) + (off), (uint16_t)Moonbit_simd_i16x8_extract_lane_u((v), (lane))), 0)
#endif
#if defined(Moonbit_simd_i32x4_extract_lane) && !defined(Moonbit_simd_v128_store32_lane)
#define Moonbit_simd_v128_store32_lane(p, off, v, lane) \
  (WRITE_UINT32_LE((p) + (off), Moonbit_simd_i32x4_extract_lane((v), (lane))), 0)
#endif
#if defined(Moonbit_simd_i64x2_extract_lane) && !defined(Moonbit_simd_v128_store64_lane)
#define Moonbit_simd_v128_store64_lane(p, off, v, lane) \
  (WRITE_UINT64_LE((p) + (off), Moonbit_simd_i64x2_extract_lane((v), (lane))), 0)
#endif
#if defined(Moonbit_simd_i32x4_splat) && defined(Moonbit_simd_i32x4_replace_lane) \
    && !defined(Moonbit_simd_v128_load32_zero)
#define Moonbit_simd_v128_load32_zero(p, off) \
  Moonbit_simd_i32x4_replace_lane(Moonbit_simd_i32x4_splat(0), READ_UINT32_LE((p) + (off)), 0)
#endif
#if defined(Moonbit_simd_i64x2_splat) && defined(Moonbit_simd_i64x2_replace_lane) \
    && !defined(Moonbit_simd_v128_load64_zero)
#define Moonbit_simd_v128_load64_zero(p, off) \
  Moonbit_simd_i64x2_replace_lane(Moonbit_simd_i64x2_splat(0), READ_UINT64_LE((p) + (off)), 0)
#endif

/* relaxed instructions may behave like their strict counterparts, so
   reuse those fast paths when present; otherwise the compiler binds the
   relaxed op to its own MoonBit fallback */
#if defined(Moonbit_simd_v128_bitselect) && !defined(Moonbit_simd_i8x16_relaxed_laneselect)
#define Moonbit_simd_i8x16_relaxed_laneselect(a, b, m) Moonbit_simd_v128_bitselect((a), (b), (m))
#endif
#if defined(Moonbit_simd_v128_bitselect) && !defined(Moonbit_simd_i16x8_relaxed_laneselect)
#define Moonbit_simd_i16x8_relaxed_laneselect(a, b, m) Moonbit_simd_v128_bitselect((a), (b), (m))
#endif
#if defined(Moonbit_simd_v128_bitselect) && !defined(Moonbit_simd_i32x4_relaxed_laneselect)
#define Moonbit_simd_i32x4_relaxed_laneselect(a, b, m) Moonbit_simd_v128_bitselect((a), (b), (m))
#endif
#if defined(Moonbit_simd_v128_bitselect) && !defined(Moonbit_simd_i64x2_relaxed_laneselect)
#define Moonbit_simd_i64x2_relaxed_laneselect(a, b, m) Moonbit_simd_v128_bitselect((a), (b), (m))
#endif
#if defined(Moonbit_simd_i8x16_swizzle) && !defined(Moonbit_simd_i8x16_relaxed_swizzle)
#define Moonbit_simd_i8x16_relaxed_swizzle(a, s) Moonbit_simd_i8x16_swizzle((a), (s))
#endif
#if defined(Moonbit_simd_i16x8_q15mulr_sat_s) && !defined(Moonbit_simd_i16x8_relaxed_q15mulr_s)
#define Moonbit_simd_i16x8_relaxed_q15mulr_s(a, b) Moonbit_simd_i16x8_q15mulr_sat_s((a), (b))
#endif
#if defined(Moonbit_simd_f32x4_min) && !defined(Moonbit_simd_f32x4_relaxed_min)
#define Moonbit_simd_f32x4_relaxed_min(a, b) Moonbit_simd_f32x4_min((a), (b))
#endif
#if defined(Moonbit_simd_f32x4_max) && !defined(Moonbit_simd_f32x4_relaxed_max)
#define Moonbit_simd_f32x4_relaxed_max(a, b) Moonbit_simd_f32x4_max((a), (b))
#endif
#if defined(Moonbit_simd_f64x2_min) && !defined(Moonbit_simd_f64x2_relaxed_min)
#define Moonbit_simd_f64x2_relaxed_min(a, b) Moonbit_simd_f64x2_min((a), (b))
#endif
#if defined(Moonbit_simd_f64x2_max) && !defined(Moonbit_simd_f64x2_relaxed_max)
#define Moonbit_simd_f64x2_relaxed_max(a, b) Moonbit_simd_f64x2_max((a), (b))
#endif
#if defined(Moonbit_simd_i32x4_trunc_sat_f32x4_s) && !defined(Moonbit_simd_i32x4_relaxed_trunc_f32x4_s)
#define Moonbit_simd_i32x4_relaxed_trunc_f32x4_s(a) Moonbit_simd_i32x4_trunc_sat_f32x4_s((a))
#endif
#if defined(Moonbit_simd_i32x4_trunc_sat_f32x4_u) && !defined(Moonbit_simd_i32x4_relaxed_trunc_f32x4_u)
#define Moonbit_simd_i32x4_relaxed_trunc_f32x4_u(a) Moonbit_simd_i32x4_trunc_sat_f32x4_u((a))
#endif
#if defined(Moonbit_simd_i32x4_trunc_sat_f64x2_s_zero) && !defined(Moonbit_simd_i32x4_relaxed_trunc_f64x2_s_zero)
#define Moonbit_simd_i32x4_relaxed_trunc_f64x2_s_zero(a) Moonbit_simd_i32x4_trunc_sat_f64x2_s_zero((a))
#endif
#if defined(Moonbit_simd_i32x4_trunc_sat_f64x2_u_zero) && !defined(Moonbit_simd_i32x4_relaxed_trunc_f64x2_u_zero)
#define Moonbit_simd_i32x4_relaxed_trunc_f64x2_u_zero(a) Moonbit_simd_i32x4_trunc_sat_f64x2_u_zero((a))
#endif

#ifdef __cplusplus
}
#endif

#endif // moonbit_simd_h_INCLUDED
