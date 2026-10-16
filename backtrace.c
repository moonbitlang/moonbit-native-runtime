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

#define MOONBIT_BUILD_RUNTIME
#include "moonbit.h"

#ifdef _MSC_VER
#define _Noreturn __declspec(noreturn)
#endif

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER

_Noreturn void exit(int status);
_Noreturn void abort(void);

void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *memcpy(void *dst, const void *src, size_t n);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

long write(int fd, const void *buf, size_t n);

#ifndef NULL
#define NULL ((void*)0)
#endif

#else

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#endif

#ifdef _WIN32

#include <windows.h>

#ifdef _MSC_VER

#include <dbghelp.h>
#pragma comment(lib, "Kernel32.lib")
#pragma comment(lib, "DbgHelp.lib")

#endif // #ifdef _MSC_VER

// #ifdef _WIN32
#elif defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)
#include <unistd.h>
#include <unwind.h>
#include "backtrace.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif // #if defined(__APPLE__)

#endif // #if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)


// Set maximum number of backtrace frames to print.
#define BACKTRACE_LIMIT 15

// Forward declaration
static char *demangle(const char *func_name);

#if defined(MOONBIT_ALLOW_STACKTRACE) && !defined(__TINYC__)

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

  char *demangled_name = function ? demangle(function) : NULL;
  const char *func_name = demangled_name ? demangled_name : (function ? function : "???");
  const char *file_name = filename ? filename : "???";

  fprintf(stderr, "    at %s (%s:%d)\n", func_name, file_name, lineno);

  bt_data->count++;
  if (demangled_name)
    free(demangled_name);
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
    char *demangled_name = demangle(symbol->Name);
    const char *func_name = demangled_name ? demangled_name : symbol->Name;

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
    const char* path_ptr = NULL;

#if defined(__APPLE__)
    uint32_t size = sizeof(exec_path);
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

static void mbt_str_init(mbt_str *s) {
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
}

/* Demangling is best-effort: omit an append if its allocation fails. */
static void mbt_str_append_char(mbt_str *s, char c) {
  if (!mbt_str_reserve(s, 1))
    return;
  s->buf[s->len++] = c;
  s->buf[s->len] = 0;
}

static void mbt_str_append_cstr(mbt_str *s, const char *cstr) {
  if (!cstr)
    return;
  size_t n = strlen(cstr);
  if (!mbt_str_reserve(s, n))
    return;
  memcpy(s->buf + s->len, cstr, n);
  s->len += n;
  s->buf[s->len] = 0;
}

static void mbt_str_append_n(mbt_str *s, const char *cstr, size_t n) {
  if (!cstr || n == 0)
    return;
  if (!mbt_str_reserve(s, n))
    return;
  memcpy(s->buf + s->len, cstr, n);
  s->len += n;
  s->buf[s->len] = 0;
}

static char *mbt_str_release(mbt_str *s) {
  char *out = s->buf;
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
  return out;
}

