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

// Extract the 2-bit layout class from an object's header word. Shared so the
// value-enum retain/release fast path (is-this-variant-INDEXED?) can be inlined
// at call sites (runtime_core.c) while the reference-walking loop stays out of
// line (runtime.c).
#define Moonbit_header_layout_class(header)                                     \
  (((header) >> MOONBIT_REGULAR_LAYOUT_CLASS_SHIFT) &                           \
   MOONBIT_REGULAR_LAYOUT_CLASS_MASK)

#define Moonbit_make_rc_header(kind, count)                                    \
  ((int32_t)((((uint32_t)(count) & MOONBIT_RC_COUNT_VALUE_MASK)               \
              << MOONBIT_RC_COUNT_SHIFT) |                                    \
             ((uint32_t)(kind) & MOONBIT_RC_KIND_MASK)))

#define Moonbit_make_dynamic_rc(kind) Moonbit_make_rc_header((kind), 1)
#define Moonbit_make_static_rc(kind)                                           \
  Moonbit_make_rc_header((kind), MOONBIT_STATIC_RC_COUNT)

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

#ifdef MOONBIT_RC_BOUND_CHECK
#define Moonbit_check_rc_count_for_increase(header)                            \
  do {                                                                         \
    if (Moonbit_rc_count(header) >= (int32_t)MOONBIT_RC_DYNAMIC_COUNT_MAX) {   \
      moonbit_panic();                                                         \
    }                                                                          \
  } while (0)
#else
#define Moonbit_check_rc_count_for_increase(header) ((void)0)
#endif

#define Moonbit_check_dynamic_rc_count(count)                                  \
  do {                                                                         \
    uint32_t moonbit_checked_count_ = (uint32_t)(count);                       \
    if (moonbit_checked_count_ > MOONBIT_RC_DYNAMIC_COUNT_MAX) {               \
      moonbit_panic();                                                         \
    }                                                                          \
  } while (0)

#define Moonbit_set_rc_count(header, count)                                    \
  do {                                                                         \
    struct moonbit_object *moonbit_header_ = (header);                         \
    uint32_t moonbit_count_ = (uint32_t)(count);                               \
    Moonbit_check_dynamic_rc_count(moonbit_count_);                            \
    moonbit_header_->rc =                                                      \
      (int32_t)((((uint32_t)moonbit_header_->rc) &                            \
                 MOONBIT_RC_STOLEN_BITS_MASK) |                               \
                (moonbit_count_ << MOONBIT_RC_COUNT_SHIFT));                   \
  } while (0)

#define Moonbit_increase_rc_count(header)                                      \
  do {                                                                         \
    struct moonbit_object *moonbit_header_ = (header);                         \
    Moonbit_check_rc_count_for_increase(moonbit_header_);                      \
    moonbit_header_->rc += (int32_t)(1u << MOONBIT_RC_COUNT_SHIFT);            \
  } while (0)

#define Moonbit_decrease_rc_count(header)                                      \
  do {                                                                         \
    (header)->rc -= (int32_t)(1u << MOONBIT_RC_COUNT_SHIFT);                   \
  } while (0)

#define Moonbit_init_dynamic_rc(header, kind)                                  \
  do {                                                                         \
    (header)->rc = Moonbit_make_dynamic_rc(kind);                              \
  } while (0)

#define Moonbit_meta(header) ((header)->meta)

#define Moonbit_set_meta(header, value)                                        \
  do {                                                                         \
    (header)->meta = (value);                                                  \
  } while (0)

#define Moonbit_rc_kind(header)                                                \
  ((enum moonbit_block_kind)(((uint32_t)(header)->rc) &                       \
                             MOONBIT_RC_KIND_MASK))

#define Moonbit_object_kind(obj) Moonbit_rc_kind(Moonbit_object_header(obj))

#define Moonbit_tag_from_header(header)                                        \
  ((header) & MOONBIT_REGULAR_TAG_MASK)

#define Moonbit_object_tag(obj)                                                \
  Moonbit_tag_from_header(Moonbit_meta(Moonbit_object_header(obj)))

#define Moonbit_array_length(obj)                                              \
  ((int32_t)Moonbit_meta(Moonbit_object_header(obj)))

#define Moonbit_make_regular_object_header(layout_class, layout_index, tag)    \
  ((((uint32_t)(layout_class) & MOONBIT_REGULAR_LAYOUT_CLASS_MASK)             \
    << MOONBIT_REGULAR_LAYOUT_CLASS_SHIFT) |                                  \
   (((uint32_t)(layout_index) & MOONBIT_REGULAR_LAYOUT_INDEX_MASK)             \
    << MOONBIT_REGULAR_LAYOUT_INDEX_SHIFT) |                                  \
   ((tag) & MOONBIT_REGULAR_TAG_MASK))

