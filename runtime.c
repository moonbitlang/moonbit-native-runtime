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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#endif

#define MOONBIT_BUILD_RUNTIME
#include "moonbit.h"
#include "moonbit_simd.h"

#ifdef _MSC_VER
#define _Noreturn __declspec(noreturn)
#endif

MOONBIT_EXPORT _Noreturn void moonbit_panic(void);

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER

int putchar(int c);
long write(int fd, const void *buf, size_t n);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
_Noreturn void exit(int status);
_Noreturn void abort(void);

#ifndef NULL
#define NULL ((void *)0)
#endif

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)
#include <unistd.h>
#include <unwind.h>
#include "backtrace.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif // #if defined(__APPLE__)

#endif // #if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)

#endif

// Set maximum number of backtrace frames to print.
#define BACKTRACE_LIMIT 15

// Forward declaration
static const char *demangle(const char *func_name, char **owned_out);

MOONBIT_EXPORT void *libc_malloc(size_t size) { return malloc(size); }
MOONBIT_EXPORT void libc_free(void *ptr) { free(ptr); }

// Static GC layout metadata. Regular object headers store a 22-bit word offset
// into this flattened table. Each layout has variable length:
//
//   word 0: payload size in 4-byte words, excluding the object header
//   word 1: number of following reference offsets
//   word 2+: reference-start offsets in 4-byte payload words
MOONBIT_EXPORT const uint32_t *moonbit_layout_table = 0;

MOONBIT_EXPORT void *moonbit_malloc(size_t size) {
  struct moonbit_object *ptr =
      (struct moonbit_object *)malloc(sizeof(struct moonbit_object) + size);
  Moonbit_init_dynamic_rc(ptr, moonbit_BLOCK_KIND_REGULAR);
  return ptr + 1;
}

#define moonbit_free(obj) free(Moonbit_object_header(obj))

// The low five RC bits store the block kind and cycle-collection bookkeeping,
// so changing the logical reference count by one changes the raw word by 32.
// Runtime hot paths can compare the raw word directly and avoid decoding the
// signed 27-bit count:
//
//   logical count 0: raw rc in [0, 31]
//   logical count 1: raw rc in [32, 63]
//   logical count 2+: raw rc >= 64
//   static/immortal: raw rc is negative
//
// These predicates intentionally use signed comparisons for the dynamic/shared
// thresholds so static/immortal objects are excluded without shifting.
#define MOONBIT_RC_COUNT_UNIT ((int32_t)(1u << MOONBIT_RC_COUNT_SHIFT))
#define raw_rc_is_dynamic(rc) ((int32_t)(rc) >= MOONBIT_RC_COUNT_UNIT)
#define raw_rc_is_shared(rc) ((int32_t)(rc) >= (MOONBIT_RC_COUNT_UNIT * 2))

#define MOONBIT_EXTERNAL_PAYLOAD_SIZE_MASK (((uint32_t)1 << 30) - 1)

// Moonbit_header_layout_class is defined in moonbit.h so the value-enum retain
// fast path can be inlined from runtime_core.c.

#define Moonbit_header_layout_index(header)                                    \
  (((header) >> MOONBIT_REGULAR_LAYOUT_INDEX_SHIFT) &                         \
   MOONBIT_REGULAR_LAYOUT_INDEX_MASK)

#define Moonbit_header_layout(header)                                          \
  (&moonbit_layout_table[Moonbit_header_layout_index(header)])

// An indexed layout entry is [size_in_word; child_count; tagged_child...].
// [child_count] is the number of tagged children (static refs + embedded value
// enums combined). The i-th tagged child is [(payload_word_offset << 1) |
// is_venum].
#define Moonbit_header_child_count(header)                                      \
  (Moonbit_header_layout(header)[1])

#define Moonbit_header_child_tagged(header, i)                                  \
  (Moonbit_header_layout(header)[2 + (i)])

#define Moonbit_make_external_object_header(payload_size)                      \
  (((uint32_t)MOONBIT_REGULAR_LAYOUT_CLASS_EXTERNAL                           \
    << MOONBIT_REGULAR_LAYOUT_CLASS_SHIFT) |                                  \
   ((uint32_t)(payload_size) & MOONBIT_EXTERNAL_PAYLOAD_SIZE_MASK))

static void **ref_slot_at(uint32_t layout_meta, void *value, int32_t ref_index) {
  int32_t offset_in_word =
      (int32_t)(Moonbit_header_child_tagged(layout_meta, ref_index) >> 1);
  return (void **)((uint32_t *)value + offset_in_word);
}


/* ===========================================
   The drop object algorithm

   When dropping a object, we need to recursively `decref` its reference children,
   which may trigger a recursive drop object operation if the RC become zero.
   So it is possible that a very large object may be dropped within a single `drop_object` call.
   We need to ensure stack safety even in case of arbitrarily deep object.
   The `drop_object` algorithm here is `O(1)` stack space + zero heap allocation.
   It achieves this by reusing corpse of parent object as a worklist for to-be-processed children.

   The whole `drop_object` procedure operate in a DFS manner,
   The `drop_object` procedure maintains a global linked array list of `struct drop_object_worklist` below.
   Each node in the list is a parent object being dropped,
   within the node holds to-be-processed children within that parent
   (note that children stored in the worklist may not necessarily need dropping,
    we perform the RC check when popping objects out of the worklist).
   The memory of each node come from the very parent object it represents.

   The worklist node need two `int32_t` plus one pointer for metadata.
   The object header give us the two `int32_t`,
   but we still need to find space for the next node pointer from the payload.
   A parent object may contain exactly `n` reference children,
   in this case, we have no room for storing the next pointer of the worklist.
   The trick here is to delay the initialization of next pointer
   until we encounter the first child in the parent to drop recursively.
   As we descend into that child, the first slot in the parent is guaranteed to be useless,
   so we can safely store the next pointer there.
 * =========================================== */

struct drop_object_worklist {
  // To obtain a natural drop order, we free children from left to right,
  // hence two `int32_t` values are needed.
  int32_t index; // this correspond to the RC field in the original object
  int32_t count; // this correspond to the meta field in the original object

  union {
    struct drop_object_worklist *next;
    void *data[1]; // Children to process in the worklist
  };
};

static inline
uint32_t scan_regular_object(
  void *obj,
  uint32_t const *layout,
  struct drop_object_worklist *wl_node,
  uint32_t count
);

/* Scan the object starting at address `obj`, whose layout is described by `layout`,
   and append all pointer children of `obj` into `wl_node`.
   The current number of children in `wl_node` should be passed in `count`.

   Return the total number of nodes in `wl_node` after writing references in `obj`.

   Note that in practice `obj` is probably part of `wl_node`,
   but it is guaranteed that the used part in `wl_node` never overlap with `obj`. */
static inline
uint32_t scan_regular_object(
  void *obj,
  uint32_t const *layout,
  struct drop_object_worklist *wl_node,
  uint32_t count
) {
  uint32_t child_count = layout[1];
  for (uint32_t i = 0; i < child_count; ++i) {
    uint32_t ref_field_desc = layout[i + 2];
    uint32_t *obj_start = (uint32_t*)obj + (ref_field_desc >> 1);
    if (ref_field_desc & 1u) {
      // value enum
      uint32_t const venum_header = *obj_start;
      // A value enum may contain reference field only in some of its constructors,
      // so it is possible that we will encounter a all-scalar constructor here.
      // In that case, there will be no meaningful layout index in the header,
      // so we need to test the layout class first.
      if (Moonbit_header_layout_class(venum_header) == MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED) {
        uint32_t const *layout = Moonbit_header_layout(venum_header);
        count = scan_regular_object(obj_start, layout, wl_node, count);
      }
    } else {
      // normal reference, store in `wl_node`
      void *child = *(void**)obj_start;
      wl_node->data[count] = child;
      ++count;
    }
  }
  return count;
}

MOONBIT_EXPORT void moonbit_drop_object(void *obj) {
  // The root of worklist, holding remaining objects to drop.
  struct drop_object_worklist *wl_root = 0;
  // The current worklist node being processed.
  // `wl_node` may be either `wl_root`, or a fresh new node not yet linked.
  struct drop_object_worklist *wl_node = 0;
  uint32_t meta;

process_new_object:
    /* The `process_new_object` block:

       1. Analyze and scan a new object to drop in `obj`

       2. If `obj` have any reference child,
          `process_new_object` should compact the layout of `obj`,
          and store all reference children using the layout of `wl_node`.
          After that, `process_new_object` will jump to `find_next_object`,
          to find a new child in `obj` to recursively drop.

          Note than in this case, `wl_node` will not be linked to `wl_root`.
          Because there is no room for `wl_node->next` before we process its content.

       3. If `obj` does not contain any reference child,
          `process_new_object` should jump to `back_to_parent` directly.
    */
    wl_node = (struct drop_object_worklist*)Moonbit_object_header(obj);
    meta = ((struct moonbit_object*)wl_node)->meta;
    switch (Moonbit_object_kind(obj)) {
      case moonbit_BLOCK_KIND_REGULAR: {
        switch (Moonbit_header_layout_class(meta)) {
          case MOONBIT_REGULAR_LAYOUT_CLASS_SCALAR:
            goto back_to_parent;
          case MOONBIT_REGULAR_LAYOUT_CLASS_EXTERNAL: {
            int32_t const payload_size = meta & MOONBIT_EXTERNAL_PAYLOAD_SIZE_MASK;
            void (**addr_of_finalize)(void *) =
                (void (**)(void *))((uint8_t *)obj + payload_size);
            (**addr_of_finalize)(obj);
            goto back_to_parent;
          }
          case MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED: {
            uint32_t const *layout = Moonbit_header_layout(meta);
            if (layout[0] << 2 == layout[1] * sizeof(void*)) {
              // fast path: all children are simple reference,
              // the layout is already compatible with `wl_node`,
              // no need to compact the layout
              wl_node->index = 0;
              wl_node->count = layout[1];
            } else {
              wl_node->index = 0;
              wl_node->count = scan_regular_object(obj, layout, wl_node, 0);
            }
            goto find_next_object;
          }
        }
        break;
      }

      case moonbit_BLOCK_KIND_VAL_ARRAY: goto back_to_parent;
      case moonbit_BLOCK_KIND_REF_ARRAY: {
        // The layout of reference array is compatible with `wl_node`,
        // so no need to do any compaction here.
        wl_node->index = 0;
        wl_node->count = meta;
        goto find_next_object;
      }

      case moonbit_BLOCK_KIND_REF_VALTYPE_ARRAY: {
        struct moonbit_valtype_array_header *varray_header = Moonbit_valtype_header(obj);
        // valtype array has a longer prefix,
        // so we need to adjust the worklist node accordingly
        wl_node = (struct drop_object_worklist*)varray_header;
        uint32_t const *elem_layout = Moonbit_header_layout(varray_header->elem_header);
        wl_node->index = 0;
        uint32_t const elem_size = elem_layout[0];
        uint32_t count = 0;
        for (uint32_t i = 0; i < meta; ++i) {
          uint32_t *elem_start = (uint32_t*)obj + i * elem_size;
          count = scan_regular_object(elem_start, elem_layout, wl_node, count);
        }
        wl_node->count = count;
        goto find_next_object;
      }
    }

back_to_parent:
  /* The `back_to_parent` block should be executed when current `wl_node` is completely processed.
     `back_to_parent` free this node and fetch a new node from the global `wl_root` list.
   */
  free(wl_node);
  if (!wl_root)
    // Everything dropped, no more work to do
    return;

  /* Note that we does not remove the new node from `wl_root` here,
     because in case we find a new child to drop below in `find_next_object`,
     `wl_node` should be pushed into `wl_root` again, and this pop-and-push is unnecessary.
     We only pop nodes from `wl_root` when a node is completely processed. */
  wl_node = wl_root;

find_next_object:
  /* The `find_next_object` block tries to find the next object to drop recursively
     from current `wl_node`. If a object is found, it will jump to `process_new_object`.
     If no object is found in current `wl_node`,
     `wl_node` will be dropped and removed from the worklist.

     Note that `wl_node` may or may not be `wl_root` when `find_next_object` is executed.
     If the control flow come from `back_to_parent`, `wl_node` will be `wl_root.
     If the control flow come from `process_new_object`,
     `wl_node` will be a fresh node not yet linked to `wl_root`.
   */
  for (uint32_t i = wl_node->index, count = wl_node->count; i < count; ++i) {
    obj = wl_node->data[i];
    if (!obj)
      continue;

    struct moonbit_object *header = Moonbit_object_header(obj);
    int32_t const rc = header->rc;
    if (raw_rc_is_shared(rc)) {
      // This child is still alive, decrease the count and
      // continue with remaining reference children
      header->rc = rc - MOONBIT_RC_COUNT_UNIT;
      continue;
    }

    if (!raw_rc_is_dynamic(rc))
      // static object
      continue;

    // we have found a object to drop recursively
    ++i;
    if (i == count) {
      // last child in parent, no longer need to keep parent in the worklist
      if (wl_node == wl_root)
        wl_root = wl_node->next;
      free(wl_node);
    } else {
      wl_node->index = i;
      if (wl_node != wl_root) {
        // `wl_node` come from a new object, link it to the worklist.
        // Note that the first object in `wl_node->data` must have been traversed now,
        // so we can safely store the next pointer in `wl_node->next`,
        // which is an alias to `wl_node->data[0]`.
        wl_node->next = wl_root;
        wl_root = wl_node;
      }
    }
    goto process_new_object;
  }

  // No more object to process in `wl_node`, drop it now.
  if (wl_node == wl_root)
    wl_root = wl_node->next;

  goto back_to_parent;
}

MOONBIT_EXPORT void moonbit_incref(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_dynamic(rc)) {
    Moonbit_increase_rc_count(header);
  }
}