static void mbt_str_destroy(mbt_str *s) {
  if (s->buf)
    free(s->buf);
  s->buf = NULL;
  s->len = 0;
  s->cap = 0;
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

static int mbt_skip_decimal(const char **p) {
  if (**p < '0' || **p > '9')
    return 0;
  while (**p >= '0' && **p <= '9')
    (*p)++;
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

static int mbt_parse_identifier(const char **p, mbt_str *out) {
  uint32_t n = 0;
  if (!mbt_parse_u32(p, &n))
    return 0;
  const char *start = *p;
  size_t remaining = strlen(start);
  if (remaining < (size_t)n)
    return 0;
  *p = start + n;

  for (uint32_t i = 0; i < n; i++) {
    char c = start[i];
    if (c != '_') {
      mbt_str_append_char(out, c);
      continue;
    }
    if (i + 1 >= n)
      return 0;
    char next = start[i + 1];
    if (next == '_') {
      mbt_str_append_char(out, '_');
      i += 1;
      continue;
    }
    if (i + 2 >= n)
      return 0;
    int hi = mbt_hex_value(start[i + 1]);
    int lo = mbt_hex_value(start[i + 2]);
    if (hi < 0 || lo < 0)
      return 0;
    char v = (char)((hi << 4) | lo);
    mbt_str_append_char(out, v);
    i += 2;
  }
  return 1;
}

static int mbt_parse_package_segments(const char **p, uint32_t count,
                                      mbt_str *out) {
  for (uint32_t i = 0; i < count; i++) {
    if (i > 0)
      mbt_str_append_char(out, '/');
    if (!mbt_parse_identifier(p, out))
      return 0;
  }
  return 1;
}

static int mbt_parse_package_count(const char **p, uint32_t *out) {
  /* In v0 the count is not delimited from the first segment length. Keep the
     established one-digit interpretation until the format is made unambiguous. */
  if (**p < '0' || **p > '9')
    return 0;
  *out = (uint32_t)(**p - '0');
  (*p)++;
  return 1;
}

static int mbt_parse_package(const char **p, mbt_str *out) {
  if (**p != 'P')
    return 0;
  (*p)++;

  if (**p == 'B') {
    (*p)++;
    mbt_str_append_cstr(out, "moonbitlang/core/builtin");
    return 1;
  }

  if (**p == 'C') {
    (*p)++;
    uint32_t count = 0;
    if (!mbt_parse_package_count(p, &count))
      return 0;
    mbt_str_append_cstr(out, "moonbitlang/core");
    if (count > 0)
      mbt_str_append_char(out, '/');
    return mbt_parse_package_segments(p, count, out);
  }

  uint32_t count = 0;
  if (!mbt_parse_package_count(p, &count))
    return 0;
  return mbt_parse_package_segments(p, count, out);
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
  int result = 0;
  mbt_str pkg = {NULL, 0, 0};
  mbt_str_init(&pkg);
  if (!mbt_parse_package(p, &pkg))
    goto cleanup;
  if (pkg.len > 0 && !(omit_core_prefix && mbt_pkg_is_core(pkg.buf))) {
    mbt_str_append_char(out, '@');
    mbt_str_append_cstr(out, pkg.buf);
    mbt_str_append_char(out, '.');
  }
  if (!mbt_parse_identifier(p, out))
    goto cleanup;
  if (**p == 'L') {
    (*p)++;
    mbt_str_append_char(out, '.');
    if (!mbt_parse_identifier(p, out))
      goto cleanup;
    if (**p != 'S')
      goto cleanup;
    (*p)++;
    if (!mbt_skip_decimal(p))
      goto cleanup;
  }
  result = 1;

cleanup:
  mbt_str_destroy(&pkg);
  return result;
}

static int mbt_parse_type_arg(const char **p, mbt_str *out);

static int mbt_parse_type_args(const char **p, mbt_str *out) {
  if (**p == 'H') {
    (*p)++;
    mbt_str_append_cstr(out, " raise ");
    return mbt_parse_type_arg(p, out);
  }
  if (**p != 'G')
    return 1;
  (*p)++;
  mbt_str_append_char(out, '[');
  int first = 1;
  while (**p && **p != 'E') {
    if (!first) {
      mbt_str_append_cstr(out, ", ");
    }
    if (!mbt_parse_type_arg(p, out))
      return 0;
    first = 0;
  }
  if (**p != 'E')
    return 0;
  (*p)++;
  mbt_str_append_char(out, ']');
  if (**p == 'H') {
    (*p)++;
    mbt_str_append_cstr(out, " raise ");
    if (!mbt_parse_type_arg(p, out))
      return 0;
  }
  return 1;
}

static int mbt_parse_fn_type(const char **p, mbt_str *out) {
  int has_async = 0, nocancel = 0, is_raw_func = 0;
  if (**p == 'V') {
    ++(*p);
    has_async = 1;
    if (**p == 'N') {
      ++(*p);
      nocancel = 1;
    }
  }

  if (**p == 'X') {
    ++(*p);
    is_raw_func = 1;
  }

  if (**p != 'W')
    return 0;
  (*p)++;

  if (is_raw_func)
    mbt_str_append_cstr(out, "FuncRef[");

  if (has_async)
    mbt_str_append_cstr(out, "async ");

  mbt_str_append_char(out, '(');
  int first = 1;
  while (**p && **p != 'E') {
    if (!first) {
      mbt_str_append_cstr(out, ", ");
    }
    if (!mbt_parse_type_arg(p, out))
      return 0;
    first = 0;
  }
  if (**p != 'E')
    return 0;
  (*p)++;
  mbt_str_append_cstr(out, ") -> ");
  if (!mbt_parse_type_arg(p, out))
    return 0;
  if (**p == 'Q') {
    (*p)++;
    mbt_str_append_cstr(out, " raise ");
    if (!mbt_parse_type_arg(p, out))
      return 0;
    if (nocancel)
      mbt_str_append_cstr(out, " + nocancel");
  } else if (nocancel) {
    mbt_str_append_cstr(out, " nocancel");
  }
  if (is_raw_func)
    mbt_str_append_char(out, ']');
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
    case 'i':
      (*p)++;
      mbt_str_append_cstr(out, "Int");
      return 1;
    case 'l':
      (*p)++;
      mbt_str_append_cstr(out, "Int64");
      return 1;
    case 'h':
      (*p)++;
      mbt_str_append_cstr(out, "Int16");
      return 1;
    case 'j':
      (*p)++;
      mbt_str_append_cstr(out, "UInt");
      return 1;
    case 'k':
      (*p)++;
      mbt_str_append_cstr(out, "UInt16");
      return 1;
    case 'm':
      (*p)++;
      mbt_str_append_cstr(out, "UInt64");
      return 1;
    case 'd':
      (*p)++;
      mbt_str_append_cstr(out, "Double");
      return 1;
    case 'f':
      (*p)++;
      mbt_str_append_cstr(out, "Float");
      return 1;
    case 'b':
      (*p)++;
      mbt_str_append_cstr(out, "Bool");
      return 1;
    case 'c':
      (*p)++;
      mbt_str_append_cstr(out, "Char");
      return 1;
    case 's':
      (*p)++;
      mbt_str_append_cstr(out, "String");
      return 1;
    case 'u':
      (*p)++;
      mbt_str_append_cstr(out, "Unit");
      return 1;
    case 'y':
      (*p)++;
      mbt_str_append_cstr(out, "Byte");
      return 1;
    case 'z':
      (*p)++;
      mbt_str_append_cstr(out, "Bytes");
      return 1;
    case 'v':
      (*p)++;
      mbt_str_append_cstr(out, "V128");
      return 1;
    case 'A': {
      (*p)++;
      mbt_str_append_cstr(out, "FixedArray[");
      if (!mbt_parse_type_arg(p, out))
        return 0;
      mbt_str_append_char(out, ']');
      return 1;
    }
    case 'N': {
      (*p)++;
      mbt_str_append_cstr(out, "ReadOnlyArray[");
      if (!mbt_parse_type_arg(p, out))
        return 0;
      mbt_str_append_char(out, ']');
      return 1;
    }
    case 'O': {
      (*p)++;
      mbt_str_append_cstr(out, "Option[");
      if (!mbt_parse_type_arg(p, out))
        return 0;
      mbt_str_append_char(out, ']');
      return 1;
    }
    case 'U': {
      (*p)++;
      mbt_str_append_char(out, '(');
      int first = 1;
      while (**p && **p != 'E') {
        if (!first)
          mbt_str_append_cstr(out, ", ");
        if (!mbt_parse_type_arg(p, out))
          return 0;
        first = 0;
      }
      if (**p != 'E')
        return 0;
      (*p)++;
      mbt_str_append_char(out, ')');
      return 1;
    }
    case 'V':
    case 'X':
    case 'W':
      return mbt_parse_fn_type(p, out);
    case 'R':
      return mbt_parse_type_ref(p, out);
    default:
      return 0;
  }
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
    mbt_str_append_char(out, '.');
    if (!mbt_parse_identifier(p, out))
      return 0;
    if (**p != 'S') {
      return 0;
    }
    (*p)++;
    if (!mbt_skip_decimal(p))
      return 0;
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
    mbt_str_append_cstr(out, ".anon(l");
    mbt_str_append_n(out, line, line_len);
    mbt_str_append_char(out, ')');
    return 1;
  }

  (void)uuid;
  (void)uuid_len;
  mbt_str_append_cstr(out, ".anon");
  return 1;
}

