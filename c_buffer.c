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

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <moonbit.h>

MOONBIT_EXPORT
void moonbit_rt_blit_to_c(
  char *dst,
  int32_t dst_offset,
  char *src,
  int32_t src_offset,
  int32_t len
) {
  memcpy(dst + dst_offset, src + src_offset, len);
}

MOONBIT_EXPORT
void moonbit_rt_blit_from_c(
  char *src,
  int32_t src_offset,
  char *dst,
  int32_t dst_offset,
  int32_t len
) {
  memcpy(dst + dst_offset, src + src_offset, len);
}

MOONBIT_EXPORT
int moonbit_rt_c_buffer_get(uint8_t *buf, int index) {
  return buf[index];
}

MOONBIT_EXPORT
int32_t moonbit_rt_strlen(char *str, int32_t offset) {
  return strlen(str + offset);
}

MOONBIT_EXPORT
int32_t moonbit_rt_pointer_is_null(void *ptr) {
  return ptr == 0;
}

MOONBIT_EXPORT
void *moonbit_rt_make_c_buffer(int32_t size) {
  return malloc(size);
}

#ifdef _WIN32
MOONBIT_EXPORT
moonbit_string_t moonbit_rt_c_buffer_as_string(char *str, int32_t offset, int32_t len) {
  size_t bytes = len == -1 ? wcslen((wchar_t*)(str + offset)) * sizeof(wchar_t) : len;
  moonbit_string_t result = moonbit_make_string(bytes / 2, 0);
  memcpy(result, str + offset, bytes);
  return result;
}
#endif