MOONBIT_EXPORT void moonbit_decref(void *ptr) {
  struct moonbit_object *header = Moonbit_object_header(ptr);
  int32_t const rc = header->rc;
  if (raw_rc_is_shared(rc)) {
    header->rc = rc - MOONBIT_RC_COUNT_UNIT;
  } else if (raw_rc_is_dynamic(rc)) {
    moonbit_drop_object(ptr);
  }
}

// A value enum's first word is a self-describing object header (the active
// constructor's layout), and its payload begins after the tag and size words
// (8 bytes). The constructor layout's reference offsets are payload-relative
// words, so a SCALAR header means "no references" and an INDEXED header lists
// exactly the references live for the current tag. A null reference slot is
// skipped so nullable references are safe.
//
// Two subtleties of the helpers below:
//
//   - They are emitted only for value enums whose type carries references, but a
//     value can currently hold a scalar constructor -- e.g. `A` or `B(Int)` of
//     `enum E { A; B(Int); C(String) }`. Such a value has a SCALAR header whose
//     layout_index is a dummy 0, so the `!= INDEXED` early return is required,
//     not just a fast path: it both skips the no-reference case and avoids
//     indexing moonbit_layout_table with that dummy index.
//
//   - Each live slot holds a plain heap reference, so moonbit_incref/decref is
//     the correct operation. This relies on value enums not nesting: a payload
//     field cannot itself be an inline value enum (unsupported for now). If
//     nesting is added, such a slot would instead need a recursive
//     moonbit_incref/decref_value_enum.

// Adjust the rc of the references of a value enum [value] that fills [len] array
// slots, mirroring moonbit_update_ref_valtype_rc but dispatching on the value
// enum's own (tag-dependent) header. len==0 undoes the init's references;
// len>1 adds (len-1) to each so all copies share ownership.
MOONBIT_EXPORT void moonbit_update_ref_enum_valtype_rc(int32_t len, void *value) {
  uint32_t header = *(uint32_t *)value;
  if (Moonbit_header_layout_class(header) != MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED) {
    return;
  }
  uint32_t *payload =
      (uint32_t *)((char *)value );
  uint32_t n = Moonbit_header_child_count(header);
  if (len == 0) {
    for (uint32_t i = 0; i < n; ++i) {
      void *r = *(void **)(payload + (Moonbit_header_child_tagged(header, i) >> 1));
      if (r) {
        moonbit_decref(r);
      }
    }
  } else if (len > 1) {
    for (uint32_t i = 0; i < n; ++i) {
      void *r = *(void **)(payload + (Moonbit_header_child_tagged(header, i) >> 1));
      if (r) {
        struct moonbit_object *value_header = Moonbit_object_header(r);
        const int32_t count = Moonbit_rc_count(value_header);
        if (count > 0) {
          Moonbit_set_rc_count(value_header,
                               (uint32_t)count + (uint32_t)len - 1u);
        }
      }
    }
  }
}

// Slow path: walk the current variant's reference slots. Assumes the header is
// INDEXED (the callers inline that cheap check). Kept out of line so scalar-tag
// moves stay call-free once the check is inlined at the use site.
MOONBIT_EXPORT void moonbit_incref_value_enum_loop(void *p) {
  uint32_t header = *(uint32_t *)p;
  uint32_t *payload =
      (uint32_t *)((char *)p );
  uint32_t n = Moonbit_header_child_count(header);
  for (uint32_t i = 0; i < n; i++) {
    void *r = *(void **)(payload + (Moonbit_header_child_tagged(header, i) >> 1));
    if (r) {
      // `r` is a plain heap reference, not a nested inline value enum (value
      // enums don't nest yet), so a direct moonbit_incref is correct.
      moonbit_incref(r);
    }
  }
}

MOONBIT_EXPORT void moonbit_incref_value_enum(void *p) {
  if (Moonbit_header_layout_class(*(uint32_t *)p) ==
      MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED) {
    moonbit_incref_value_enum_loop(p);
  }
}

MOONBIT_EXPORT void moonbit_decref_value_enum_loop(void *p) {
  uint32_t header = *(uint32_t *)p;
  uint32_t *payload =
      (uint32_t *)((char *)p );
  uint32_t n = Moonbit_header_child_count(header);
  for (uint32_t i = 0; i < n; i++) {
    void *r = *(void **)(payload + (Moonbit_header_child_tagged(header, i) >> 1));
    if (r) {
      // `r` is a plain heap reference, not a nested inline value enum (value
      // enums don't nest yet), so a direct moonbit_decref is correct.
      moonbit_decref(r);
    }
  }
}

MOONBIT_EXPORT void moonbit_decref_value_enum(void *p) {
  if (Moonbit_header_layout_class(*(uint32_t *)p) ==
      MOONBIT_REGULAR_LAYOUT_CLASS_INDEXED) {
    moonbit_decref_value_enum_loop(p);
  }
}


#ifdef _WIN32

#include <windows.h>

#ifdef _MSC_VER

#include <shellapi.h>
#include <dbghelp.h>
#include <bcrypt.h>

#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Bcrypt.lib")

#endif

// #ifdef _WIN32
#elif defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)

// Forward declaration
static void error_callback(void *data, const char *msg, int errnum);

typedef struct {
  struct backtrace_state *state;
  int count;
  int limit_reached;
  int has_meaningful_frame;
  int consecutive_unknown;
  int stop_unwinding;
} mbt_backtrace_data;

static int symbolize_callback(void *data, uintptr_t pc, const char *filename, int lineno, const char *function) {
  mbt_backtrace_data *bt_data = (mbt_backtrace_data *)data;
  
  if (bt_data->limit_reached) return 0;

  if (function && (strstr(function, "moonbit_panic") || strstr(function, "unwind_callback"))) {
    return 0;
  }

  int is_meaningful = (filename != NULL) || (function != NULL);
  
  if (bt_data->has_meaningful_frame && !is_meaningful && lineno == 0) {
    bt_data->consecutive_unknown++;
    if (bt_data->consecutive_unknown > 1) {
      bt_data->stop_unwinding = 1;
      return 0;
    }
  } else {
    bt_data->consecutive_unknown = 0;
    if (is_meaningful) {
      bt_data->has_meaningful_frame = 1;
    }
  }

  if (bt_data->count >= BACKTRACE_LIMIT) {
    fprintf(stderr, "    ...\n");
    
    bt_data->limit_reached = 1;
    return 0;
  }

  char *owned_name = NULL;
  const char *func_name = function ? demangle(function, &owned_name) : "???";
  const char *file_name = filename ? filename : "???";

  fprintf(stderr, "    at %s (%s:%d)\n", func_name, file_name, lineno);

  bt_data->count++;
  free(owned_name);
  return 0;
}

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *ctx, void *arg) {
  mbt_backtrace_data *bt_data = (mbt_backtrace_data *)arg;
  
  if (bt_data->limit_reached || bt_data->stop_unwinding) {
    return _URC_END_OF_STACK;
  }
  
  uintptr_t pc = _Unwind_GetIP(ctx);
  if (pc == 0) return _URC_END_OF_STACK;

  backtrace_pcinfo(bt_data->state, pc - 1, symbolize_callback, error_callback, bt_data);

  if (bt_data->stop_unwinding) {
    return _URC_END_OF_STACK;
  }

  return _URC_NO_REASON;
}

static void error_callback(void *data, const char *msg, int errnum) {
  fprintf(stderr, "libbacktrace error: %s (%d)\n", msg, errnum);
}

#endif // #if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)

MOONBIT_EXPORT _Noreturn void moonbit_panic(void) {
#ifdef _MSC_VER
  fflush(stdout);
  fprintf(stderr, "PanicError\n");

  SymSetOptions(SYMOPT_LOAD_LINES);

  void *frames[BACKTRACE_LIMIT];
  int n = CaptureStackBackTrace(0, BACKTRACE_LIMIT, frames, NULL);
  HANDLE self = GetCurrentProcess();
  if (!SymInitialize(self, NULL, TRUE))
    goto fail_to_get_stack_trace;

  for (int i = 0; i < n; ++i) {
    void *frame = frames[i];
    SYMBOL_INFO *symbol = malloc(sizeof(SYMBOL_INFO) + 256);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    if (!SymFromAddr(self, (DWORD64)frame, NULL, symbol)) {
      fprintf(stderr, "    at ??? (%lld)\n", (uint64_t)frame);
      continue;
    }
    char *demangled_name = NULL;
    const char *func_name = demangle(symbol->Name, &demangled_name);

    DWORD displacement;
#ifdef _WIN64
    IMAGEHLP_LINE64 line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    BOOL ret = SymGetLineFromAddr64(self, (DWORD64)frame, &displacement, &line);
#else
    IMAGEHLP_LINE line;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE);
    BOOL ret = SymGetLineFromAddr(self, (DWORD)frame, &displacement, &line);
#endif
    if (ret) {
      fprintf(stderr, "    at %s (%s:%d)\n", func_name, line.FileName, line.LineNumber);
    } else {
      fprintf(stderr, "    at %s\n", func_name);
    }

    if (demangled_name)
      free(demangled_name);

    if (strncmp(symbol->Name, "main", 4) == 0)
      break;
  }

  if (n == BACKTRACE_LIMIT)
    fprintf(stderr, "    ...\n");

fail_to_get_stack_trace:
// #ifdef _MSC_VER
#elif defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)
  fflush(stdout);
  fprintf(stderr, "PanicError\n");

  static struct backtrace_state *state = NULL;
  if (!state) {
    static char exec_path[1024];
    uint32_t size = sizeof(exec_path);
    const char* path_ptr = NULL;

#if defined(__APPLE__)
    if (_NSGetExecutablePath(exec_path, &size) == 0) {
      path_ptr = exec_path;
    }
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", exec_path, sizeof(exec_path)-1);
    if (len != -1) {
      exec_path[len] = '\0';
      path_ptr = exec_path;
    }
#endif // #if defined(__APPLE__)
    state = backtrace_create_state(path_ptr, 1, error_callback, NULL);
  }

  if (state) {
    mbt_backtrace_data bt_data = {state, 0, 0, 0, 0, 0};
    _Unwind_Backtrace(unwind_callback, &bt_data);
  }
#endif // #if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)

#ifdef MOONBIT_NATIVE_EXIT_ON_PANIC
  exit(1);
#else
  abort();
#endif
}

/* ------------------------------------------------------------- */
/* Optional: integrate with TinyCC (-run) backtrace facility.
   TinyCC's tccrun.c supports a per-frame callback (TCCBtFunc) when a runtime
   exception / SIGABRT happens. We expose a callback symbol that can be
   discovered by the host (tcc -run) and used to build a backtrace string and
   print it.

   Notes:
   - Keep this code independent from libc headers: it must compile with
     MOONBIT_NATIVE_NO_SYS_HEADER.
   - The host (TinyCC) decides whether to call this function.
*/

static void moonbit__eprint_n(const char *s, size_t n) {
  if (!s || n == 0)
    return;
#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
  while (n) {
    /* Best-effort write to stderr; retry a few times to handle EINTR-like
       transient failures without needing errno. Retries are bounded. */
    int tries = 0;
    long r = write(2, s, n);
    while (r < 0 && tries < 3) {
      tries++;
      r = write(2, s, n);
    }
    if (r <= 0)
      break;
    s += (size_t)r;
    n -= (size_t)r;
  }
#else
  (void)fwrite(s, 1, n, stderr);
  (void)fflush(stderr);
#endif
}

static void moonbit__eprint(const char *s) {
  if (!s)
    return;
  moonbit__eprint_n(s, strlen(s));
}

static void moonbit__bt_buf_append_char(char *buf, size_t cap, size_t *len,
                                        char c) {
  /* Need space for the char and a trailing '\0'. Use subtraction to avoid
     potential overflow in *len + 2. */
  if (cap == 0 || *len >= cap || (cap - *len) < 2)
    return;
  buf[*len] = c;
  *len += 1;
  buf[*len] = 0;
}

static void moonbit__bt_buf_append_cstr(char *buf, size_t cap, size_t *len,
                                        const char *s) {
  if (!s)
    return;
  for (size_t i = 0; s[i]; i++)
    moonbit__bt_buf_append_char(buf, cap, len, s[i]);
}

static void moonbit__bt_buf_append_u32_dec(char *buf, size_t cap, size_t *len,
                                           uint32_t x) {
  char tmp[16];
  int n = 0;
  if (x == 0) {
    moonbit__bt_buf_append_char(buf, cap, len, '0');
    return;
  }
  while (x && n < (int)sizeof(tmp)) {
    tmp[n++] = (char)('0' + (x % 10));
    x /= 10;
  }
  while (n-- > 0)
    moonbit__bt_buf_append_char(buf, cap, len, tmp[n]);
}

static void moonbit__bt_buf_append_ptr_hex(char *buf, size_t cap, size_t *len,
                                           uintptr_t p) {
  static const char hexdig[] = "0123456789abcdef";
  /* Need "0x" + 2*bytes characters, plus trailing '\0'. */
  const size_t need_chars = 2 + (sizeof(uintptr_t) * 2);
  if (cap == 0 || *len >= cap)
    return;
  if ((cap - *len) < (need_chars + 1)) {
    moonbit__bt_buf_append_cstr(buf, cap, len, "0x?");
    return;
  }
  moonbit__bt_buf_append_cstr(buf, cap, len, "0x");
  for (int i = (int)(sizeof(uintptr_t) * 2) - 1; i >= 0; i--) {
    unsigned shift = (unsigned)i * 4;
    moonbit__bt_buf_append_char(buf, cap, len,
                                hexdig[(p >> shift) & 0xF]);
  }
}

static uint32_t moonbit__bt_level = 0;

