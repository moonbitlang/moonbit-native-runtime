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

/* =====================================
   This header contains private definition used by MoonBit native.
   The API here are unstable and not intended for usage in native stub.
   ===================================== */

#ifndef moonbit_runtime_h_INCLUDED
#define moonbit_runtime_h_INCLUDED

#include "moonbit.h"

#ifndef MOONBIT_TRIAL_DELETION
#define MOONBIT_TRIAL_DELETION 0
#endif

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

// Borrows ptr; null and static/immortal objects are not unique.
// Generated C uses the inline helper; object backends use the runtime export.
static inline int32_t moonbit_is_unique_ptr_inlined(void *ptr) {
  return ptr != 0 && Moonbit_rc_count(Moonbit_object_header(ptr)) == 1;
}

MOONBIT_EXPORT int32_t moonbit_is_unique_ptr(void *ptr);

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

#define Moonbit_init_dynamic_rc(header, kind)                                  \
  do {                                                                         \
    (header)->rc = Moonbit_make_dynamic_rc(kind);                              \
  } while (0)

#define Moonbit_set_meta(header, value)                                        \
  do {                                                                         \
    (header)->meta = (value);                                                  \
  } while (0)

#define Moonbit_tag_from_header(header)                                        \
  ((header) & MOONBIT_REGULAR_TAG_MASK)

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

// [possible_root] and [deferred_free] are used in drop specialization.
// [possible_root] is defined inline below; only [push_root], its buffer-append
// slow path, stays out of line since that is the branch which may allocate.
MOONBIT_EXPORT void moonbit_push_root(void *ptr);
MOONBIT_EXPORT void moonbit_deferred_free(void *ptr);
// Called by [moonbit_runtime_init] to install allocator-specific hooks.
MOONBIT_EXPORT void moonbit_allocator_init(void);
// [flush_cycles] is called at the end of the main function.
MOONBIT_EXPORT void moonbit_flush_cycles(void);
// Testing utilities.
MOONBIT_EXPORT void moonbit_collect_cycles_for_testing(void);
MOONBIT_EXPORT void moonbit_dump_root_for_testing(void);

// Nonzero once the possible-root buffer has outgrown its fixed trigger.
// System-allocator builds poll this at regular-object allocation safe points;
// mimalloc builds check it from their deferred-free callback instead, and also
// collect when mimalloc asks for a forced heap collection.
// Collection never starts from inside a drop.
MOONBIT_EXPORT int32_t moonbit_cycle_collection_threshold(void);
MOONBIT_EXPORT void moonbit_collect_cycles(void);

static inline int32_t moonbit_cycle_capable(void *ptr) {
  return MOONBIT_IN_ROOT(ptr) ||
         !MOONBIT_CHECK_CYCLE_STATUS(
             ptr, moonbit_CYCLE_STATUS_ACYCLIC_OR_CANDIDATE);
}

// Drop specialization emits a [possible_root] call on every decref of a
// cycle-capable object that does not reach zero, so this is one of the hottest
// paths in the runtime. Most calls are for an object already in the root
// buffer and do nothing but re-mark it a candidate, so only the append branch
// costs a call. The gate lives inside the body rather than around the
// definition because generated code emits the call unconditionally.
static inline void moonbit_possible_root(void *ptr) {
#if MOONBIT_TRIAL_DELETION
  // In-root nodes can become LIVE after an incref. A later decref makes
  // them candidates again without adding a duplicate root entry.
  set_cycle_status(ptr, moonbit_CYCLE_STATUS_ACYCLIC_OR_CANDIDATE);
  if (MOONBIT_IN_ROOT(ptr)) {
    return;
  }
  // [push_root] owns the rest: it publishes the entry (in_root). It never
  // collects, so buffering a root in the middle of a drop is safe.
  moonbit_push_root(ptr);
#else
  (void)ptr;
#endif
}

#if MOONBIT_TRIAL_DELETION
static inline void moonbit_add_possible_root(void *ptr) {
  if (moonbit_cycle_capable(ptr)) {
    moonbit_possible_root(ptr);
  }
}
static inline void moonbit_mark_live(void *ptr) {
  if (moonbit_cycle_capable(ptr)) {
    set_cycle_status(ptr, moonbit_CYCLE_STATUS_LIVE);
  }
}
#define MOONBIT_ADD_POSSIBLE_ROOT(ptr) moonbit_add_possible_root(ptr)
#define MOONBIT_MARK_LIVE(ptr) moonbit_mark_live(ptr)
#else
#define MOONBIT_ADD_POSSIBLE_ROOT(ptr) ((void)0)
#define MOONBIT_MARK_LIVE(ptr) ((void)0)
#endif

MOONBIT_EXPORT void moonbit_update_ref_valtype_rc(int32_t len, void *value, uint32_t header);

// Like moonbit_update_ref_valtype_rc but for value-enum array elements.
MOONBIT_EXPORT void moonbit_update_ref_enum_valtype_rc(int32_t len, void *value);

#define MOONBIT_ALLOCATOR_SYSTEM 1
#define MOONBIT_ALLOCATOR_MIMALLOC 2

#ifndef MOONBIT_ALLOCATOR
#define MOONBIT_ALLOCATOR MOONBIT_ALLOCATOR_SYSTEM
#endif

#if MOONBIT_ALLOCATOR == MOONBIT_ALLOCATOR_MIMALLOC

#if defined(_MSC_VER)
__declspec(restrict) void *mi_malloc(size_t size);
#elif defined(__GNUC__)
void *mi_malloc(size_t size) __attribute__((malloc, alloc_size(1)));
#else
void *mi_malloc(size_t size);
#endif
void mi_free(void *ptr);
#define MOONBIT_MALLOC_RAW mi_malloc
#define MOONBIT_FREE_RAW mi_free

#elif MOONBIT_ALLOCATOR == MOONBIT_ALLOCATOR_SYSTEM

void *malloc(size_t size);
void free(void *ptr);
#define MOONBIT_MALLOC_RAW malloc
#define MOONBIT_FREE_RAW free

#else

#error "unknown value for `MOONBIT_ALLOCATOR`"

#endif

// Because of drop specialization, the MoonBit program can call the free
// function directly, so we export a symbol for it
MOONBIT_EXPORT void moonbit_free_raw(void *obj);

#define moonbit_free(obj) MOONBIT_FREE_RAW(Moonbit_object_header(obj))

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

#endif // moonbit_runtime_h_INCLUDED