static int mbt_parse_fn_suffixes(const char **p, mbt_str *out) {
  if (!mbt_parse_lifted_suffixes(p, out))
    return 0;
  if (!mbt_parse_closure_suffix(p, out))
    return 0;
  return 1;
}

static int demangle_tag_F(const char **p, mbt_str *out) {
  int result = 0;
  mbt_str pkg = {NULL, 0, 0};
  mbt_str_init(&pkg);
  if (!mbt_parse_package(p, &pkg))
    goto cleanup;
  if (pkg.len > 0) {
    mbt_str_append_char(out, '@');
    mbt_str_append_cstr(out, pkg.buf);
    mbt_str_append_char(out, '.');
  }
  if (!mbt_parse_identifier(p, out))
    goto cleanup;
  if (!mbt_parse_fn_suffixes(p, out))
    goto cleanup;
  if (**p == 'G' || **p == 'H') {
    if (!mbt_parse_type_args(p, out))
      goto cleanup;
  }
  if (!mbt_parse_fn_suffixes(p, out))
    goto cleanup;
  result = 1;

cleanup:
  mbt_str_destroy(&pkg);
  return result;
}

static int demangle_tag_M(const char **p, mbt_str *out) {
  if (!mbt_append_type_path(p, out, 0))
    return 0;
  mbt_str_append_cstr(out, "::");
  if (!mbt_parse_identifier(p, out))
    return 0;
  if (!mbt_parse_fn_suffixes(p, out))
    return 0;
  if (**p == 'G' || **p == 'H') {
    if (!mbt_parse_type_args(p, out))
      return 0;
  }
  return mbt_parse_fn_suffixes(p, out);
}