/* The two-bit cycle status is interpreted together with the in-root flag:

   ACYCLIC_OR_CANDIDATE
     Without the in-root flag, the object is proven acyclic and never
     participates in trial deletion. With the flag, it is a possible cycle
     root waiting to be examined.

   PENDING
     The object is in the candidate subgraph and its internal incoming
     references have been temporarily subtracted.

   LIVE
     The object is known to be live or has been restored because trial deletion
     found an external reference. With the in-root flag, its stale root entry
     still needs to be removed.

   DEAD
     Trial deletion found no external reference, so the object belongs to the
     garbage subgraph selected for collection. */
enum moonbit_cycle_status {
  moonbit_CYCLE_STATUS_ACYCLIC_OR_CANDIDATE = 0,
  moonbit_CYCLE_STATUS_PENDING = 1,
  moonbit_CYCLE_STATUS_LIVE = 2,
  moonbit_CYCLE_STATUS_DEAD = 3,
};

#define MOONBIT_CYCLE_STATUS_SHIFT 2
#define MOONBIT_CYCLE_STATUS_MASK (0x3u << MOONBIT_CYCLE_STATUS_SHIFT)
#define MOONBIT_OBJ_IN_ROOT_SHIFT 4
#define MOONBIT_OBJ_IN_ROOT_MASK (0x1u << MOONBIT_OBJ_IN_ROOT_SHIFT)

static inline void set_cycle_status(void *ptr,
                                    enum moonbit_cycle_status status) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  uint32_t rc = (uint32_t)header->rc;
  rc = (rc & ~MOONBIT_CYCLE_STATUS_MASK) |
       ((uint32_t)status << MOONBIT_CYCLE_STATUS_SHIFT);
  header->rc = (int32_t)rc;
}

static inline void set_cycle_capable(void *ptr) {
  // Empty arrays may be represented by shared static singletons. Their headers
  // must remain immutable regardless of the array type at a particular use.
  if (Moonbit_rc_count(Moonbit_object_header(ptr)) < 0)
    return;
  set_cycle_status(ptr, moonbit_CYCLE_STATUS_LIVE);
}

static inline void set_in_root(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  header->rc =
      (int32_t)((uint32_t)header->rc | MOONBIT_OBJ_IN_ROOT_MASK);
}

static inline void clear_in_root(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  header->rc =
      (int32_t)((uint32_t)header->rc & ~MOONBIT_OBJ_IN_ROOT_MASK);
}

#define MOONBIT_IN_ROOT(obj)                                                   \
  ((((uint32_t)Moonbit_object_header(obj)->rc) & MOONBIT_OBJ_IN_ROOT_MASK) !=  \
   0)
#define MOONBIT_CYCLE_STATUS(obj)                                              \
  ((((uint32_t)Moonbit_object_header(obj)->rc) &                              \
    MOONBIT_CYCLE_STATUS_MASK) >>                                             \
   MOONBIT_CYCLE_STATUS_SHIFT)
#define MOONBIT_CHECK_CYCLE_STATUS(obj, status)                               \
  (MOONBIT_CYCLE_STATUS(obj) == (status))

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
// Like moonbit_update_ref_valtype_rc but for value-enum array elements.
MOONBIT_EXPORT void moonbit_update_ref_enum_valtype_rc(int32_t len, void *value);

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

MOONBIT_EXPORT void moonbit_update_ref_valtype_rc(int32_t len, void *value, uint32_t header);

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

// ----------------------------------------------------------------------------
// byte-swap and extraction functions
// ----------------------------------------------------------------------------

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#if __has_builtin(__builtin_bswap64)
#  define BSWAP64(x) __builtin_bswap64((uint64_t)(x))
#  define BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#elif defined(_MSC_VER)
#  include <stdlib.h>
#  define BSWAP64(x) _byteswap_uint64((uint64_t)(x))
#  define BSWAP32(x) _byteswap_ulong((uint32_t)(x))
#else
#  define BSWAP64(x) ( \
      (((uint64_t)(x) & 0x00000000000000FFULL) << 56) | \
      (((uint64_t)(x) & 0x000000000000FF00ULL) << 40) | \
      (((uint64_t)(x) & 0x0000000000FF0000ULL) << 24) | \
      (((uint64_t)(x) & 0x00000000FF000000ULL) <<  8) | \
      (((uint64_t)(x) & 0x000000FF00000000ULL) >>  8) | \
      (((uint64_t)(x) & 0x0000FF0000000000ULL) >> 24) | \
      (((uint64_t)(x) & 0x00FF000000000000ULL) >> 40) | \
      (((uint64_t)(x) & 0xFF00000000000000ULL) >> 56))
#  define BSWAP32(x) ( \
      (((uint32_t)(x) & 0x000000FFU) << 24) | \
      (((uint32_t)(x) & 0x0000FF00U) <<  8) | \
      (((uint32_t)(x) & 0x00FF0000U) >>  8) | \
      (((uint32_t)(x) & 0xFF000000U) >> 24))
#endif

