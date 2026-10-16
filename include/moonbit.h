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

// =====================================
// WARNING: very unstable API, for internal use only
// =====================================

#ifndef moonbit_h_INCLUDED
#define moonbit_h_INCLUDED

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
#include "moonbit-fundamental.h"
#else
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#if defined (_WIN32) || defined (_WIN64)
#ifdef MOONBIT_BUILD_RUNTIME
#define MOONBIT_EXPORT __declspec(dllexport)
#else
#ifdef MOONBIT_USE_SHARED_RUNTIME
#define MOONBIT_EXPORT __declspec(dllimport)
#else
#define MOONBIT_EXPORT
#endif
#endif
#define MOONBIT_FFI_EXPORT __declspec(dllexport)
#else
#define MOONBIT_EXPORT // __attribute__ ((visibility("default")))
#define MOONBIT_FFI_EXPORT
#endif

enum moonbit_block_kind {
  // 0 => regular block or external object; meta discriminates
  moonbit_BLOCK_KIND_REGULAR = 0,
  // 1 => array of immediate value/string/bytes
  moonbit_BLOCK_KIND_VAL_ARRAY = 1,
  // 2 => array of pointers
  moonbit_BLOCK_KIND_REF_ARRAY = 2,
  // 3 => array of inline value types with reference fields
  moonbit_BLOCK_KIND_REF_VALTYPE_ARRAY = 3
};

struct moonbit_object {
  int32_t rc;
  /* The layout of the 32-bit rc word:

     bits 0..1:   enum moonbit_block_kind kind
     bits 2..3:   enum moonbit_cycle_status status
     bit 4:       whether the object is in the possible-root buffer
     bits 5..31:  signed 27-bit reference count

     The layout of 32-bit meta data (starting from most significant bit):

     union {
       // when [kind = BLOCK_KIND_REGULAR]
       struct {
         unsigned int layout_class : 2;
         unsigned int layout_index : 22;
         // For blocks, we steal 8 bits from the length to represent enum tag
         unsigned int tag : 8;
       };
       // when [kind = BLOCK_KIND_REF_ARRAY], [kind = BLOCK_KIND_VAL_ARRAY],
       // or [kind = BLOCK_KIND_REF_VALTYPE_ARRAY]
       uint32_t len;
       // when [kind = BLOCK_KIND_REGULAR] and meta layout class is external
       uint32_t size : 30;
     };
  */
  uint32_t meta;
};

#define MOONBIT_RC_KIND_MASK ((uint32_t)3)
#define MOONBIT_RC_STOLEN_BITS_MASK ((uint32_t)31)
#define MOONBIT_RC_COUNT_SHIFT 5
#define MOONBIT_RC_COUNT_VALUE_MASK (((uint32_t)1 << 27) - 1)
// The count bits are decoded with an arithmetic right shift, so dynamic
// positive counts and suspended-drop progress must stay below the sign bit.
// Pass -DMOONBIT_RC_BOUND_CHECK=1 to enable the hot-path increment overflow
// check. Bulk setters always validate before writing the signed RC payload.
#define MOONBIT_RC_DYNAMIC_COUNT_MAX (((uint32_t)1 << 26) - 1)
#define MOONBIT_REGULAR_LAYOUT_CLASS_SHIFT 30
#define MOONBIT_REGULAR_LAYOUT_CLASS_MASK ((uint32_t)3)
#define MOONBIT_REGULAR_LAYOUT_INDEX_SHIFT 8
#define MOONBIT_REGULAR_LAYOUT_INDEX_MASK (((uint32_t)1 << 22) - 1)
#define MOONBIT_REGULAR_TAG_MASK 0xFFu
#define MOONBIT_STATIC_RC_COUNT MOONBIT_RC_COUNT_VALUE_MASK

enum moonbit_regular_layout_class {
  MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR = 0,
  // An indexed layout: [size_in_word; child_count; tagged_child...]. Each tagged
  // child is [(payload_word_offset << 1) | is_venum]: a direct GC reference when
  // is_venum is 0, or an embedded value enum (scanned by dispatching on its own
  // tag-dependent header) when is_venum is 1. Static refs and embedded value
  // enums therefore share one child list and one layout class.
  MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED = 1,
  MOONBIT_REGULAR_LAYOUT_CLASS_EXTERNAL = 2
};

// Only value-type arrays whose elements contain references carry this extra
// header before the normal array header. All-scalar value-type arrays are
// represented as ordinary all-scalar arrays (moonbit_BLOCK_KIND_VAL_ARRAY).
// For reference-containing value types, `elem_header` stores the element layout
// metadata used by drop scanning.
struct moonbit_valtype_array_header {
  uint32_t elem_header;
  uint32_t padding;
  struct moonbit_object array_header;
};

#define Moonbit_object_header(obj) ((struct moonbit_object *)(obj) - 1)
#define Moonbit_valtype_header(obj)                                            \
  ((struct moonbit_valtype_array_header *)(obj) - 1)

// The count field occupies the upper 27 bits of rc. Arithmetic-shifting right
// by the five low bookkeeping bits sign-extends the count, so the all-ones
// static/immortal count decodes as -1 while normal dynamic counts remain
// positive.
#define Moonbit_rc_count(header)                                               \
  (((int32_t)(header)->rc) >> MOONBIT_RC_COUNT_SHIFT)

#define Moonbit_object_kind(obj) \
  ((enum moonbit_block_kind)(Moonbit_object_header(obj)->rc & MOONBIT_RC_KIND_MASK))

#define Moonbit_object_tag(obj)                                                \
  ((Moonbit_object_header(obj)->meta) & MOONBIT_REGULAR_TAG_MASK)