static int demangle_tag_I(const char **p, mbt_str *out) {
  int result = 0;
  mbt_str impl_type = {NULL, 0, 0};
  mbt_str trait_type = {NULL, 0, 0};
  mbt_str type_args = {NULL, 0, 0};
  mbt_str method = {NULL, 0, 0};

  mbt_str_init(&impl_type);
  mbt_str_init(&trait_type);
  mbt_str_init(&type_args);
  mbt_str_init(&method);

  if (!mbt_append_type_path(p, &impl_type, 0) ||
      !mbt_append_type_path(p, &trait_type, 0))
    goto cleanup;

  if (!mbt_parse_identifier(p, &method))
    goto cleanup;
  if (!mbt_parse_fn_suffixes(p, &method))
    goto cleanup;
  if (**p == 'G' || **p == 'H') {
    if (!mbt_parse_type_args(p, &type_args))
      goto cleanup;
  }
  if (!mbt_parse_fn_suffixes(p, &method))
    goto cleanup;

  mbt_str_append_cstr(out, "impl ");
  mbt_str_append_cstr(out, trait_type.buf);
  mbt_str_append_cstr(out, " for ");
  mbt_str_append_cstr(out, impl_type.buf);
  if (type_args.len)
    mbt_str_append_cstr(out, type_args.buf);
  mbt_str_append_cstr(out, " with ");
  mbt_str_append_cstr(out, method.buf);

  result = 1;

cleanup:
  mbt_str_destroy(&impl_type);
  mbt_str_destroy(&trait_type);
  mbt_str_destroy(&type_args);
  mbt_str_destroy(&method);
  return result;
}

