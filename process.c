/*
 * Copyright 2025 International Digital Economy Academy
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

#ifdef _WIN32

#include <windows.h>

typedef DWORD pid_t;

#else

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef int HANDLE;

#ifdef __linux__

#include <linux/version.h>
#include <sys/syscall.h>

#ifndef P_PIDFD
#define P_PIDFD 3
#endif

#endif

#endif

#include <moonbit.h>


// ======== process status query API =========

MOONBIT_EXPORT
int moonbit_rt_get_process_result(HANDLE handle, int32_t pid, int32_t *out) {
#ifdef _WIN32

  // On Windows, `get_process_result` should only be called when the process has exited.
  BOOL ret = GetExitCodeProcess(handle, out);
  return ret ? 0 : - 1;

#else

#ifdef __linux__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)

  if (handle >= 0) {
    siginfo_t info;
    info.si_pid = 0;
    int ret = waitid(P_PIDFD, handle, &info, WEXITED | WNOHANG);

    if (ret < 0)
      return -1;

    if (info.si_pid == 0) {
      errno = EAGAIN;
      return -1;
    }

    if (info.si_code == CLD_EXITED) {
      *out = info.si_status;
    } else {
      // killed by signal
      *out = -info.si_status;
    }
    return 0;
  }

#endif // #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#endif // #ifdef __linux__

  /* This can be either:
     1. Linux systems without `pidfd` support.
     2. MacOS/BSD
  */
  int wstatus;
  int ret = waitpid(pid, &wstatus, WNOHANG);
  if (ret < 0)
    return -1;

  if (ret == 0) {
    errno = EAGAIN;
    return -1;
  }

  if WIFEXITED(wstatus) {
    *out = WEXITSTATUS(wstatus);
  } else {
    *out = -WTERMSIG(wstatus);
  }
  return 0;

#endif // #ifdef _WIN32
}


MOONBIT_EXPORT
HANDLE moonbit_rt_open_pid_handle(int32_t pid) {
#ifdef _WIN32

  return OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

#else

#ifdef __linux__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)

  int pidfd = syscall(SYS_pidfd_open, pid, 0);
  if (pidfd < 0 && (errno == ENOSYS || errno == EPERM))
    // some container environments do not support `pidfd` even on Linux >= 5.3.0
    errno = 0;

  return pidfd;

#endif // #if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)
#endif // #ifdef __linux__

  errno = 0;
  return -1;

#endif // #ifdef _WIN32
}

// ======== process termination API =========

MOONBIT_EXPORT
void moonbit_rt_terminate_process(pid_t pid, int32_t signal) {
#ifdef _WIN32
  GenerateConsoleCtrlEvent(signal, pid);
#else
  kill(pid, signal);
#endif
}

MOONBIT_EXPORT
void moonbit_rt_kill_process(pid_t pid) {
#ifdef _WIN32
  HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
  if (handle == INVALID_HANDLE_VALUE)
    return;

  TerminateProcess(handle, 1);
  CloseHandle(handle);
#else
  kill(pid, SIGKILL);
#endif
}

// ======== child process environment manipulation =========
#ifdef _WIN32

MOONBIT_EXPORT
LPWCH moonbit_rt_get_curr_env() {
  return GetEnvironmentStringsW();
}

// return the size of the env block, in number of `WCHAR`,
// excluding the final trailing NUL
MOONBIT_EXPORT
int32_t moonbit_rt_env_block_length(LPWCH env_block) {
  if (!env_block)
    return 0;

  LPWCH cursor = env_block;
  int len = 0;
  while (*cursor) {
    int block_len = 0;
    while (cursor[block_len])
      ++block_len;

    // skip pseudo entries
    if (*cursor != L'=')
      len += block_len + 1;

    cursor += block_len + 1;
  }
  return len;
}

MOONBIT_EXPORT
LPWCH moonbit_rt_allocate_env_block(int32_t size) {
  /* In theory, an empty environment block should be represented as L"\0"
     (i.e. single NUL terminator for block termination).
     However, according to https://nullprogram.com/blog/2023/08/23/,
     `CreateProcessW` requires the second character being readable in this case
     (the content of that character does not matter).
     So always allocate at least two characters here. */
  size = size == 0 ? 1 : size;
  LPWCH env_block = (LPWCH)malloc((size + 1) * sizeof(WCHAR));
  return env_block;
}

struct EnvEntry {
  int32_t len;
  int32_t key_len;
};

static inline
struct EnvEntry resolve_env_entry(LPWCH block) {
  struct EnvEntry result = { 0, -1 };
  WCHAR c;
  while (c = block[result.len]) {
    if (result.key_len < 0 && c == L'=')
      result.key_len = result.len;
    ++result.len;
  }
  return result;
}

MOONBIT_EXPORT
void moonbit_rt_write_env_block(LPWCH dst, LPWCH env_block, int32_t base_offset) {
  if (!env_block) {
    dst[base_offset] = 0;
    return;
  }

  LPWCH cursor = env_block;
  int dst_offset = base_offset;
  while (*cursor) {
    struct EnvEntry entry = resolve_env_entry(cursor);

    // skip pseudo entries
    if (entry.key_len <= 0)
      goto skip_entry;

    // check if an duplicated entry already exists
    for (LPWCH cursor2 = dst; cursor2 - dst < base_offset;) {
      struct EnvEntry existing = resolve_env_entry(cursor2);
      if (
        entry.key_len == existing.key_len
        && CompareStringOrdinal(cursor2, existing.key_len, cursor, entry.key_len, TRUE)
           == CSTR_EQUAL
      ) {
        goto skip_entry;
      }

      cursor2 += existing.len + 1;
    }

    memcpy(dst + dst_offset, cursor, (entry.len + 1) * sizeof(WCHAR));
    dst_offset += entry.len + 1;

  skip_entry:
    cursor += entry.len + 1;
  }
  dst[dst_offset] = 0;
  FreeEnvironmentStringsW(env_block);
}