#define BSWAP16(x) ( \
  (((uint16_t)(x) & 0x00FFU) << 8) | \
  (((uint16_t)(x) & 0xFF00U) >> 8))

// Detect if we're on x86 or ARMv6+
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) || \
    defined(__i386__) || defined(_M_IX86) || \
    defined(__aarch64__) || defined(_M_ARM64) || \
    (defined(__ARM_ARCH) && __ARM_ARCH >= 6)
    #define SUPPORTS_UNALIGNED_ACCESS 1
#else
    #define SUPPORTS_UNALIGNED_ACCESS 0
#endif  

#if SUPPORTS_UNALIGNED_ACCESS
    // Fast path: direct memory access
    #define READ_UINT64(ptr) (*(const uint64_t*)(ptr))
    #define READ_UINT32(ptr) (*(const uint32_t*)(ptr))
    #define READ_UINT16(ptr) (*(const uint16_t*)(ptr))
    #define WRITE_UINT64(ptr, value) (*(uint64_t*)(ptr) = (value))
    #define WRITE_UINT32(ptr, value) (*(uint32_t*)(ptr) = (value))
    #define WRITE_UINT16(ptr, value) (*(uint16_t*)(ptr) = (value))
#else
    // Safe path: use memcpy (prevents crashes on ARMv5 and earlier)
    static inline uint64_t read_uint64_unaligned(const void* ptr) {
        uint64_t value;
        memcpy(&value, ptr, sizeof(uint64_t));
        return value;
    }
    
    static inline uint32_t read_uint32_unaligned(const void* ptr) {
        uint32_t value;
        memcpy(&value, ptr, sizeof(uint32_t));
        return value;
    }
    
    static inline uint16_t read_uint16_unaligned(const void* ptr) {
        uint16_t value;
        memcpy(&value, ptr, sizeof(uint16_t));
        return value;
    }
    
    static inline void write_uint64_unaligned(void* ptr, uint64_t value) {
        memcpy(ptr, &value, sizeof(uint64_t));
    }
    
    static inline void write_uint32_unaligned(void* ptr, uint32_t value) {
        memcpy(ptr, &value, sizeof(uint32_t));
    }
    
    static inline void write_uint16_unaligned(void* ptr, uint16_t value) {
        memcpy(ptr, &value, sizeof(uint16_t));
    }
    
    #define READ_UINT64(ptr) read_uint64_unaligned(ptr)
    #define READ_UINT32(ptr) read_uint32_unaligned(ptr)
    #define READ_UINT16(ptr) read_uint16_unaligned(ptr)
    #define WRITE_UINT64(ptr, value) write_uint64_unaligned(ptr, value)
    #define WRITE_UINT32(ptr, value) write_uint32_unaligned(ptr, value)
    #define WRITE_UINT16(ptr, value) write_uint16_unaligned(ptr, value)
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__)
#  define HOST_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#else
  /* Fallback runtime test (rarely needed) */
#  define HOST_BIG_ENDIAN (!*(unsigned char *)&(uint16_t){1})
#endif

#define READ_UINT64_LE(ptr) (HOST_BIG_ENDIAN ? BSWAP64(READ_UINT64(ptr)) : READ_UINT64(ptr))
#define READ_UINT32_LE(ptr) (HOST_BIG_ENDIAN ? BSWAP32(READ_UINT32(ptr)) : READ_UINT32(ptr))
#define READ_UINT64_BE(ptr) (HOST_BIG_ENDIAN ? READ_UINT64(ptr) : BSWAP64(READ_UINT64(ptr)))
#define READ_UINT32_BE(ptr) (HOST_BIG_ENDIAN ? READ_UINT32(ptr) : BSWAP32(READ_UINT32(ptr)))
#define READ_UINT16_LE(ptr) (HOST_BIG_ENDIAN ? BSWAP16(READ_UINT16(ptr)) : READ_UINT16(ptr))
#define READ_UINT16_BE(ptr) (HOST_BIG_ENDIAN ? READ_UINT16(ptr) : BSWAP16(READ_UINT16(ptr)))
#define WRITE_UINT64_LE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT64(ptr, BSWAP64(value)) : WRITE_UINT64(ptr, value))
#define WRITE_UINT32_LE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT32(ptr, BSWAP32(value)) : WRITE_UINT32(ptr, value))
#define WRITE_UINT64_BE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT64(ptr, value) : WRITE_UINT64(ptr, BSWAP64(value)))
#define WRITE_UINT32_BE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT32(ptr, value) : WRITE_UINT32(ptr, BSWAP32(value)))
#define WRITE_UINT16_LE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT16(ptr, BSWAP16(value)) : WRITE_UINT16(ptr, value))
#define WRITE_UINT16_BE(ptr, value) (HOST_BIG_ENDIAN ? WRITE_UINT16(ptr, value) : WRITE_UINT16(ptr, BSWAP16(value)))

#ifdef __cplusplus
}
#endif

#endif // moonbit_h_INCLUDED
