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
#include "moonbit_simd.h"

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

#ifdef __cplusplus
}
#endif
