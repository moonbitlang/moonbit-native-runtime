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
#include "moonbit_runtime.h"

#ifdef MOONBIT_NATIVE_NO_SYS_HEADER

typedef struct FILE FILE;
extern FILE *stdout;
extern FILE *stderr;
size_t fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream);
void *memcpy(void *dst, const void *src, size_t n);
size_t strlen(const char *s);

// #ifdef MOONBIT_NATIVE_NO_SYS_HEADER
#elif defined(_WIN32)

#include <windows.h>
#include <stdio.h>

#ifdef _MSC_VER

#include <stdlib.h>
#include <shellapi.h>
#include <bcrypt.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Bcrypt.lib")

#else

#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#endif

#else // #ifdef _WIN32

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/random.h>
#include <sys/time.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#endif // #ifndef _WIN32

#ifdef _WIN32

typedef moonbit_string_t moonbit_os_string_t;
#define moonbit_empty_os_string moonbit_empty_int16_array

#else

typedef moonbit_bytes_t moonbit_os_string_t;
#define moonbit_empty_os_string moonbit_empty_int8_array

#endif


#ifdef _WIN32
static
void write_console(HANDLE console, moonbit_string_t str) {
  static const DWORD max_chunk_size = 1 << 14; // 16K
  DWORD len = Moonbit_array_length(str);
  DWORD written = 0, total_written = 0;
  while (total_written < len) {
    DWORD chars_to_write = len - total_written;
    if (chars_to_write > max_chunk_size)
      chars_to_write = max_chunk_size;

    BOOL ret = WriteConsoleW(
      console,
      ((WCHAR*)str) + total_written,
      chars_to_write,
      &written,
      NULL
    );

    if (!ret) return;
    total_written += written;
  }
  WriteConsoleW(console, L"\n", 1, NULL, NULL);
}
#endif


// defined in `utf.c`
int32_t moonbit_utf8_encode_from_utf16(
  moonbit_string_t src,
  int32_t src_offset,
  int32_t src_length,
  moonbit_bytes_t dst,
  int32_t dst_offset
);

static
void write_stdio(FILE *stream, moonbit_string_t str) {
  // use a sliding window to perform batch-write
  unsigned char window[1024];

  // a UTF-16 code unit can map to up to 3 UTF-8 bytes
  static const int32_t max_chunk_size = (sizeof(window) - 1) / 3;

  int32_t const len = Moonbit_array_length(str);
  int32_t src_offset = 0;
  for (;;) {
    int32_t const remaining = len - src_offset;
    int32_t consumed = remaining < max_chunk_size ? remaining : max_chunk_size;

    if (consumed > 1) {
      uint16_t const last_char = str[src_offset + consumed - 1];
      if (0xD800 <= last_char && last_char < 0xDC00)
        consumed -= 1;
    }

    int32_t produced = moonbit_utf8_encode_from_utf16(str, src_offset, consumed, window, 0);
    if (produced < 0)
      return;

    src_offset += consumed;
    if (src_offset == len) {
      window[produced] = '\n';
      fwrite(window, 1, produced + 1, stream);
      break;
    } else {
      fwrite(window, 1, produced, stream);
    }
  }
}

MOONBIT_EXPORT void moonbit_println(moonbit_string_t str) {
#ifdef _WIN32
  static HANDLE stdout_handle = INVALID_HANDLE_VALUE;
  static BOOL stdout_is_console = 0;

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
    write_console(stdout_handle, str);
    return;
  }

  // For redirected stdout, output UTF-8
#endif

  write_stdio(stdout, str);
}

MOONBIT_EXPORT void moonbit_eprintln(moonbit_string_t str) {
#ifdef _WIN32
  static HANDLE stderr_handle = INVALID_HANDLE_VALUE;
  static BOOL stderr_is_console = 0;

  if (stderr_handle == INVALID_HANDLE_VALUE) {
    stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

    if (stderr_handle == INVALID_HANDLE_VALUE) {
      // There is no stdout. Simply ignore the message
      return;
    }

    DWORD mode;
    stderr_is_console = GetConsoleMode(stderr_handle, &mode);
  }

  if (stderr_is_console) {
    // When stdout is a real console,
    // use `WriteConsoleW` to output with current code page of the console.
    write_console(stderr_handle, str);
    return;
  }

  // For redirected stdout, output UTF-8
#endif

  write_stdio(stderr, str);
}