#ifndef MOONBIT_NATIVE_BT_MAX_FRAMES
#define MOONBIT_NATIVE_BT_MAX_FRAMES 64
#endif

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} mbt_str;

static int mbt_str_reserve(mbt_str *s, size_t extra) {
  if (s->len + extra + 1 <= s->cap)
    return 1;
  size_t next_cap = s->cap ? s->cap : 64;
  while (next_cap < s->len + extra + 1)
    next_cap *= 2;
  char *next = (char *)realloc(s->buf, next_cap);
  if (!next)
    return 0;
  s->buf = next;
  s->cap = next_cap;
  return 1;
}

static int mbt_str_init(mbt_str *s) {
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
  return mbt_str_reserve(s, 0);
}

static int mbt_str_append_char(mbt_str *s, char c) {
  if (!mbt_str_reserve(s, 1))
    return 0;
  s->buf[s->len++] = c;
  s->buf[s->len] = 0;
  return 1;
}

static int mbt_str_append_cstr(mbt_str *s, const char *cstr) {
  if (!cstr)
    return 1;
  for (size_t i = 0; cstr[i]; i++) {
    if (!mbt_str_append_char(s, cstr[i]))
      return 0;
  }
  return 1;
}

static int mbt_str_append_n(mbt_str *s, const char *cstr, size_t n) {
  if (!cstr || n == 0)
    return 1;
  if (!mbt_str_reserve(s, n))
    return 0;
  memcpy(s->buf + s->len, cstr, n);
  s->len += n;
  s->buf[s->len] = 0;
  return 1;
}

static char *mbt_str_release(mbt_str *s) {
  char *out = s->buf;
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
  return out;
}

static int mbt_dup_cstr(const char *src, char **out) {
  size_t n = strlen(src);
  char *dst = (char *)malloc(n + 1);
  if (!dst)
    return 0;
  memcpy(dst, src, n + 1);
  *out = dst;
  return 1;
}

static int mbt_parse_u32(const char **p, uint32_t *out) {
  const char *cur = *p;
  if (*cur < '0' || *cur > '9')
    return 0;
  uint32_t v = 0;
  while (*cur >= '0' && *cur <= '9') {
    uint32_t d = (uint32_t)(*cur - '0');
    v = v * 10 + d;
    cur++;
  }
  *p = cur;
  *out = v;
  return 1;
}

static int mbt_hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F')
    return 10 + (c - 'A');
  return -1;
}

static int mbt_parse_identifier(const char **p, char **out) {
  uint32_t n = 0;
  if (!mbt_parse_u32(p, &n))
    return 0;
  const char *start = *p;
  size_t remaining = strlen(start);
  if (remaining < (size_t)n)
    return 0;
  *p = start + n;

  mbt_str decoded;
  if (!mbt_str_init(&decoded))
    return 0;
  for (uint32_t i = 0; i < n; i++) {
    char c = start[i];
    if (c != '_') {
      if (!mbt_str_append_char(&decoded, c)) {
        free(decoded.buf);
        return 0;
      }
      continue;
    }
    if (i + 1 >= n) {
      free(decoded.buf);
      return 0;
    }
    char next = start[i + 1];
    if (next == '_') {
      if (!mbt_str_append_char(&decoded, '_')) {
        free(decoded.buf);
        return 0;
      }
      i += 1;
      continue;
    }
    if (i + 2 >= n) {
      free(decoded.buf);
      return 0;
    }
    int hi = mbt_hex_value(start[i + 1]);
    int lo = mbt_hex_value(start[i + 2]);
    if (hi < 0 || lo < 0) {
      free(decoded.buf);
      return 0;
    }
    char v = (char)((hi << 4) | lo);
    if (!mbt_str_append_char(&decoded, v)) {
      free(decoded.buf);
      return 0;
    }
    i += 2;
  }
  *out = mbt_str_release(&decoded);
  return 1;
}

static int mbt_parse_package_segments(const char **p, uint32_t count,
                                      char **out_pkg) {
  mbt_str pkg;
  if (!mbt_str_init(&pkg))
    return 0;
  for (uint32_t i = 0; i < count; i++) {
    char *seg = NULL;
    if (!mbt_parse_identifier(p, &seg)) {
      free(pkg.buf);
      return 0;
    }
    if (i > 0 && !mbt_str_append_char(&pkg, '/')) {
      free(seg);
      free(pkg.buf);
      return 0;
    }
    if (!mbt_str_append_cstr(&pkg, seg)) {
      free(seg);
      free(pkg.buf);
      return 0;
    }
    free(seg);
  }
  *out_pkg = mbt_str_release(&pkg);
  return 1;
}

static int mbt_parse_package(const char **p, char **out_pkg) {
  if (**p != 'P')
    return 0;
  (*p)++;

  if (**p == 'B') {
    (*p)++;
    return mbt_dup_cstr("moonbitlang/core/builtin", out_pkg);
  }

  if (**p == 'C') {
    (*p)++;
    const char *count_start = *p;
    uint32_t count = 0;
    if (!mbt_parse_u32(p, &count))
      return 0;

    char *suffix = NULL;
    if (!mbt_parse_package_segments(p, count, &suffix)) {
      *p = count_start;
      if (**p < '0' || **p > '9')
        return 0;
      count = (uint32_t)(**p - '0');
      (*p)++;
      if (!mbt_parse_package_segments(p, count, &suffix))
        return 0;
    }

    mbt_str full;
    if (!mbt_str_init(&full)) {
      free(suffix);
      return 0;
    }
    if (!mbt_str_append_cstr(&full, "moonbitlang/core") ||
        (suffix[0] && (!mbt_str_append_char(&full, '/') ||
                       !mbt_str_append_cstr(&full, suffix)))) {
      free(suffix);
      free(full.buf);
      return 0;
    }
    free(suffix);
    *out_pkg = mbt_str_release(&full);
    return 1;
  }

  const char *count_start = *p;
  uint32_t count = 0;
  if (!mbt_parse_u32(p, &count))
    return 0;
  if (mbt_parse_package_segments(p, count, out_pkg))
    return 1;

  *p = count_start;
  if (**p < '0' || **p > '9')
    return 0;
  count = (uint32_t)(**p - '0');
  (*p)++;
  return mbt_parse_package_segments(p, count, out_pkg);
}

static int mbt_pkg_is_core(const char *pkg) {
  if (!pkg || !pkg[0])
    return 0;
  const char *prefix = "moonbitlang/core";
  size_t n = strlen(prefix);
  if (strncmp(pkg, prefix, n) != 0)
    return 0;
  return pkg[n] == 0 || pkg[n] == '/';
}

static int mbt_append_type_path(const char **p, mbt_str *out,
                                int omit_core_prefix) {
  char *pkg = NULL;
  char *type = NULL;
  if (!mbt_parse_package(p, &pkg))
    return 0;
  if (!mbt_parse_identifier(p, &type)) {
    free(pkg);
    return 0;
  }
  if (**p == 'L') {
    (*p)++;
    char *local = NULL;
    if (!mbt_parse_identifier(p, &local)) {
      free(pkg);
      free(type);
      return 0;
    }
    mbt_str combined;
    if (!mbt_str_init(&combined)) {
      free(pkg);
      free(type);
      free(local);
      return 0;
    }
    if (!mbt_str_append_cstr(&combined, type) ||
        !mbt_str_append_char(&combined, '.') ||
        !mbt_str_append_cstr(&combined, local)) {
      free(pkg);
      free(type);
      free(local);
      free(combined.buf);
      return 0;
    }
    free(type);
    free(local);
    type = mbt_str_release(&combined);
  }

  const char *pkg_use = pkg;
  if (omit_core_prefix && mbt_pkg_is_core(pkg))
    pkg_use = "";

  if (!mbt_str_append_char(out, '@')) {
    free(pkg);
    free(type);
    return 0;
  }
  if (pkg_use && pkg_use[0]) {
    if (!mbt_str_append_cstr(out, pkg_use) ||
        !mbt_str_append_char(out, '.')) {
      free(pkg);
      free(type);
      return 0;
    }
  }
  if (!mbt_str_append_cstr(out, type)) {
    free(pkg);
    free(type);
    return 0;
  }
  free(pkg);
  free(type);
  return 1;
}

static int mbt_parse_type_arg(const char **p, mbt_str *out);

static int mbt_parse_type_args(const char **p, mbt_str *out) {
  if (**p == 'H') {
    (*p)++;
    if (!mbt_str_append_cstr(out, " raise "))
      return 0;
    return mbt_parse_type_arg(p, out);
  }
  if (**p != 'G')
    return 1;
  (*p)++;
  if (!mbt_str_append_char(out, '['))
    return 0;
  int first = 1;
  while (**p && **p != 'E') {
    if (!first) {
      if (!mbt_str_append_cstr(out, ", "))
        return 0;
    }
    if (!mbt_parse_type_arg(p, out))
      return 0;
    first = 0;
  }
  if (**p != 'E')
    return 0;
  (*p)++;
  if (!mbt_str_append_char(out, ']'))
    return 0;
  if (**p == 'H') {
    (*p)++;
    if (!mbt_str_append_cstr(out, " raise "))
      return 0;
    if (!mbt_parse_type_arg(p, out))
      return 0;
  }
  return 1;
}

static int mbt_parse_fn_type(const char **p, mbt_str *out, int async_mark) {
  if (**p != 'W')
    return 0;
  (*p)++;
  if (async_mark && !mbt_str_append_cstr(out, "async "))
    return 0;
  if (!mbt_str_append_char(out, '('))
    return 0;
  int first = 1;
  while (**p && **p != 'E') {
    if (!first) {
      if (!mbt_str_append_cstr(out, ", "))
        return 0;
    }
    if (!mbt_parse_type_arg(p, out))
      return 0;
    first = 0;
  }
  if (**p != 'E')
    return 0;
  (*p)++;
  if (!mbt_str_append_cstr(out, ") -> "))
    return 0;
  if (!mbt_parse_type_arg(p, out))
    return 0;
  if (**p == 'Q') {
    (*p)++;
    if (!mbt_str_append_cstr(out, " raise "))
      return 0;
    if (!mbt_parse_type_arg(p, out))
      return 0;
  }
  return 1;
}

static int mbt_parse_type_ref(const char **p, mbt_str *out) {
  if (**p != 'R')
    return 0;
  (*p)++;
  if (!mbt_append_type_path(p, out, 0))
    return 0;
  if (**p == 'G') {
    if (!mbt_parse_type_args(p, out))
      return 0;
  }
  return 1;
}

static int mbt_parse_type_arg(const char **p, mbt_str *out) {
  char c = **p;
  if (!c)
    return 0;
  switch (c) {
    case 'i': (*p)++; return mbt_str_append_cstr(out, "Int");
    case 'l': (*p)++; return mbt_str_append_cstr(out, "Int64");
    case 'h': (*p)++; return mbt_str_append_cstr(out, "Int16");
    case 'j': (*p)++; return mbt_str_append_cstr(out, "UInt");
    case 'k': (*p)++; return mbt_str_append_cstr(out, "UInt16");
    case 'm': (*p)++; return mbt_str_append_cstr(out, "UInt64");
    case 'd': (*p)++; return mbt_str_append_cstr(out, "Double");
    case 'f': (*p)++; return mbt_str_append_cstr(out, "Float");
    case 'b': (*p)++; return mbt_str_append_cstr(out, "Bool");
    case 'c': (*p)++; return mbt_str_append_cstr(out, "Char");
    case 's': (*p)++; return mbt_str_append_cstr(out, "String");
    case 'u': (*p)++; return mbt_str_append_cstr(out, "Unit");
    case 'y': (*p)++; return mbt_str_append_cstr(out, "Byte");
    case 'z': (*p)++; return mbt_str_append_cstr(out, "Bytes");
    case 'v': (*p)++; return mbt_str_append_cstr(out, "V128");
    case 'A': {
      (*p)++;
      if (!mbt_str_append_cstr(out, "FixedArray["))
        return 0;
      if (!mbt_parse_type_arg(p, out))
        return 0;
      return mbt_str_append_char(out, ']');
    }
    case 'O': {
      (*p)++;
      if (!mbt_str_append_cstr(out, "Option["))
        return 0;
      if (!mbt_parse_type_arg(p, out))
        return 0;
      return mbt_str_append_char(out, ']');
    }
    case 'U': {
      (*p)++;
      if (!mbt_str_append_char(out, '('))
        return 0;
      int first = 1;
      while (**p && **p != 'E') {
        if (!first) {
          if (!mbt_str_append_cstr(out, ", "))
            return 0;
        }
        if (!mbt_parse_type_arg(p, out))
          return 0;
        first = 0;
      }
      if (**p != 'E')
        return 0;
      (*p)++;
      return mbt_str_append_char(out, ')');
    }
    case 'V':
      (*p)++;
      return mbt_parse_fn_type(p, out, 1);
    case 'W':
      return mbt_parse_fn_type(p, out, 0);
    case 'R':
      return mbt_parse_type_ref(p, out);
    default:
      return 0;
  }
}

static int mbt_skip_decimal(const char **p) {
  if (**p < '0' || **p > '9')
    return 0;
  while (**p >= '0' && **p <= '9')
    (*p)++;
  return 1;
}

static int mbt_parse_decimal_span(const char **p, const char **start, size_t *len) {
  const char *s = *p;
  if (*s < '0' || *s > '9')
    return 0;
  while (**p >= '0' && **p <= '9')
    (*p)++;
  *start = s;
  *len = (size_t)(*p - s);
  return 1;
}