MOONBIT_EXPORT
int32_t moonbit_rt_env_block_add_entry(
  LPWCH env_block,
  int32_t offset,
  moonbit_string_t key,
  int32_t key_len,
  moonbit_string_t value,
  int32_t value_len
) {
  memcpy(env_block + offset, key, key_len * sizeof(WCHAR));
  env_block[offset + key_len] = L'=';
  memcpy(env_block + offset + key_len + 1, value, value_len * sizeof(WCHAR));
  env_block[offset + key_len + 1 + value_len] = 0;

  // handle `NUL` in key or value to prevent injection
  int32_t entry_end = offset;
  while (env_block[entry_end])
    ++entry_end;
  // In case this is the last entry we ever written,
  // write the final NUL terminator.
  // If subsequent entry exists, they will overwrite the NUL here.
  env_block[entry_end + 1] = 0;
  return entry_end - offset < key_len ? offset : entry_end + 1;
}

#else // #ifdef _WIN32 for child process env block manipulation

extern char **environ;

MOONBIT_EXPORT
char **moonbit_rt_get_curr_env() {
  return environ;
}

MOONBIT_EXPORT
int32_t moonbit_rt_env_block_length(char **env_block) {
  for (char **cursor = env_block;; ++cursor) {
    if (*cursor == 0)
      return cursor - env_block;
  }
}

MOONBIT_EXPORT
char **moonbit_rt_allocate_env_block(int32_t size) {
  char **env_block = (char**)malloc((size + 1) * sizeof(char*));
  env_block[size] = 0;
  return env_block;
}

int32_t moonbit_utf8_len_from_utf16(
  moonbit_string_t src,
  int32_t src_offset,
  int32_t src_length
);

int32_t moonbit_utf8_encode_from_utf16(
  moonbit_string_t src,
  int32_t src_offset,
  int32_t src_length,
  moonbit_bytes_t dst, 
  int32_t dst_offset
);

MOONBIT_EXPORT
int32_t moonbit_rt_env_block_add_entry(
  char **env_block,
  int32_t index,
  moonbit_string_t key,
  int32_t key_len,
  moonbit_string_t value,
  int32_t value_len
) {
  for (int i = 0; i < key_len; ++i)
    if (key[i] == 0)
      return index;

  int key_bytes = moonbit_utf8_len_from_utf16(key, 0, key_len);
  int value_bytes = moonbit_utf8_len_from_utf16(value, 0, value_len);
  // `2`: `=` + trailing NUL
  unsigned char *entry = (unsigned char*)malloc(key_bytes + value_bytes + 2);
  moonbit_utf8_encode_from_utf16(key, 0, key_len, entry, 0);
  entry[key_bytes] = '=';
  moonbit_utf8_encode_from_utf16(value, 0, value_len, entry, key_bytes + 1);
  entry[key_bytes + value_bytes + 1] = 0;
  env_block[index] = (char*)entry; 
  return index + 1;
}

MOONBIT_EXPORT
void moonbit_rt_write_env_block(char **dst, char **env_block, int32_t base_offset) {
  for (int i = 0, offset = base_offset;; ++i) {
    char const *entry = env_block[i];
    if (entry == 0) {
      dst[offset] = 0;
      return;
    }

    int len = strlen(entry);
    char const *key_end = strchr(entry, '=');
    if (key_end) {
      for (int j = 0; j < base_offset; ++j) {
        if (strncmp(dst[j], entry, key_end + 1 - entry) == 0)
          goto skip_duplicated_entry;
      }
    } else {
      for (int j = 0; j < base_offset; ++j)
        if (strcmp(dst[j], entry) == 0)
          goto skip_duplicated_entry;
    }

    char *copied = malloc(len + 1);
    memcpy(copied, entry, len + 1);
    dst[offset++] = copied;
  skip_duplicated_entry:
    ;
  }
}

#endif // #ifndef _WIN32 for child process envrinoment manipulation

// ======== Unix argv handling API =========

#ifndef _WIN32
MOONBIT_EXPORT
char **moonbit_rt_make_argv_array(int32_t len) {
  char **argv = malloc((len + 1) * sizeof(char*));
  argv[len] = 0;
  return argv;
}

MOONBIT_EXPORT
void moonbit_rt_argv_array_add_encoded_entry(
  char **argv,
  int32_t offset,
  moonbit_bytes_t arg,
  int32_t arg_len
) {
  char *entry = malloc(arg_len + 1);
  memcpy(entry, arg, arg_len + 1); 
  argv[offset] = entry;
}

MOONBIT_EXPORT
void moonbit_rt_argv_array_add_entry(
  char **argv,
  int32_t offset,
  moonbit_string_t arg,
  int32_t arg_len
) {
  int len = moonbit_utf8_len_from_utf16(arg, 0, arg_len);
  char *entry = malloc(len + 1);
  moonbit_utf8_encode_from_utf16(arg, 0, arg_len, (moonbit_bytes_t)entry, 0); 
  entry[len] = 0;
  argv[offset] = entry;
}
#endif