static int __moonbit_internal_argc = 0;
static char **__moonbit_internal_argv = 0;

MOONBIT_EXPORT void moonbit_runtime_init(int argc, char **argv) {
  __moonbit_internal_argc = argc;
  __moonbit_internal_argv = argv;
  moonbit_allocator_init();
}

// ========= random seed API ==========

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

// ========= command line argument API ==========

MOONBIT_FFI_EXPORT
moonbit_os_string_t *moonbit_rt_get_cli_args(void) {
  if (__moonbit_internal_argc == 0 && __moonbit_internal_argv == 0)
    return (moonbit_os_string_t*)moonbit_empty_ref_array;

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

// ========= errno manipulation API ==========

MOONBIT_EXPORT
int moonbit_rt_get_errno() {
#ifdef _WIN32
  return GetLastError();
#else
  return errno;
#endif
}

MOONBIT_EXPORT
char *moonbit_rt_errno_to_string(int err) {
#ifdef _WIN32
  LPWSTR result = 0;
  if (
    !FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER
      | FORMAT_MESSAGE_FROM_SYSTEM
      | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      err,
      0x409, // language ID for en-US
      (LPWSTR)&result,
      1,
      NULL
    )
  ) {
    return moonbit_rt_errno_to_string(GetLastError());
  }
  return (char*)result;
#else
  return strerror(err);
#endif
}

MOONBIT_EXPORT
void moonbit_rt_free_errno_str(void *ptr) {
#ifdef _WIN32
  LocalFree(ptr);
#endif
}

// ========= current executable & directory retrieval ==========

#ifdef _WIN32

static moonbit_os_string_t moonbit_current_exe_error(DWORD error) {
  SetLastError(error);
  return moonbit_empty_os_string;
}

#else

static moonbit_os_string_t moonbit_current_exe_error(int error) {
  errno = error;
  return moonbit_empty_os_string;
}

#endif

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

// Return the executable path in the platform's native string representation.
// An empty string indicates failure; errno or GetLastError() provides the cause.
MOONBIT_FFI_EXPORT
moonbit_os_string_t moonbit_rt_get_current_exe(void) {
#ifdef _WIN32

  DWORD capacity = 256;
  WCHAR *buf = NULL;

  for (;;) {
    WCHAR *new_buf = (WCHAR *)realloc(buf, capacity * sizeof(WCHAR));
    if (!new_buf) {
      free(buf);
      return moonbit_current_exe_error(ERROR_NOT_ENOUGH_MEMORY);
    }
    buf = new_buf;

    DWORD len = GetModuleFileNameW(NULL, buf, capacity);
    if (len == 0) {
      DWORD error = GetLastError();
      free(buf);
      return moonbit_current_exe_error(error != ERROR_SUCCESS
                                           ? error
                                           : ERROR_GEN_FAILURE);
    }

    if (len == capacity) {
      if (capacity > (DWORD)(INT32_MAX / 2)) {
        free(buf);
        return moonbit_current_exe_error(ERROR_FILENAME_EXCED_RANGE);
      }
      capacity *= 2;
      continue;
    }

    moonbit_os_string_t result = moonbit_make_string_raw((int32_t)len);
    memcpy(result, buf, len * sizeof(WCHAR));
    free(buf);
    return result;
  }

#elif defined(__APPLE__)

  char stack_buf[256];
  char *buf = stack_buf;
  uint32_t size = sizeof(stack_buf);

  if (_NSGetExecutablePath(buf, &size) != 0) {
    // dyld reports the required size, including the terminating NUL, so only
    // one retry is needed.
    if (size > INT32_MAX)
      return moonbit_current_exe_error(ENAMETOOLONG);

    buf = (char *)malloc(size);
    if (!buf)
      return moonbit_current_exe_error(ENOMEM);

    if (_NSGetExecutablePath(buf, &size) != 0) {
      free(buf);
      return moonbit_current_exe_error(EIO);
    }
  }

  // Preserve dyld's path spelling; it may contain symlinks or ".." segments.
  size_t len = strlen(buf);
  moonbit_os_string_t result = moonbit_make_bytes_raw((int32_t)len);
  memcpy(result, buf, len);
  if (buf != stack_buf)
    free(buf);
  return result;

#elif defined(__linux__)

  size_t capacity = 256;
  char *buf = NULL;

  // readlink neither appends a NUL nor reports the required size. Filling the
  // buffer is the only truncation signal, so retry with a larger buffer.
  for (;;) {
    char *new_buf = (char *)realloc(buf, capacity);
    if (!new_buf) {
      int error = errno;
      free(buf);
      return moonbit_current_exe_error(error != 0 ? error : ENOMEM);
    }
    buf = new_buf;

    ssize_t len = readlink("/proc/self/exe", buf, capacity);
    if (len < 0) {
      int error = errno;
      free(buf);
      return moonbit_current_exe_error(error != 0 ? error : EIO);
    }
    if ((size_t)len < capacity) {
      // Preserve /proc/self/exe verbatim, including the kernel's " (deleted)"
      // suffix when the running executable has been unlinked.
      moonbit_os_string_t result = moonbit_make_bytes_raw((int32_t)len);
      memcpy(result, buf, (size_t)len);
      free(buf);
      return result;
    }
    if (capacity > (size_t)(INT32_MAX / 2)) {
      free(buf);
      return moonbit_current_exe_error(ENAMETOOLONG);
    }
    capacity *= 2;
  }

#else

  return moonbit_current_exe_error(ENOTSUP);

#endif
}