static int mbt_parse_lifted_suffixes(const char **p, mbt_str *out) {
  while (**p == 'N') {
    (*p)++;
    char *nested = NULL;
    if (!mbt_parse_identifier(p, &nested))
      return 0;
    if (**p != 'S') {
      free(nested);
      return 0;
    }
    (*p)++;
    if (!mbt_skip_decimal(p)) {
      free(nested);
      return 0;
    }
    if (!mbt_str_append_char(out, '.') || !mbt_str_append_cstr(out, nested)) {
      free(nested);
      return 0;
    }
    free(nested);
  }
  return 1;
}

static int mbt_parse_closure_suffix(const char **p, mbt_str *out) {
  if (**p != 'C')
    return 1;
  (*p)++;
  if (**p < '0' || **p > '9')
    return 0;

  const char *uuid = NULL;
  size_t uuid_len = 0;
  if (!mbt_parse_decimal_span(p, &uuid, &uuid_len))
    return 0;

  if (**p == 'l') {
    const char *line = NULL;
    size_t line_len = 0;
    (*p)++;
    if (!mbt_parse_decimal_span(p, &line, &line_len))
      return 0;
    return mbt_str_append_cstr(out, ".anon(l") &&
           mbt_str_append_n(out, line, line_len) &&
           mbt_str_append_char(out, ')');
  }

  (void)uuid;
  (void)uuid_len;
  return mbt_str_append_cstr(out, ".anon");
}

static int mbt_parse_fn_suffixes(const char **p, mbt_str *out) {
  if (!mbt_parse_lifted_suffixes(p, out))
    return 0;
  if (!mbt_parse_closure_suffix(p, out))
    return 0;
  return 1;
}

static int demangle_tag_F(const char **p, mbt_str *out) {
  int ok = 1;
  char *pkg = NULL;
  char *name = NULL;
  if (!mbt_parse_package(p, &pkg) || !mbt_parse_identifier(p, &name)) {
    ok = 0;
  } else if (!mbt_str_append_char(out, '@') ||
             (pkg && pkg[0] && (!mbt_str_append_cstr(out, pkg) ||
                                !mbt_str_append_char(out, '.'))) ||
             !mbt_str_append_cstr(out, name)) {
    ok = 0;
  }
  free(pkg);
  free(name);
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  if (ok && (**p == 'G' || **p == 'H')) {
    if (!mbt_parse_type_args(p, out))
      ok = 0;
  }
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  return ok;
}

static int demangle_tag_M(const char **p, mbt_str *out) {
  int ok = 1;
  char *pkg = NULL;
  char *type = NULL;
  char *name = NULL;
  if (!mbt_parse_package(p, &pkg) || !mbt_parse_identifier(p, &type) ||
      !mbt_parse_identifier(p, &name)) {
    ok = 0;
  } else if (!mbt_str_append_char(out, '@') ||
             (pkg && pkg[0] && (!mbt_str_append_cstr(out, pkg) ||
                                !mbt_str_append_char(out, '.'))) ||
             !mbt_str_append_cstr(out, type) ||
             !mbt_str_append_cstr(out, "::") ||
             !mbt_str_append_cstr(out, name)) {
    ok = 0;
  }
  free(pkg);
  free(type);
  free(name);
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  if (ok && (**p == 'G' || **p == 'H')) {
    if (!mbt_parse_type_args(p, out))
      ok = 0;
  }
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  return ok;
}

static int demangle_tag_I(const char **p, mbt_str *out) {
  int ok = 1;
  mbt_str impl_type;
  mbt_str trait_type;
  mbt_str type_args;
  mbt_str fn_suffix;
  int impl_inited = 0;
  int trait_inited = 0;
  int args_inited = 0;
  int suffix_inited = 0;

  if (mbt_str_init(&impl_type))
    impl_inited = 1;
  if (mbt_str_init(&trait_type))
    trait_inited = 1;
  if (mbt_str_init(&type_args))
    args_inited = 1;
  if (mbt_str_init(&fn_suffix))
    suffix_inited = 1;
  if (!impl_inited || !trait_inited || !args_inited || !suffix_inited)
    ok = 0;

  if (ok && (!mbt_append_type_path(p, &impl_type, 0) ||
             !mbt_append_type_path(p, &trait_type, 0)))
    ok = 0;

  char *name = NULL;
  if (ok && !mbt_parse_identifier(p, &name))
    ok = 0;
  if (ok && !mbt_parse_fn_suffixes(p, &fn_suffix))
    ok = 0;
  if (ok && (**p == 'G' || **p == 'H')) {
    if (!mbt_parse_type_args(p, &type_args))
      ok = 0;
  }
  if (ok && !mbt_parse_fn_suffixes(p, &fn_suffix))
    ok = 0;

  if (ok && (!mbt_str_append_cstr(out, "impl ") ||
             !mbt_str_append_cstr(out, trait_type.buf) ||
             !mbt_str_append_cstr(out, " for ") ||
             !mbt_str_append_cstr(out, impl_type.buf) ||
             (type_args.len && !mbt_str_append_cstr(out, type_args.buf)) ||
             !mbt_str_append_cstr(out, " with ") ||
             !mbt_str_append_cstr(out, name) ||
             (fn_suffix.len && !mbt_str_append_cstr(out, fn_suffix.buf))))
    ok = 0;

  if (impl_inited)
    free(impl_type.buf);
  if (trait_inited)
    free(trait_type.buf);
  if (args_inited)
    free(type_args.buf);
  if (suffix_inited)
    free(fn_suffix.buf);
  free(name);
  return ok;
}

static int demangle_tag_E(const char **p, mbt_str *out) {
  int ok = 1;
  char *type_pkg = NULL;
  char *type_name = NULL;
  char *method_pkg = NULL;
  char *method_name = NULL;
  if (!mbt_parse_package(p, &type_pkg) ||
      !mbt_parse_identifier(p, &type_name) ||
      !mbt_parse_package(p, &method_pkg) ||
      !mbt_parse_identifier(p, &method_name)) {
    ok = 0;
  } else {
    const char *type_pkg_use = mbt_pkg_is_core(type_pkg) ? "" : type_pkg;
    if (!mbt_str_append_char(out, '@') ||
        (method_pkg && method_pkg[0] && (!mbt_str_append_cstr(out, method_pkg) ||
                                         !mbt_str_append_char(out, '.')))) {
      ok = 0;
    }
    if (ok && type_pkg_use && type_pkg_use[0]) {
      if (!mbt_str_append_cstr(out, type_pkg_use) ||
          !mbt_str_append_char(out, '.'))
        ok = 0;
    }
  if (ok && (!mbt_str_append_cstr(out, type_name) ||
             !mbt_str_append_cstr(out, "::") ||
             !mbt_str_append_cstr(out, method_name))) {
    ok = 0;
  }
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  if (ok && (**p == 'G' || **p == 'H')) {
    if (!mbt_parse_type_args(p, out))
      ok = 0;
  }
  if (ok && !mbt_parse_fn_suffixes(p, out))
    ok = 0;
  }
  free(type_pkg);
  free(type_name);
  free(method_pkg);
  free(method_name);
  return ok;
}

static int demangle_tag_T(const char **p, mbt_str *out) {
  return mbt_append_type_path(p, out, 0);
}

static int demangle_tag_L(const char **p, mbt_str *out) {
  int ok = 1;
  if (**p == 'm')
    (*p)++;
  char *ident = NULL;
  if (!mbt_parse_identifier(p, &ident)) {
    ok = 0;
  } else if (**p != 'S') {
    ok = 0;
  } else {
    (*p)++;
    if (!(**p >= '0' && **p <= '9')) {
      ok = 0;
    } else {
      while (**p >= '0' && **p <= '9')
        (*p)++;
      const char *use = ident;
      if (use[0] == '$')
        use++;
      if (!mbt_str_append_char(out, '@') || !mbt_str_append_cstr(out, use)) {
        ok = 0;
      }
    }
  }
  free(ident);
  return ok;
}

static const char *demangle(const char *func_name, char **owned_out) {
  if (!owned_out)
    return func_name;
  *owned_out = NULL;
  if (!func_name)
    return NULL;
  const char *p = func_name;
  if (p[0] == '$')
    p++;
  if (strlen(p) < 3 || p[0] != '_' || p[1] != 'M' || p[2] != '0')
    return func_name;
  p += 3;

  mbt_str out;
  if (!mbt_str_init(&out))
    return func_name;

  int ok = 1;
  char tag = *p;
  if (!tag)
    ok = 0;
  if (ok)
    p++;

  if (ok) {
    switch (tag) {
      case 'F':
        ok = demangle_tag_F(&p, &out);
        break;
      case 'M':
        ok = demangle_tag_M(&p, &out);
        break;
      case 'I':
        ok = demangle_tag_I(&p, &out);
        break;
      case 'E':
        ok = demangle_tag_E(&p, &out);
        break;
      case 'T':
        ok = demangle_tag_T(&p, &out);
        break;
      case 'L':
        ok = demangle_tag_L(&p, &out);
        break;
      default:
        ok = 0;
        break;
    }
  }

  if (ok && *p != '\0') {
    char c = *p;
    if (!(c == '.' || c == '$' || c == '@'))
      ok = 0;
  }

  if (!ok) {
    free(out.buf);
    return func_name;
  }
  *owned_out = mbt_str_release(&out);
  return *owned_out ? *owned_out : func_name;
}

/* TCCBtFunc signature (see vendor/tinycc/libtcc.h):
   int (*)(void *udata, void *pc, const char *file, int line,
           const char* func, const char *msg)
*/
MOONBIT_EXPORT int moonbit_tcc_backtrace(void *udata, void *pc,
                                        const char *file, int line,
                                        const char *func, const char *msg) {
  (void)udata;

  /* Streamed printing (recommended): TinyCC does not provide an explicit
     "end-of-backtrace" callback, so delaying output until we see "main"
     can drop the whole trace if unwinding stops early. */
  if (msg) {
    moonbit__bt_level = 0;
    moonbit__eprint(msg);
    moonbit__eprint("\n");
  }

  if (!func) {
    return 1; /* skip this frame */
  }

  char *owned_name = NULL;
  const char *func_name = demangle(func, &owned_name);

  {
    char linebuf[512];
    size_t len = 0;
    linebuf[0] = 0;

    /* location: either file:line or address */
    if (file) {
      moonbit__bt_buf_append_cstr(linebuf, sizeof linebuf, &len, file);
      moonbit__bt_buf_append_char(linebuf, sizeof linebuf, &len, ':');
      if (line >= 0)
        moonbit__bt_buf_append_u32_dec(linebuf, sizeof linebuf, &len,
                                       (uint32_t)line);
      else
        moonbit__bt_buf_append_cstr(linebuf, sizeof linebuf, &len, "?");
    } else {
      moonbit__bt_buf_append_ptr_hex(linebuf, sizeof linebuf, &len,
                                     (uintptr_t)(const void *)pc);
    }
    moonbit__bt_buf_append_char(linebuf, sizeof linebuf, &len, ' ');

    moonbit__bt_buf_append_cstr(linebuf, sizeof linebuf, &len,
                                (moonbit__bt_level == 0) ? "at" : "by");
    moonbit__bt_buf_append_char(linebuf, sizeof linebuf, &len, ' ');
    moonbit__bt_buf_append_cstr(linebuf, sizeof linebuf, &len, func_name);
    moonbit__bt_buf_append_cstr(linebuf, sizeof linebuf, &len, "\n");
    moonbit__eprint_n(linebuf, len);
  }

  moonbit__bt_level += 1;
  free(owned_name);

  if ((func && 0 == strcmp(func, "main")) ||
      moonbit__bt_level >= (uint32_t)MOONBIT_NATIVE_BT_MAX_FRAMES)
    return 0;
  return 1;
}

MOONBIT_EXPORT void *moonbit_malloc_array(enum moonbit_block_kind kind,
                                          int elem_size_shift, int32_t len) {
  if (len < 0)
    moonbit_panic();
  int padding = elem_size_shift < 2 ? 1 : 0;
  struct moonbit_object *obj = (struct moonbit_object *)malloc(
      ((len + padding) << elem_size_shift) + sizeof(struct moonbit_object));
  Moonbit_init_dynamic_rc(obj, kind);
  Moonbit_set_meta(obj, (uint32_t)len);
  return obj + 1;
}

MOONBIT_EXPORT moonbit_string_t moonbit_make_string_raw(int32_t len) {
  moonbit_string_t result = (moonbit_string_t)moonbit_malloc_array(
    moonbit_BLOCK_KIND_VAL_ARRAY,
    1,
    len
  );
  result[len] = 0;
  return result;
}

MOONBIT_EXPORT moonbit_bytes_t moonbit_make_bytes_raw(int32_t len) {
  moonbit_bytes_t result = (moonbit_bytes_t)moonbit_malloc_array(
    moonbit_BLOCK_KIND_VAL_ARRAY,
    0,
    len
  );
  result[len] = 0;
  return result;
}

MOONBIT_EXPORT moonbit_string_t moonbit_make_string(int32_t len,
                                                    uint16_t value) {
  uint16_t *str =
      (uint16_t *)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY, 1, len);
  for (int32_t i = 0; i < len; ++i) {
    str[i] = value;
  }
  str[len] = 0;
  return str;
}

MOONBIT_EXPORT int moonbit_val_array_equal_sized(const void *lhs,
                                                 const void *rhs,
                                                 int32_t elem_size) {
  int32_t const len = Moonbit_array_length(lhs);
  if (len != Moonbit_array_length(rhs))
    return 0;

  return 0 == memcmp(lhs, rhs, len * elem_size);
}

MOONBIT_EXPORT moonbit_string_t moonbit_add_string(moonbit_string_t s1,
                                                   moonbit_string_t s2) {
  int32_t const len1 = Moonbit_array_length(s1);
  int32_t const len2 = Moonbit_array_length(s2);
  moonbit_string_t result = (moonbit_string_t)moonbit_malloc_array(
      moonbit_BLOCK_KIND_VAL_ARRAY, 1, len1 + len2);
  memcpy(result, s1, len1 * 2);
  memcpy(result + len1, s2, len2 * 2);
  result[len1 + len2] = 0;
  return result;
}