static int demangle_tag_E(const char **p, mbt_str *out) {
  int result = 0;
  mbt_str type = {NULL, 0, 0};
  mbt_str method_pkg = {NULL, 0, 0};
  const char *type_use = NULL;
  mbt_str_init(&type);
  mbt_str_init(&method_pkg);

  if (!mbt_append_type_path(p, &type, 1) ||
      !mbt_parse_package(p, &method_pkg))
    goto cleanup;
  if (method_pkg.len > 0) {
    mbt_str_append_char(out, '@');
    mbt_str_append_cstr(out, method_pkg.buf);
    mbt_str_append_char(out, '.');
  }
  type_use = type.buf;
  if (method_pkg.len > 0 && type_use && type_use[0] == '@')
    type_use++;
  mbt_str_append_cstr(out, type_use);
  mbt_str_append_cstr(out, "::");
  if (!mbt_parse_identifier(p, out))
    goto cleanup;
  if (!mbt_parse_fn_suffixes(p, out))
    goto cleanup;
  if (**p == 'G' || **p == 'H') {
    if (!mbt_parse_type_args(p, out))
      goto cleanup;
  }
  if (!mbt_parse_fn_suffixes(p, out))
    goto cleanup;
  result = 1;

cleanup:
  mbt_str_destroy(&type);
  mbt_str_destroy(&method_pkg);
  return result;
}

static int demangle_tag_L(const char **p, mbt_str *out) {
  int result = 0;
  mbt_str ident = {NULL, 0, 0};
  const char *use = NULL;
  mbt_str_init(&ident);
  if (**p == 'm')
    (*p)++;
  if (!mbt_parse_identifier(p, &ident) || **p != 'S')
    goto cleanup;
  (*p)++;
  if (!(**p >= '0' && **p <= '9'))
    goto cleanup;
  while (**p >= '0' && **p <= '9')
    (*p)++;
  use = ident.buf;
  if (use && use[0] == '$')
    use++;
  mbt_str_append_char(out, '@');
  mbt_str_append_cstr(out, use);
  result = 1;

cleanup:
  mbt_str_destroy(&ident);
  return result;
}

static char *demangle(const char *func_name) {
  char tag;
  if (!func_name)
    return NULL;
  const char *p = func_name;
  if (p[0] == '$')
    p++;
  if (strlen(p) < 3 || p[0] != '_' || p[1] != 'M' || p[2] != '0')
    return NULL;
  p += 3;

  mbt_str out = {NULL, 0, 0};
  mbt_str_init(&out);

  tag = *p;
  if (!tag)
    goto fail;
  p++;

  switch (tag) {
    case 'F':
      if (!demangle_tag_F(&p, &out))
        goto fail;
      break;
    case 'M':
      if (!demangle_tag_M(&p, &out))
        goto fail;
      break;
    case 'I':
      if (!demangle_tag_I(&p, &out))
        goto fail;
      break;
    case 'E':
      if (!demangle_tag_E(&p, &out))
        goto fail;
      break;
    case 'L':
      if (!demangle_tag_L(&p, &out))
        goto fail;
      break;
    default:
      goto fail;
  }

  if (*p != '\0') {
    char c = *p;
    if (!(c == '.' || c == '$' || c == '@'))
      goto fail;
  }

  return mbt_str_release(&out);

fail:
  mbt_str_destroy(&out);
  return NULL;
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

  char *demangled_name = demangle(func);
  const char *func_name = demangled_name ? demangled_name : func;

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
  if (demangled_name)
    free(demangled_name);

  if ((func && 0 == strcmp(func, "main")) ||
      moonbit__bt_level >= (uint32_t)MOONBIT_NATIVE_BT_MAX_FRAMES)
    return 0;
  return 1;
}

#ifdef __cplusplus
}
#endif