MOONBIT_FFI_EXPORT
moonbit_os_string_t moonbit_rt_get_current_exe_tcc(void) {
  // In-memory execution has no executable artifact distinct from the host tcc
  // process, so current_exe is unavailable by definition.
#ifdef _WIN32
  return moonbit_current_exe_error(ERROR_NOT_SUPPORTED);
#else
  return moonbit_current_exe_error(ENOTSUP);
#endif
}


// ========= current process PID ==========

MOONBIT_EXPORT
uint32_t moonbit_rt_getpid() {
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

// ========= environment variable manipulation ==========

MOONBIT_EXPORT
moonbit_os_string_t moonbit_rt_get_env_var(moonbit_os_string_t key, int32_t *exists) {
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
moonbit_os_string_t *moonbit_rt_get_env_vars(void) {
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
 
  int count = 0;
  LPWCH start_of_block = env_str;
  while (*start_of_block) {
    if (*start_of_block != L'=')
      ++count;

    do {
      ++start_of_block;
    } while (*start_of_block);
    ++start_of_block;
  }

  if (count == 0) {
    FreeEnvironmentStringsW(env_str);
    return (moonbit_os_string_t*)moonbit_empty_ref_array;
  }

  moonbit_os_string_t *result = (moonbit_os_string_t*)moonbit_make_ref_array_raw(count * 2);

  start_of_block = env_str;
  for (int block_index = 0; block_index < count;) {
    int key_len = -1, block_len = 0;
    while (start_of_block[block_len] != 0) {
      if (start_of_block[block_len] == L'=' && key_len < 0)
        key_len = block_len;

      ++block_len;
    }

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
    } else if (key_len < 0) {
      // this should not happen, but if there is no `=` in the env block,
      // treat the whole block as key and the value as empty
      moonbit_os_string_t key = moonbit_make_string_raw(block_len);
      memcpy(key, start_of_block, block_len * sizeof(WCHAR));

      result[block_index * 2] = key;
      result[block_index * 2 + 1] = moonbit_empty_os_string;

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
void moonbit_rt_set_env_var(moonbit_os_string_t key, moonbit_os_string_t value) {
#ifdef _WIN32
  SetEnvironmentVariableW((LPCWSTR)key, (LPCWSTR)value);
#else
  setenv((const char *)key, (const char *)value, 1);
#endif
}

MOONBIT_EXPORT
void moonbit_rt_unset_env_var(moonbit_os_string_t key) {
#ifdef _WIN32
  SetEnvironmentVariableW((LPCWSTR)key, NULL);
#else
  unsetenv((const char *)key);
#endif
}

// ========= basic timer API ==========

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