MOONBIT_EXPORT moonbit_bytes_t moonbit_make_bytes(int32_t size, int init) {
  moonbit_bytes_t result = (moonbit_bytes_t)moonbit_malloc_array(
      moonbit_BLOCK_KIND_VAL_ARRAY, 0, size);
  memset(result, init, size);
  result[size] = 0;
  return result;
}

static int32_t moonbit__utf8_decode_scalar_one(const uint8_t *src,
                                               int32_t len, uint32_t *out) {
  uint8_t b0;
  uint8_t b1;
  uint8_t b2;
  uint8_t b3;

  if (len <= 0)
    return -1;
  b0 = src[0];
  if (b0 < 0x80) {
    *out = b0;
    return 1;
  }
  if (b0 < 0xC2)
    return -1;
  if (b0 < 0xE0) {
    if (len < 2)
      return -1;
    b1 = src[1];
    if ((b1 & 0xC0) != 0x80)
      return -1;
    *out = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
    return 2;
  }
  if (b0 < 0xF0) {
    if (len < 3)
      return -1;
    b1 = src[1];
    b2 = src[2];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80)
      return -1;
    if (b0 == 0xE0 && b1 < 0xA0)
      return -1;
    if (b0 == 0xED && b1 >= 0xA0)
      return -1;
    *out = ((uint32_t)(b0 & 0x0F) << 12) |
           ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
    return 3;
  }
  if (b0 < 0xF5) {
    if (len < 4)
      return -1;
    b1 = src[1];
    b2 = src[2];
    b3 = src[3];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 ||
        (b3 & 0xC0) != 0x80)
      return -1;
    if (b0 == 0xF0 && b1 < 0x90)
      return -1;
    if (b0 == 0xF4 && b1 >= 0x90)
      return -1;
    *out = ((uint32_t)(b0 & 0x07) << 18) |
           ((uint32_t)(b1 & 0x3F) << 12) |
           ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
    return 4;
  }
  return -1;
}

static int32_t moonbit__utf16_write_scalar(uint16_t *dst, int32_t dst_offset,
                                           uint32_t codepoint) {
  if (codepoint < 0x10000) {
    dst[dst_offset] = (uint16_t)codepoint;
    return 1;
  }
  codepoint -= 0x10000;
  dst[dst_offset] = (uint16_t)(0xD800 + (codepoint >> 10));
  dst[dst_offset + 1] = (uint16_t)(0xDC00 + (codepoint & 0x3FF));
  return 2;
}

#ifndef MOONBIT_USE_SIMDUTF
static int32_t moonbit__utf8_decode_into_utf16_scalar(uint16_t *dst,
                                                      int32_t dst_offset,
                                                      const uint8_t *src,
                                                      int32_t len) {
  int32_t i = 0;
  int32_t written = 0;
  while (i < len) {
    uint32_t codepoint;
    int32_t consumed =
        moonbit__utf8_decode_scalar_one(src + i, len - i, &codepoint);
    if (consumed < 0)
      return -(i + 1);
    written += moonbit__utf16_write_scalar(dst, dst_offset + written,
                                           codepoint);
    i += consumed;
  }
  return written;
}
#endif

static int32_t moonbit__utf8_decode_lossy_into_utf16_scalar(uint16_t *dst,
                                                            int32_t dst_offset,
                                                            const uint8_t *src,
                                                            int32_t len) {
  int32_t i = 0;
  int32_t written = 0;
  while (i < len) {
    uint32_t codepoint;
    int32_t consumed =
        moonbit__utf8_decode_scalar_one(src + i, len - i, &codepoint);
    if (consumed < 0) {
      int32_t rest = len - i;
      int32_t advance = 1;
      uint8_t b0 = src[i];
      if (rest >= 2) {
        uint8_t b1 = src[i + 1];
        if ((b0 == 0xE0 && b1 >= 0xA0 && b1 <= 0xBF) ||
            (b0 >= 0xE1 && b0 <= 0xEC && (b1 & 0xC0) == 0x80) ||
            (b0 == 0xED && b1 >= 0x80 && b1 <= 0x9F) ||
            (b0 >= 0xEE && b0 <= 0xEF && (b1 & 0xC0) == 0x80)) {
          advance = 2;
        } else if ((b0 == 0xF0 && b1 >= 0x90 && b1 <= 0xBF) ||
                   (b0 >= 0xF1 && b0 <= 0xF3 && (b1 & 0xC0) == 0x80) ||
                   (b0 == 0xF4 && b1 >= 0x80 && b1 <= 0x8F)) {
          advance = 2;
          if (rest >= 3) {
            uint8_t b2 = src[i + 2];
            if ((b2 & 0xC0) == 0x80) {
              advance = 3;
            }
          }
        }
      }
      dst[dst_offset + written] = 0xFFFD;
      written += 1;
      i += advance;
    } else {
      written += moonbit__utf16_write_scalar(dst, dst_offset + written,
                                             codepoint);
      i += consumed;
    }
  }
  return written;
}

typedef enum moonbit_simdutf_error_code {
  MOONBIT_SIMDUTF_ERROR_SUCCESS = 0,
  MOONBIT_SIMDUTF_ERROR_OTHER
} moonbit_simdutf_error_code;

typedef struct moonbit_simdutf_result {
  moonbit_simdutf_error_code error;
  size_t count;
} moonbit_simdutf_result;

#ifdef MOONBIT_USE_SIMDUTF
size_t moonbit_simdutf_utf16_length_from_utf8(const char *input,
                                              size_t length);
size_t moonbit_simdutf_utf8_length_from_utf16(const uint16_t *input,
                                              size_t length);
moonbit_simdutf_result moonbit_simdutf_convert_utf8_to_utf16_with_errors(
    const char *input, size_t length, uint16_t *output);
moonbit_simdutf_result moonbit_simdutf_convert_utf16_to_utf8_with_errors(
    const uint16_t *input, size_t length, char *output);
#else
static int32_t moonbit__utf8_encode_from_utf16_scalar(uint8_t *dst,
                                                      int32_t dst_offset,
                                                      const uint16_t *src,
                                                      int32_t len) {
  int32_t i = 0;
  int32_t written = 0;
  while (i < len) {
    uint32_t codepoint;
    uint16_t w1 = src[i];
    if (w1 < 0xD800 || w1 > 0xDFFF) {
      codepoint = w1;
      i += 1;
    } else {
      uint16_t w2;
      if (w1 > 0xDBFF || i + 1 >= len)
        return -(i + 1);
      w2 = src[i + 1];
      if (w2 < 0xDC00 || w2 > 0xDFFF)
        return -(i + 1);
      codepoint =
          0x10000 + ((((uint32_t)w1 - 0xD800) << 10) | ((uint32_t)w2 - 0xDC00));
      i += 2;
    }

    if (codepoint < 0x80) {
      dst[dst_offset + written] = (uint8_t)codepoint;
      written += 1;
    } else if (codepoint < 0x800) {
      dst[dst_offset + written] = (uint8_t)(0xC0 | (codepoint >> 6));
      dst[dst_offset + written + 1] = (uint8_t)(0x80 | (codepoint & 0x3F));
      written += 2;
    } else if (codepoint < 0x10000) {
      dst[dst_offset + written] = (uint8_t)(0xE0 | (codepoint >> 12));
      dst[dst_offset + written + 1] =
          (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
      dst[dst_offset + written + 2] = (uint8_t)(0x80 | (codepoint & 0x3F));
      written += 3;
    } else {
      dst[dst_offset + written] = (uint8_t)(0xF0 | (codepoint >> 18));
      dst[dst_offset + written + 1] =
          (uint8_t)(0x80 | ((codepoint >> 12) & 0x3F));
      dst[dst_offset + written + 2] =
          (uint8_t)(0x80 | ((codepoint >> 6) & 0x3F));
      dst[dst_offset + written + 3] = (uint8_t)(0x80 | (codepoint & 0x3F));
      written += 4;
    }
  }
  return written;
}

static size_t moonbit_simdutf_utf16_length_from_utf8(const char *input,
                                                     size_t length) {
  size_t units = 0;
  for (size_t i = 0; i < length; ++i) {
    uint8_t byte = (uint8_t)input[i];
    if (byte < 0x80) {
      units += 1;
    } else if (byte < 0xC0) {
      units += 0;
    } else if (byte < 0xF0) {
      units += 1;
    } else {
      units += 2;
    }
  }
  return units;
}

static size_t moonbit_simdutf_utf8_length_from_utf16(const uint16_t *input,
                                                     size_t length) {
  size_t bytes = 0;
  for (size_t i = 0; i < length; ++i) {
    uint16_t word = input[i];
    bytes += 1;
    bytes += (size_t)(word > 0x7F);
    bytes += (size_t)((word > 0x7FF && word <= 0xD7FF) || word >= 0xE000);
  }
  return bytes;
}

static moonbit_simdutf_result moonbit_simdutf_convert_utf8_to_utf16_with_errors(
    const char *input, size_t length, uint16_t *output) {
  int32_t result = moonbit__utf8_decode_into_utf16_scalar(
      output, 0, (const uint8_t *)input, (int32_t)length);
  moonbit_simdutf_result out;
  if (result < 0) {
    out.error = MOONBIT_SIMDUTF_ERROR_OTHER;
    out.count = (size_t)(-result - 1);
  } else {
    out.error = MOONBIT_SIMDUTF_ERROR_SUCCESS;
    out.count = (size_t)result;
  }
  return out;
}

static moonbit_simdutf_result moonbit_simdutf_convert_utf16_to_utf8_with_errors(
    const uint16_t *input, size_t length, char *output) {
  int32_t result = moonbit__utf8_encode_from_utf16_scalar(
      (uint8_t *)output, 0, input, (int32_t)length);
  moonbit_simdutf_result out;
  if (result < 0) {
    out.error = MOONBIT_SIMDUTF_ERROR_OTHER;
    out.count = (size_t)(-result - 1);
  } else {
    out.error = MOONBIT_SIMDUTF_ERROR_SUCCESS;
    out.count = (size_t)result;
  }
  return out;
}
#endif

MOONBIT_EXPORT int32_t moonbit_utf16_len_from_utf8(moonbit_bytes_t src,
                                                   int32_t src_offset,
                                                   int32_t src_length) {
  return (int32_t)moonbit_simdutf_utf16_length_from_utf8(
      (const char *)(src + src_offset), (size_t)src_length);
}

MOONBIT_EXPORT int32_t moonbit_utf8_decode_into_utf16(
    moonbit_bytes_t src, int32_t src_offset, int32_t src_length,
    moonbit_string_t dst, int32_t dst_offset) {
  uint8_t *input = src + src_offset;
  moonbit_simdutf_result result =
      moonbit_simdutf_convert_utf8_to_utf16_with_errors(
          (const char *)input, (size_t)src_length, dst + dst_offset);
  if (result.error != MOONBIT_SIMDUTF_ERROR_SUCCESS)
    return -((int32_t)result.count + 1);
  return (int32_t)result.count;
}

MOONBIT_EXPORT int32_t moonbit_utf8_decode_lossy_into_utf16(
    moonbit_bytes_t src, int32_t src_offset, int32_t src_length,
    moonbit_string_t dst, int32_t dst_offset) {
  uint8_t *input = src + src_offset;
  return moonbit__utf8_decode_lossy_into_utf16_scalar(dst, dst_offset, input,
                                                      src_length);
}

MOONBIT_EXPORT int32_t moonbit_utf8_len_from_utf16(moonbit_string_t src,
                                                   int32_t src_offset,
                                                   int32_t src_length) {
  uint16_t *input = src + src_offset;
  return (int32_t)moonbit_simdutf_utf8_length_from_utf16(input,
                                                         (size_t)src_length);
}

MOONBIT_EXPORT int32_t moonbit_utf8_encode_from_utf16(
    moonbit_string_t src, int32_t src_offset, int32_t src_length,
    moonbit_bytes_t dst, int32_t dst_offset) {
  uint16_t *input = src + src_offset;
  moonbit_simdutf_result result =
      moonbit_simdutf_convert_utf16_to_utf8_with_errors(
          input, (size_t)src_length, (char *)(dst + dst_offset));
  if (result.error != MOONBIT_SIMDUTF_ERROR_SUCCESS)
    return -((int32_t)result.count + 1);
  return (int32_t)result.count;
}

MOONBIT_EXPORT void moonbit_unsafe_bytes_blit(moonbit_bytes_t dst,
                                              int32_t dst_start,
                                              moonbit_bytes_t src,
                                              int32_t src_offset, int32_t len) {
  memmove(dst + dst_start, src + src_offset, len);
  moonbit_decref(dst);
  moonbit_decref(src);
}

MOONBIT_EXPORT moonbit_string_t moonbit_unsafe_bytes_sub_string(
    moonbit_bytes_t bytes, int32_t start, int32_t len) {
  int32_t str_len = len / 2 + (len & 1);
  moonbit_string_t str = (moonbit_string_t)moonbit_malloc_array(
      moonbit_BLOCK_KIND_VAL_ARRAY, 1, str_len);
  memcpy(str, bytes + start, len);
  str[str_len] = 0;
  moonbit_decref(bytes);
  return str;
}

MOONBIT_EXPORT int32_t moonbit_unsafe_ref_array_blit(void *dst,
                                                     int32_t dst_offset,
                                                     void *src,
                                                     int32_t src_offset,
                                                     int32_t len) {
  void **dst_ptrs = (void **)dst;
  void **src_ptrs = (void **)src;
  int32_t dst_end = dst_offset + len;
  int32_t src_end = src_offset + len;
  int32_t dst_len = Moonbit_array_length(dst_ptrs);
  int32_t src_len = Moonbit_array_length(src_ptrs);
  if (dst_offset < 0 || dst_end > dst_len || src_offset < 0 ||
      src_end > src_len) {
    moonbit_panic();
  }
  struct moonbit_object *src_header = Moonbit_object_header(src_ptrs);
  int32_t const src_rc = src_header->rc;
  if (raw_rc_is_shared(src_rc)) {
    src_header->rc = src_rc - MOONBIT_RC_COUNT_UNIT;
  } else if (raw_rc_is_dynamic(src_rc)) {
    for (int32_t i = 0; i < src_offset; ++i) {
      if (src_ptrs[i])
        moonbit_decref(src_ptrs[i]);
    }
    for (int32_t i = src_end; i < src_len; ++i) {
      if (src_ptrs[i])
        moonbit_decref(src_ptrs[i]);
    }
    for (int32_t i = dst_offset; i < dst_end; ++i) {
      if (dst_ptrs[i])
        moonbit_decref(dst_ptrs[i]);
    }
    // since `src` is unique, it must not overlap with `dst`
    memcpy(dst_ptrs + dst_offset, src_ptrs + src_offset, len * sizeof(void *));
    moonbit_free(src_ptrs);
    moonbit_decref(dst_ptrs);
    return 0;
  }
  for (int32_t i = src_offset; i < src_end; ++i) {
    if (src_ptrs[i])
      moonbit_incref(src_ptrs[i]);
  }
  for (int32_t i = dst_offset; i < dst_end; ++i) {
    if (dst_ptrs[i])
      moonbit_decref(dst_ptrs[i]);
  }
  memmove(dst_ptrs + dst_offset, src_ptrs + src_offset, len * sizeof(void *));
  moonbit_decref(dst_ptrs);
  return 0;
}

MOONBIT_EXPORT int32_t moonbit_unsafe_val_array_blit(uint8_t *dst,
                                                     int32_t dst_offset,
                                                     uint8_t *src,
                                                     int32_t src_offset,
                                                     int32_t len,
                                                     int32_t elem_size) {
  int32_t dst_end = dst_offset + len;
  int32_t src_end = src_offset + len;
  int32_t dst_len = Moonbit_array_length(dst);
  int32_t src_len = Moonbit_array_length(src);
  if (dst_offset < 0 || dst_end > dst_len || src_offset < 0 ||
      src_end > src_len) {
    moonbit_panic();
  }
  memmove(dst + dst_offset * elem_size, src + src_offset * elem_size,
          len * elem_size);
  moonbit_decref(src);
  moonbit_decref(dst);
  return 0;
}

MOONBIT_EXPORT void **moonbit_make_ref_array_with_blit(
    int32_t allocate_len, void *value, void *src, int32_t src_offset,
    int32_t dst_offset, int32_t len) {
  void **dst_ptrs = moonbit_make_ref_array_raw(allocate_len);
  void **src_ptrs = (void **)src;
  int32_t dst_end = dst_offset + len;
  int32_t src_end = src_offset + len;
  int32_t src_len = Moonbit_array_length(src_ptrs);
  int32_t init_slots = allocate_len - len;
  if (value) {
    if (init_slots == 0) {
      moonbit_decref(value);
    } else {
      struct moonbit_object *value_header = Moonbit_object_header(value);
      int32_t const count = Moonbit_rc_count(value_header);
      if (count > 0 && init_slots > 1) {
        Moonbit_set_rc_count(value_header,
                             (uint32_t)count + (uint32_t)init_slots - 1u);
      }
    }
  }
  for (int32_t i = 0; i < dst_offset; ++i) {
    dst_ptrs[i] = value;
  }
  for (int32_t i = dst_end; i < allocate_len; ++i) {
    dst_ptrs[i] = value;
  }
  struct moonbit_object *src_header = Moonbit_object_header(src_ptrs);
  int32_t const src_rc = src_header->rc;
  if (raw_rc_is_shared(src_rc)) {
    src_header->rc = src_rc - MOONBIT_RC_COUNT_UNIT;
  } else if (raw_rc_is_dynamic(src_rc)) {
    for (int32_t i = 0; i < src_offset; ++i) {
      if (src_ptrs[i])
        moonbit_decref(src_ptrs[i]);
    }
    for (int32_t i = src_end; i < src_len; ++i) {
      if (src_ptrs[i])
        moonbit_decref(src_ptrs[i]);
    }
    memcpy(dst_ptrs + dst_offset, src_ptrs + src_offset, len * sizeof(void *));
    moonbit_free(src_ptrs);
    return dst_ptrs;
  }
  for (int32_t i = src_offset; i < src_end; ++i) {
    if (src_ptrs[i])
      moonbit_incref(src_ptrs[i]);
  }
  memcpy(dst_ptrs + dst_offset, src_ptrs + src_offset, len * sizeof(void *));
  return dst_ptrs;
}

MOONBIT_EXPORT void moonbit_println(moonbit_string_t str) {
#ifdef _WIN32
  static HANDLE stdout_handle = INVALID_HANDLE_VALUE;
  static BOOL stdout_is_console = 0;
  static const DWORD max_chunk_size = 1 << 14; // 16K

  if (stdout_handle == INVALID_HANDLE_VALUE) {
    stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    if (stdout_handle == INVALID_HANDLE_VALUE) {
      // There is no stdout. Simply ignore the message
      return;
    }

    DWORD mode;
    stdout_is_console = GetConsoleMode(stdout_handle, &mode);
  }

  if (stdout_is_console) {
    // When stdout is a real console,
    // use `WriteConsoleW` to output with current code page of the console.
    DWORD len = Moonbit_array_length(str);
    DWORD written = 0, total_written = 0;
    while (total_written < len) {
      DWORD chars_to_write = len - total_written;
      if (chars_to_write > max_chunk_size)
        chars_to_write = max_chunk_size;

      BOOL ret = WriteConsoleW(
        stdout_handle,
        ((WCHAR*)str) + total_written,
        chars_to_write,
        &written,
        NULL
      );

      if (!ret) return;
      total_written += written;
    }
    WriteConsoleW(stdout_handle, L"\n", 1, NULL, NULL);
    return;
  }

  // For redirected stdout, output UTF-8
#endif

  int32_t const len = Moonbit_array_length(str);
  for (int32_t i = 0; i < len; ++i) {
    uint32_t c = str[i];
    if (0xD800 <= c && c <= 0xDBFF) {
      c -= 0xD800;
      i = i + 1;
      uint32_t l = str[i] - 0xDC00;
      c = ((c << 10) + l) + 0x10000;
    }
    // stdout accepts UTF-8, so convert the stream to UTF-8 first
    if (c < 0x80) {
      putchar(c);
    } else if (c < 0x800) {
      putchar(0xc0 + (c >> 6));
      putchar(0x80 + (c & 0x3f));
    } else if (c < 0x10000) {
      putchar(0xe0 + (c >> 12));
      putchar(0x80 + ((c >> 6) & 0x3f));
      putchar(0x80 + (c & 0x3f));
    } else {
      putchar(0xf0 + (c >> 18));
      putchar(0x80 + ((c >> 12) & 0x3f));
      putchar(0x80 + ((c >> 6) & 0x3f));
      putchar(0x80 + (c & 0x3f));
    }
  }
  putchar('\n');
}

MOONBIT_EXPORT int32_t *moonbit_make_int32_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_int32_array;
  return (int32_t *)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY, 2, len);
}