#define Moonbit_array_length(obj)                                              \
  ((int32_t)((Moonbit_object_header(obj))->meta))

MOONBIT_EXPORT void *libc_malloc(size_t size);
MOONBIT_EXPORT void libc_free(void *ptr);
MOONBIT_EXPORT void *moonbit_malloc(size_t size);
MOONBIT_EXPORT void moonbit_incref(void *obj);
MOONBIT_EXPORT void moonbit_decref(void *obj);
// incref/decref the live references of a value enum (tagged union) whose first
// word is a self-describing object header (the constructor's layout). [p] points
// at the value enum value (its tag/header word); the payload follows tag+size.
MOONBIT_EXPORT void moonbit_incref_value_enum(void *p);
MOONBIT_EXPORT void moonbit_decref_value_enum(void *p);

typedef uint16_t *moonbit_string_t;
typedef uint8_t *moonbit_bytes_t;

typedef struct {
  uint64_t lo;
  uint64_t hi;
} moonbit_v128_storage_t;

#if !defined(MOONBIT_NATIVE_NO_SYS_HEADER) && (defined(__aarch64__) || defined(__ARM_NEON))
#include <arm_neon.h>
typedef uint8x16_t moonbit_v128_t;
#define MOONBIT_V128_NEON 1
#elif !defined(MOONBIT_NATIVE_NO_SYS_HEADER) && defined(__SSE2__)
#include <emmintrin.h>
typedef __m128i moonbit_v128_t;
#define MOONBIT_V128_SSE2 1
#else
typedef moonbit_v128_storage_t moonbit_v128_t;
#endif

MOONBIT_EXPORT moonbit_string_t moonbit_make_string(int32_t size, uint16_t value);
MOONBIT_EXPORT moonbit_string_t moonbit_make_string_raw(int32_t size);
MOONBIT_EXPORT moonbit_bytes_t moonbit_make_bytes(int32_t size, int value);
MOONBIT_EXPORT moonbit_bytes_t moonbit_make_bytes_raw(int32_t size);
MOONBIT_EXPORT int32_t *moonbit_make_int32_array(int32_t len, int32_t value);
MOONBIT_EXPORT int32_t *moonbit_make_int32_array_raw(int32_t len);
MOONBIT_EXPORT void **moonbit_make_ref_array(int32_t len, void *value);
MOONBIT_EXPORT void **moonbit_make_ref_array_raw(int32_t len);
MOONBIT_EXPORT int64_t *moonbit_make_int64_array(int32_t len, int64_t value);
MOONBIT_EXPORT int64_t *moonbit_make_int64_array_raw(int32_t len);
MOONBIT_EXPORT double *moonbit_make_double_array(int32_t len, double value);
MOONBIT_EXPORT double *moonbit_make_double_array_raw(int32_t len);
MOONBIT_EXPORT float *moonbit_make_float_array(int32_t len, float value);
MOONBIT_EXPORT float *moonbit_make_float_array_raw(int32_t len);
MOONBIT_EXPORT void **moonbit_make_extern_ref_array(int32_t len, void *value);
MOONBIT_EXPORT void **moonbit_make_extern_ref_array_raw(int32_t len);
MOONBIT_EXPORT moonbit_v128_storage_t *
moonbit_make_v128_array(int32_t len, uint64_t lo, uint64_t hi);
MOONBIT_EXPORT moonbit_v128_storage_t *moonbit_make_v128_array_raw(int32_t len);
MOONBIT_EXPORT void *moonbit_make_scalar_valtype_array(int32_t len, size_t valtype_size, void *init);
MOONBIT_EXPORT void *moonbit_make_ref_valtype_array(int32_t len, size_t valtype_size, uint32_t header, void *init);
MOONBIT_EXPORT void *moonbit_make_scalar_valtype_array_raw(int32_t len, size_t valtype_size);
MOONBIT_EXPORT void *moonbit_make_ref_valtype_array_raw(int32_t len, size_t valtype_size, uint32_t header);
MOONBIT_EXPORT void **moonbit_make_ref_array_with_blit(int32_t allocate_len, void *value, void *src, int32_t src_offset, int32_t dst_offset, int32_t len);

/* `finalize` should drop the payload of the external object.
   `finalize` MUST NOT drop the [moonbit_external_object] container itself.

   `payload_size` is the size of payload, excluding [drop].

   The returned pointer points directly to the start of user payload.
   The finalizer pointer would be stored at the end of the object, after user payload.
*/
MOONBIT_EXPORT void *moonbit_make_external_object(
  void (*finalize)(void *self),
  uint32_t payload_size
);

MOONBIT_EXPORT extern uint8_t* const moonbit_empty_int8_array;
MOONBIT_EXPORT extern uint16_t* const moonbit_empty_int16_array;
MOONBIT_EXPORT extern int32_t* const moonbit_empty_int32_array;
MOONBIT_EXPORT extern int64_t* const moonbit_empty_int64_array;
MOONBIT_EXPORT extern float*   const moonbit_empty_float_array;
MOONBIT_EXPORT extern double*  const moonbit_empty_double_array;
MOONBIT_EXPORT extern moonbit_v128_storage_t* const moonbit_empty_v128_array;
MOONBIT_EXPORT extern void**   const moonbit_empty_ref_array;
MOONBIT_EXPORT extern void**   const moonbit_empty_extern_ref_array;
MOONBIT_EXPORT extern void* const moonbit_empty_scalar_valtype_array;
MOONBIT_EXPORT extern void* const moonbit_empty_ref_valtype_array;

#ifdef __cplusplus
}
#endif

#endif // moonbit_h_INCLUDED