MOONBIT_EXPORT int32_t *moonbit_make_int32_array(int32_t len, int32_t value) {
  int32_t *arr = moonbit_make_int32_array_raw(len);
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

MOONBIT_EXPORT void **moonbit_make_ref_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_ref_array;
  return (void **)moonbit_malloc_array(moonbit_BLOCK_KIND_REF_ARRAY,
                                       (sizeof(void *) >> 2) + 1, len);
}

MOONBIT_EXPORT void **moonbit_make_ref_array(int32_t len, void *value) {
  if (len == 0) {
    if (value)
      moonbit_decref(value);
    return moonbit_empty_ref_array;
  }

  void **arr = moonbit_make_ref_array_raw(len);

  if (value) {
    struct moonbit_object *value_header = Moonbit_object_header(value);
    const int32_t count = Moonbit_rc_count(value_header);
    if (count > 0 && len > 1) {
      Moonbit_set_rc_count(value_header, (uint32_t)count + (uint32_t)len - 1u);
    }
  }
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

MOONBIT_EXPORT void **moonbit_make_extern_ref_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_extern_ref_array;
  return (void **)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY,
                                       (sizeof(void *) >> 2) + 1, len);
}

MOONBIT_EXPORT void **moonbit_make_extern_ref_array(int32_t len, void *value) {
  void **arr = moonbit_make_extern_ref_array_raw(len);
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

MOONBIT_EXPORT int64_t *moonbit_make_int64_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_int64_array;
  return (int64_t *)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY, 3, len);
}

MOONBIT_EXPORT int64_t *moonbit_make_int64_array(int32_t len, int64_t value) {
  int64_t *arr = moonbit_make_int64_array_raw(len);
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

MOONBIT_EXPORT double *moonbit_make_double_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_double_array;
  return (double *)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY, 3, len);
}

MOONBIT_EXPORT double *moonbit_make_double_array(int32_t len, double value) {
  double *arr = moonbit_make_double_array_raw(len);
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

MOONBIT_EXPORT float *moonbit_make_float_array_raw(int32_t len) {
  if (len == 0)
    return moonbit_empty_float_array;
  return (float *)moonbit_malloc_array(moonbit_BLOCK_KIND_VAL_ARRAY, 2, len);
}

MOONBIT_EXPORT float *moonbit_make_float_array(int32_t len, float value) {
  float *arr = moonbit_make_float_array_raw(len);
  for (int32_t i = 0; i < len; ++i) {
    arr[i] = value;
  }
  return arr;
}

static struct {
  int32_t rc;
  uint32_t meta;
  void *data[];
} moonbit_empty_scalar_valtype_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT void *const moonbit_empty_scalar_valtype_array =
    moonbit_empty_scalar_valtype_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  moonbit_v128_storage_t data[];
} moonbit_empty_v128_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT moonbit_v128_storage_t *const moonbit_empty_v128_array =
    moonbit_empty_v128_array_object.data;

MOONBIT_EXPORT void *moonbit_make_scalar_valtype_array(int32_t len,
                                                       size_t valtype_size,
                                                       void *init) {
  void *array = moonbit_make_scalar_valtype_array_raw(len, valtype_size);
  if (array) {
    for (int32_t i = 0; i < len; ++i) {
      memcpy((uint8_t *)array + i * valtype_size, init, valtype_size);
    }
  }
  return array;
}

MOONBIT_EXPORT void *
moonbit_make_scalar_valtype_array_raw(int32_t len, size_t valtype_size) {
  if (len < 0)
    moonbit_panic();
  if (len == 0)
    return moonbit_empty_scalar_valtype_array;
  // All-scalar value-type arrays have no references to scan, so they use the
  // ordinary all-scalar array representation and do not carry the extra
  // value-type-array header.
  struct moonbit_object *obj = (struct moonbit_object *)malloc(
      len * valtype_size + sizeof(struct moonbit_object));
  Moonbit_init_dynamic_rc(obj, moonbit_BLOCK_KIND_VAL_ARRAY);
  Moonbit_set_meta(obj, (uint32_t)len);
  return (void *)(obj + 1);
}

MOONBIT_EXPORT moonbit_v128_storage_t *
moonbit_make_v128_array(int32_t len, uint64_t lo, uint64_t hi) {
  moonbit_v128_storage_t *array = moonbit_make_v128_array_raw(len);
  if (array) {
    for (int32_t i = 0; i < len; ++i) {
      array[i].lo = lo;
      array[i].hi = hi;
    }
  }
  return array;
}

MOONBIT_EXPORT moonbit_v128_storage_t *moonbit_make_v128_array_raw(int32_t len) {
  if (len < 0)
    moonbit_panic();
  if (len == 0)
    return moonbit_empty_v128_array;
  struct moonbit_object *obj = (struct moonbit_object *)malloc(
      (size_t)len * sizeof(moonbit_v128_storage_t) +
      sizeof(struct moonbit_object));
  Moonbit_init_dynamic_rc(obj, moonbit_BLOCK_KIND_VAL_ARRAY);
  Moonbit_set_meta(obj, (uint32_t)len);
  return (moonbit_v128_storage_t *)(obj + 1);
}

static struct {
  int32_t rc;
  uint32_t meta;
  void *data[];
} moonbit_empty_ref_valtype_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REF_VALTYPE_ARRAY),
    0};

MOONBIT_EXPORT void *const moonbit_empty_ref_valtype_array =
    moonbit_empty_ref_valtype_array_object.data;

MOONBIT_EXPORT void moonbit_update_ref_valtype_rc(int32_t len, void *value,
                                                  uint32_t layout_meta) {
  // Static references of a value-struct element. Embedded value enums (is_venum
  // children) carry their own tag-dependent references and are adjusted by
  // moonbit_update_ref_enum_valtype_rc instead, so skip them here.
  uint32_t n = Moonbit_header_child_count(layout_meta);
  if (len == 0) {
    for (uint32_t i = 0; i < n; ++i) {
      if (Moonbit_header_child_tagged(layout_meta, i) & 1u)
        continue;
      void **ptrs = ref_slot_at(layout_meta, value, (int32_t)i);
      if (*ptrs)
        moonbit_decref(*ptrs);
    }
  } else if (len > 1) {
    for (uint32_t i = 0; i < n; ++i) {
      if (Moonbit_header_child_tagged(layout_meta, i) & 1u)
        continue;
      void **ptrs = ref_slot_at(layout_meta, value, (int32_t)i);
      if (*ptrs) {
        struct moonbit_object *value_header = Moonbit_object_header(*ptrs);
        const int32_t count = Moonbit_rc_count(value_header);
        if (count > 0) {
          Moonbit_set_rc_count(value_header,
                               (uint32_t)count + (uint32_t)len - 1u);
        }
      }
    }
  }
}

MOONBIT_EXPORT void *moonbit_make_ref_valtype_array(int32_t len,
                                                    size_t valtype_size,
                                                    uint32_t layout_meta,
                                                    void *init) {
  void *array =
      moonbit_make_ref_valtype_array_raw(len, valtype_size, layout_meta);
  if (array) {
    for (int32_t i = 0; i < len; ++i) {
      memcpy((uint8_t *)array + i * valtype_size, init, valtype_size);
    }
  }
  return array;
}

MOONBIT_EXPORT void *moonbit_make_ref_valtype_array_raw(int32_t len,
                                                        size_t valtype_size,
                                                        uint32_t layout_meta) {
  if (len < 0)
    moonbit_panic();
  if (len == 0)
    return moonbit_empty_ref_valtype_array;
  // the extra header is 4-byte but we allocate extra 8-byte for better
  // alignment
  size_t const total_size =
      len * valtype_size + sizeof(struct moonbit_object) + sizeof(void *);
  struct moonbit_object *obj = (struct moonbit_object *)malloc(total_size);
  *(uint64_t *)obj = (uint64_t)layout_meta;
  obj = (struct moonbit_object *)(((uint64_t *)obj) + 1);
  Moonbit_init_dynamic_rc(obj, moonbit_BLOCK_KIND_REF_VALTYPE_ARRAY);
  Moonbit_set_meta(obj, (uint32_t)len);
  return (void *)(obj + 1);
}

MOONBIT_EXPORT void *moonbit_make_external_object(void (*finalize)(void *self),
                                                  uint32_t payload_size) {
  void *result = moonbit_malloc(sizeof(void (*)(void *)) + payload_size);
  Moonbit_set_meta(Moonbit_object_header(result),
                   Moonbit_make_external_object_header(payload_size));
  void (**addr_of_finalize)(void *) =
      (void (**)(void *))((uint8_t *)result + payload_size);
  *addr_of_finalize = finalize;
  return result;
}

static struct {
  int32_t rc;
  uint32_t meta;
  uint8_t data[];
} moonbit_empty_int8_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT uint8_t *const moonbit_empty_int8_array =
    moonbit_empty_int8_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  uint16_t data[];
} moonbit_empty_int16_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT uint16_t *const moonbit_empty_int16_array =
    moonbit_empty_int16_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  int32_t data[];
} moonbit_empty_int32_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT int32_t *const moonbit_empty_int32_array =
    moonbit_empty_int32_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  int64_t data[];
} moonbit_empty_int64_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT int64_t *const moonbit_empty_int64_array =
    moonbit_empty_int64_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  float data[];
} moonbit_empty_float_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT float *const moonbit_empty_float_array =
    moonbit_empty_float_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  double data[];
} moonbit_empty_double_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT double *const moonbit_empty_double_array =
    moonbit_empty_double_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  void *data[];
} moonbit_empty_ref_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_REF_ARRAY),
    0};

MOONBIT_EXPORT void **const moonbit_empty_ref_array =
    moonbit_empty_ref_array_object.data;

static struct {
  int32_t rc;
  uint32_t meta;
  void *data[];
} moonbit_empty_extern_ref_array_object = {
    Moonbit_make_static_rc(moonbit_BLOCK_KIND_VAL_ARRAY),
    0};

MOONBIT_EXPORT void **const moonbit_empty_extern_ref_array =
    moonbit_empty_extern_ref_array_object.data;

static int __moonbit_internal_argc = 0;
static char **__moonbit_internal_argv = 0;

MOONBIT_EXPORT moonbit_bytes_t *moonbit_get_cli_args(void) {
  moonbit_bytes_t *result =
      (moonbit_bytes_t *)moonbit_make_ref_array(__moonbit_internal_argc, 0);
  for (int i = 0; i < __moonbit_internal_argc; ++i) {
    int len = strlen(__moonbit_internal_argv[i]);
    moonbit_bytes_t arg = moonbit_make_bytes(len, 0);
    memcpy(arg, __moonbit_internal_argv[i], len);
    result[i] = arg;
  }
  return result;
}

MOONBIT_EXPORT void moonbit_runtime_init(int argc, char **argv) {
  __moonbit_internal_argc = argc;
  __moonbit_internal_argv = argv;
}

#ifdef _WIN32

typedef moonbit_string_t moonbit_os_string_t;
#define moonbit_empty_os_string moonbit_empty_int16_array

#else

typedef moonbit_bytes_t moonbit_os_string_t;
#define moonbit_empty_os_string moonbit_empty_int8_array

#endif


#ifndef MOONBIT_NATIVE_NO_SYS_HEADER

#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32

#include <direct.h>

#else // #ifdef _WIN32

#include <dirent.h>
#include <sys/random.h>
#include <sys/time.h>
#include <unistd.h>

#endif // #ifndef _WIN32

#endif // #ifndef MOONBIT_NATIVE_NO_SYS_HEADER

MOONBIT_EXPORT int32_t moonbit_rt_get_random(moonbit_bytes_t buf) {
  int32_t len = Moonbit_array_length(buf);
  if (len <= 0) return 0;
#ifdef MOONBIT_NATIVE_NO_SYS_HEADER
  return -1;
#elif defined(_MSC_VER)
  NTSTATUS status =
      BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return status != 0 ? -1 : 0;
#elif defined(_WIN32)
  for (int32_t i = 0; i < len; i += 4) {
    unsigned int word;
    if (rand_s(&word) != 0) return -1;
    int32_t copy = (len - i < 4) ? (len - i) : 4;
    memcpy(buf + i, &word, copy);
  }
  return 0;
#else
  for (int32_t i = 0; i < len;) {
    size_t chunk = (size_t)(len - i);
    if (chunk > 256) chunk = 256;
    if (getentropy(buf + i, chunk) != 0) return -1;
    i += (int32_t)chunk;
  }
  return 0;
#endif
}

MOONBIT_FFI_EXPORT
moonbit_os_string_t *moonbit_rt_get_cli_args(void) {
#ifdef _MSC_VER

  LPWSTR command_line = GetCommandLineW();

  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(command_line, &argc);

  if (!argv)
    return (moonbit_os_string_t*)moonbit_empty_ref_array;

  moonbit_os_string_t *result = (moonbit_os_string_t*)moonbit_make_ref_array_raw(argc);
  for (int i = 0; i < argc; ++i) {
    LPWSTR raw_arg = argv[i];
    int len = wcslen(raw_arg);
    moonbit_os_string_t arg = moonbit_make_string_raw(len);
    memcpy(arg, raw_arg, len * sizeof(WCHAR));
    result[i] = arg;
  }

  LocalFree(argv);
  return result;

#elif defined(_WIN32)

  // mingw does not support `#pragma comment`,
  // so we cannot link `Shell32` and use `CommandLineToArgvW`.
  // Fallback to (unicode-unsafe) ANSI processing.
  // Note that the MoonBit side still requires UTF-16 string,
  // so we need to manually do the translation here.
  moonbit_os_string_t *result =
    (moonbit_os_string_t*)moonbit_make_ref_array_raw(__moonbit_internal_argc);

  for (int i = 0; i < __moonbit_internal_argc; ++i) {
    int len = strlen(__moonbit_internal_argv[i]);
    moonbit_os_string_t arg = moonbit_make_string_raw(len);

    // Assume ANSI only, decode to UTF16-LE
    for (int j = 0; j < len; ++j) {
      ((char*)arg)[j * 2] = __moonbit_internal_argv[i][j];
      ((char*)arg)[j * 2 + 1] = 0;
    }

    result[i] = arg;
  }

  return result;

#else

  moonbit_os_string_t *result =
    (moonbit_os_string_t*)moonbit_make_ref_array_raw(__moonbit_internal_argc);

  for (int i = 0; i < __moonbit_internal_argc; ++i) {
    int len = strlen(__moonbit_internal_argv[i]);
    moonbit_os_string_t arg = moonbit_make_bytes_raw(len);
    memcpy(arg, __moonbit_internal_argv[i], len);
    result[i] = arg;
  }

  return result;

#endif
}


#ifndef MOONBIT_NATIVE_NO_SYS_HEADER

MOONBIT_FFI_EXPORT
moonbit_os_string_t moonbit_rt_get_current_dir(void) {
#ifdef _WIN32

  WCHAR buf[1024];

  DWORD size = GetCurrentDirectoryW(1024, buf);
  moonbit_os_string_t result;

  if (size < 1024) {
    result = moonbit_make_string_raw(size);
    memcpy(result, buf, size * sizeof(WCHAR));
  } else {
    result = moonbit_make_string_raw(size - 1);
    GetCurrentDirectoryW(size, result);
  }

  return result;

#else

  char buf[1024];
  char *cwd = buf;

  if (!getcwd(buf, 1024)) {
    if (errno != ERANGE)
      return moonbit_empty_os_string;

    cwd = getcwd(NULL, 0);
  }

  int len = strlen(cwd);
  moonbit_os_string_t result = moonbit_make_bytes_raw(len);
  memcpy(result, cwd, len);

  if (cwd != buf)
    free(cwd);

  return result;

#endif
}

MOONBIT_EXPORT
moonbit_os_string_t moonbit_rt_get_env_var2(moonbit_os_string_t key, int32_t *exists) {
#ifdef _WIN32

  WCHAR buffer[1024];

  SetLastError(0);
  DWORD len = GetEnvironmentVariableW((LPCWSTR)key, buffer, sizeof(buffer) / sizeof(WCHAR));

  if (len == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    *exists = 0;
    return moonbit_empty_os_string;
  }

  *exists = 1;
  moonbit_os_string_t result;
  if (len < 1024) {
    // The static buffer is large enough to hold the data,
    // no need for an extra `GetEnvironmentVariablesW` call.
    result = moonbit_make_string_raw(len);
    memcpy(result, buffer, len * sizeof(WCHAR));
  } else {
    // In case the buffer is not large enough,
    // the returned length INCLUDES the terminating NUL.
    // Since `String` has its own trailing NUL in MoonBit,
    // exclude the trailing NUL when allocating the result.
    result = moonbit_make_string_raw(len - 1);
    GetEnvironmentVariableW((LPCWSTR)key, result, len);
  }

  return result;

#else

  const char *value = getenv((const char*)key);
  if (!value) {
    *exists = 0;
    return moonbit_empty_os_string;
  }

  *exists = 1;
  int len = strlen(value);
  moonbit_os_string_t result = moonbit_make_bytes_raw(len);
  memcpy(result, value, len);

  return result;

#endif
}

MOONBIT_EXPORT
moonbit_os_string_t *moonbit_rt_get_env_vars2(void) {
#ifdef _WIN32

  LPWCH env_str = GetEnvironmentStringsW();
  if (!env_str)
    return (moonbit_os_string_t*)moonbit_empty_ref_array;

  // `env_str` is a NUL-separated list of environment block entries.
  // Each entry can be either:
  // 
  // 1. a normal entry of the form `A=B`
  // 2. a pseudo entry of the from `=...`. We should skip this
  // 
  // an empty entry indicates end of the list

  // count the first block
  int count = (env_str[0] != '=' && env_str[0] != 0) ? 1 : 0;
  for (int i = 0;; ++i) {
    // end of a single env var block is indicated by '\0'
    if (env_str[i] != 0)
      continue;

    // an empty block indicates end of the whole environment string
    if (env_str[i + 1] == 0)
      break;

    // count the next block
    if (env_str[i + 1] != '=')
      ++count;
  }

  moonbit_os_string_t *result = (moonbit_os_string_t*)moonbit_make_ref_array_raw(count * 2);

  LPWCH start_of_block = env_str;
  for (int block_index = 0; block_index < count;) {
    int key_len = -1, block_len = 0;
    while (start_of_block[block_len] != 0) {
      if (start_of_block[block_len] == L'=' && key_len < 0)
        key_len = block_len;

      ++block_len;
    }

    if (key_len < 0)
      // this should not happen, but if there is no `=` in the env block,
      // treat the whole block as key and the value as empty
      key_len = block_len;

    if (key_len > 0) {
      // exclude pseudo environment entry
      moonbit_os_string_t key = moonbit_make_string_raw(key_len);
      memcpy(key, start_of_block, key_len * sizeof(WCHAR));

      int value_len = block_len - key_len - 1;
      moonbit_os_string_t value = moonbit_make_string_raw(value_len);
      memcpy(value, start_of_block + key_len + 1, value_len * sizeof(WCHAR));

      result[block_index * 2] = key;
      result[block_index * 2 + 1] = value;

      ++block_index;
    }

    start_of_block += block_len + 1;
  }

  FreeEnvironmentStringsW(env_str);
  return result;

#else

  extern char **environ;

  int count = 0;
  char **env = environ;
  while (*env != NULL) {
    count++;
    env++;
  }

  moonbit_os_string_t *result = (moonbit_os_string_t*)moonbit_make_ref_array_raw(count * 2);
  env = environ;
  int i = 0;
  while (*env != NULL) {
    char *equals = strchr(*env, '=');
    if (equals != NULL) {
      size_t key_len = equals - *env;
      size_t val_len = strlen(equals + 1);

      moonbit_bytes_t key_bytes = moonbit_make_bytes_raw(key_len);
      memcpy(key_bytes, *env, key_len);

      moonbit_bytes_t value_bytes = moonbit_make_bytes_raw(val_len);
      memcpy(value_bytes, equals + 1, val_len);

      result[i * 2] = key_bytes;
      result[(i * 2) + 1] = value_bytes;
    }
    env++;
    i++;
  }
  return result;

#endif
}

MOONBIT_EXPORT
void moonbit_rt_set_env_var2(moonbit_os_string_t key, moonbit_os_string_t value) {
#ifdef _WIN32
  SetEnvironmentVariableW((LPCWSTR)key, (LPCWSTR)value);
#else
  setenv((const char *)key, (const char *)value, 1);
#endif
}

MOONBIT_EXPORT
void moonbit_rt_unset_env_var2(moonbit_os_string_t key) {
#ifdef _WIN32
  SetEnvironmentVariableW((LPCWSTR)key, NULL);
#else
  unsetenv((const char *)key);
#endif
}

/* Environment variable format notes:
   - On Unix/POSIX, `environ` is a NULL-terminated array of byte strings in
     `KEY=VALUE` form. Both key and value are treated as opaque bytes here.
   - On Windows, `GetEnvironmentStringsA` returns a double-NULL-terminated
     block of ANSI strings in `KEY=VALUE` form. Entries that start with '=' are
     pseudo variables (for example `=C:=C:\path`) and are skipped.
   - This runtime intentionally keeps env keys/values as raw bytes; encoding and
     decoding are handled at higher layers.
   - Exported symbol names are namespaced with `moonbit_rt_` to avoid collisions
     with package-local stubs that may define generic names like `get_env_var`.
*/
MOONBIT_EXPORT moonbit_bytes_t moonbit_rt_get_env_var(moonbit_bytes_t key) {
#ifdef _WIN32
  DWORD buf_size = GetEnvironmentVariableA((LPCSTR)key, NULL, 0);
  if (buf_size == 0) {
    return moonbit_make_bytes(0, 0);
  }
  moonbit_bytes_t result = moonbit_make_bytes(buf_size - 1, 0);
  GetEnvironmentVariableA((LPCSTR)key, (LPSTR)result, buf_size);
  return result;
#else
  char *value = getenv((const char *)key);
  if (value == NULL) {
    return moonbit_make_bytes(0, 0);
  }
  size_t len = strlen(value);
  moonbit_bytes_t result = moonbit_make_bytes(len, 0);
  memcpy(result, value, len);
  return result;
#endif
}

MOONBIT_EXPORT int32_t moonbit_rt_get_env_var_exists(moonbit_bytes_t key) {
#ifdef _WIN32
  DWORD buf_size = GetEnvironmentVariableA((LPCSTR)key, NULL, 0);
  return buf_size != 0;
#else
  char *value = getenv((const char *)key);
  return value != NULL;
#endif
}

MOONBIT_EXPORT moonbit_bytes_t *moonbit_rt_get_env_vars(void) {
#ifdef _WIN32
  LPCH env_block = GetEnvironmentStringsA();
  if (env_block == NULL) {
    return (moonbit_bytes_t *)moonbit_make_ref_array(0, NULL);
  }

  int count = 0;
  LPCH env = env_block;
  while (*env) {
    char *equals = strchr(env, '=');
    if (env[0] != '=' && equals != NULL && equals != env) {
      count++;
    }
    env += strlen(env) + 1;
  }

  moonbit_bytes_t *result =
      (moonbit_bytes_t *)moonbit_make_ref_array(count * 2, NULL);

  env = env_block;
  int i = 0;
  while (*env) {
    char *equals = strchr(env, '=');
    if (env[0] != '=' && equals != NULL && equals != env) {
      size_t key_len = equals - env;
      size_t val_len = strlen(equals + 1);

      moonbit_bytes_t key_bytes = moonbit_make_bytes(key_len, 0);
      memcpy(key_bytes, env, key_len);

      moonbit_bytes_t value_bytes = moonbit_make_bytes(val_len, 0);
      memcpy(value_bytes, equals + 1, val_len);

      result[i * 2] = key_bytes;
      result[(i * 2) + 1] = value_bytes;
      i++;
    }
    env += strlen(env) + 1;
  }

  FreeEnvironmentStringsA(env_block);
  return result;
#else
  extern char **environ;

  int count = 0;
  char **env = environ;
  while (*env != NULL) {
    count++;
    env++;
  }

  moonbit_bytes_t *result =
      (moonbit_bytes_t *)moonbit_make_ref_array(count * 2, NULL);
  env = environ;
  int i = 0;
  while (*env != NULL) {
    char *equals = strchr(*env, '=');
    if (equals != NULL) {
      size_t key_len = equals - *env;
      size_t val_len = strlen(equals + 1);

      moonbit_bytes_t key_bytes = moonbit_make_bytes(key_len, 0);
      memcpy(key_bytes, *env, key_len);

      moonbit_bytes_t value_bytes = moonbit_make_bytes(val_len, 0);
      memcpy(value_bytes, equals + 1, val_len);

      result[i * 2] = key_bytes;
      result[(i * 2) + 1] = value_bytes;
    }
    env++;
    i++;
  }
  return result;
#endif
}

MOONBIT_EXPORT void moonbit_rt_set_env_var(moonbit_bytes_t key,
                                           moonbit_bytes_t value) {
#ifdef _WIN32
  SetEnvironmentVariableA((LPCSTR)key, (LPCSTR)value);
#else
  setenv((const char *)key, (const char *)value, 1);
#endif
}

MOONBIT_EXPORT void moonbit_rt_unset_env_var(moonbit_bytes_t key) {
#ifdef _WIN32
  SetEnvironmentVariableA((LPCSTR)key, NULL);
#else
  unsetenv((const char *)key);
#endif
}

MOONBIT_EXPORT FILE *moonbit_fopen_ffi(moonbit_bytes_t path,
                                       moonbit_bytes_t mode) {
  return fopen((const char *)path, (const char *)mode);
}

MOONBIT_EXPORT int moonbit_is_null(void *ptr) { return ptr == NULL; }

MOONBIT_EXPORT size_t moonbit_fread_ffi(moonbit_bytes_t ptr, int size,
                                        int nitems, FILE *stream) {
  return fread(ptr, size, nitems, stream);
}

MOONBIT_EXPORT size_t moonbit_fwrite_ffi(moonbit_bytes_t ptr, int size,
                                         int nitems, FILE *stream) {
  return fwrite(ptr, size, nitems, stream);
}

MOONBIT_EXPORT int moonbit_fseek_ffi(FILE *stream, long offset, int whence) {
  return fseek(stream, offset, whence);
}

MOONBIT_EXPORT long moonbit_ftell_ffi(FILE *stream) { return ftell(stream); }

MOONBIT_EXPORT int moonbit_fflush_ffi(FILE *file) { return fflush(file); }

MOONBIT_EXPORT int moonbit_fclose_ffi(FILE *stream) { return fclose(stream); }

MOONBIT_EXPORT moonbit_bytes_t moonbit_get_error_message(void) {
  const char *err_str = strerror(errno);
  size_t len = strlen(err_str);
  moonbit_bytes_t bytes = moonbit_make_bytes(len, 0);
  memcpy(bytes, err_str, len);
  return bytes;
}

MOONBIT_EXPORT int moonbit_stat_ffi(moonbit_bytes_t path) {
  struct stat buffer;
  int status = stat((const char *)path, &buffer);
  return status;
}

MOONBIT_EXPORT int moonbit_is_dir_ffi(moonbit_bytes_t path) {
#ifdef _WIN32
  DWORD attrs = GetFileAttributes((const char *)path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return -1;
  }
  if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
    return 1;
  }
  return 0;
#else
  struct stat buffer;
  int status = stat((const char *)path, &buffer);
  if (status == -1) {
    return -1;
  }
  if (S_ISDIR(buffer.st_mode)) {
    return 1;
  }
  return 0;
#endif
}

MOONBIT_EXPORT int moonbit_is_file_ffi(moonbit_bytes_t path) {
#ifdef _WIN32
  DWORD attrs = GetFileAttributes((const char *)path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return -1;
  }
  if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return 1;
  }
  return 0;
#else
  struct stat buffer;
  int status = stat((const char *)path, &buffer);
  if (status == -1) {
    return -1;
  }
  if (S_ISREG(buffer.st_mode)) {
    return 1;
  }
  return 0;
#endif
}

MOONBIT_EXPORT int moonbit_remove_dir_ffi(moonbit_bytes_t path) {
#ifdef _WIN32
  return _rmdir((const char *)path);
#else
  return rmdir((const char *)path);
#endif
}

MOONBIT_EXPORT int moonbit_remove_file_ffi(moonbit_bytes_t path) {
  return remove((const char *)path);
}

MOONBIT_EXPORT int moonbit_create_dir_ffi(moonbit_bytes_t path) {
#ifdef _WIN32
  return _mkdir((const char *)path);
#else
  return mkdir((const char *)path, 0777);
#endif
}

MOONBIT_EXPORT moonbit_bytes_t *moonbit_read_dir_ffi(moonbit_bytes_t path) {
#ifdef _WIN32
  WIN32_FIND_DATA find_data;
  HANDLE dir;
  moonbit_bytes_t *result = NULL;
  int count = 0;

  size_t path_len = strlen((const char *)path);
  char *search_path = malloc(path_len + 3);
  if (search_path == NULL) {
    return NULL;
  }

  sprintf(search_path, "%s\\*", (const char *)path);
  dir = FindFirstFile(search_path, &find_data);
  if (dir == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    fprintf(stderr, "Failed to open directory: error code %lu\n", error);
    free(search_path);
    return NULL;
  }

  do {
    if (find_data.cFileName[0] != '.') {
      count++;
    }
  } while (FindNextFile(dir, &find_data));

  FindClose(dir);
  dir = FindFirstFile(search_path, &find_data);
  free(search_path);

  result = (moonbit_bytes_t *)moonbit_make_ref_array(count, NULL);
  if (result == NULL) {
    FindClose(dir);
    return NULL;
  }

  int index = 0;
  do {
    if (find_data.cFileName[0] != '.') {
      size_t name_len = strlen(find_data.cFileName);
      moonbit_bytes_t item = moonbit_make_bytes(name_len, 0);
      memcpy(item, find_data.cFileName, name_len);
      result[index++] = item;
    }
  } while (FindNextFile(dir, &find_data));

  FindClose(dir);
  return result;
#else

  DIR *dir;
  struct dirent *entry;
  moonbit_bytes_t *result = NULL;
  int count = 0;

  // open the directory
  dir = opendir((const char *)path);
  if (dir == NULL) {
    perror("opendir");
    return NULL;
  }

  // first traversal of the directory, calculate the number of items
  while ((entry = readdir(dir)) != NULL) {
    // ignore hidden files and current/parent directories
    if (entry->d_name[0] != '.') {
      count++;
    }
  }

  // reset the directory stream
  rewinddir(dir);

  // create moonbit_ref_array to store the result
  result = (moonbit_bytes_t *)moonbit_make_ref_array(count, NULL);
  if (result == NULL) {
    closedir(dir);
    return NULL;
  }

  // second traversal of the directory, fill the array
  int index = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] != '.') {
      size_t name_len = strlen(entry->d_name);
      moonbit_bytes_t item = moonbit_make_bytes(name_len, 0);
      memcpy(item, entry->d_name, name_len);
      result[index++] = item;
    }
  }

  closedir(dir);
  return result;
#endif
}

static void timestamp_finalizer(void *dummy) { (void)dummy; }

#ifdef __APPLE__
#define MOONBIT_CLOCK_MONOTONIC CLOCK_MONOTONIC_RAW
#else
#define MOONBIT_CLOCK_MONOTONIC CLOCK_MONOTONIC
#endif

#ifdef _WIN32

struct timestamp {
  LARGE_INTEGER ts;
};

MOONBIT_EXPORT void *moonbit_monotonic_clock_start(void) {
  struct timestamp *ts = moonbit_make_external_object(timestamp_finalizer,
                                                      sizeof(struct timestamp));
  QueryPerformanceCounter(&ts->ts);
  return ts;
}

MOONBIT_EXPORT double moonbit_monotonic_clock_stop(void *prev) {
  LARGE_INTEGER counter;
  (void)QueryPerformanceCounter(&counter);

  static LARGE_INTEGER freq;
  if (freq.QuadPart == 0) // initialize only once
    (void)QueryPerformanceFrequency(&freq);

  struct timestamp *ts = (struct timestamp *)prev;
  return (double)(counter.QuadPart - ts->ts.QuadPart) /
         (double)freq.QuadPart * 1e6;
}

MOONBIT_FFI_EXPORT
uint64_t moonbit_get_ms_since_epoch() {
  FILETIME file_time;
  GetSystemTimeAsFileTime(&file_time);
  uint64_t result = (uint64_t)file_time.dwHighDateTime << 32;
  result |= file_time.dwLowDateTime;
  return result / 10000 - 11644473600000;
}

#else

struct timestamp {
  struct timespec ts;
};

MOONBIT_EXPORT void *moonbit_monotonic_clock_start(void) {
  struct timestamp *ts = moonbit_make_external_object(timestamp_finalizer,
                                                      sizeof(struct timestamp));
  if (0 == clock_gettime(MOONBIT_CLOCK_MONOTONIC, &ts->ts))
    return ts;
  memset(ts, 0, sizeof(struct timestamp));
  return ts;
}

MOONBIT_EXPORT double moonbit_monotonic_clock_stop(void *prev) {
  struct timespec ts;
  if (0 != clock_gettime(MOONBIT_CLOCK_MONOTONIC, &ts))
    return NAN;
  struct timespec *ts0 = &(((struct timestamp *)prev)->ts);
  return (double)(ts.tv_sec - ts0->tv_sec) * 1000000 +
         (double)(ts.tv_nsec - ts0->tv_nsec) * 0.001;
}

MOONBIT_FFI_EXPORT
uint64_t moonbit_get_ms_since_epoch() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

#endif

#endif

#ifdef __cplusplus
}
#endif
