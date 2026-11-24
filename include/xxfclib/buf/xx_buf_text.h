/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_buf_text.h
 * @brief Text helpers for xx_buf_t: numbers, runs of one byte, C string view.
 *
 * They follow the xx_buf_t contract (sticky failure, the content stays zero
 * terminated) and do not go through a printf formatter, so they are cheap
 * enough for writers that emit millions of numbers.
 *
 * Moved from CPDBRipper (cpdbr_buf_*) into the library in 2026-11.
 */

#ifndef XXFCLIB_XX_BUF_TEXT_H
#define XXFCLIB_XX_BUF_TEXT_H

#include "xxfclib/buf/xx_buf.h"
#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Append @p count copies of the byte @p c. */
XXFC_API bool xx_buf_append_fill(xx_buf_t *buf, char c, size_t count);

/** @brief Append an unsigned value in decimal. */
XXFC_API bool xx_buf_append_u64(xx_buf_t *buf, uint64_t value);

/** @brief Append a signed value in decimal ('-' for a negative one). */
XXFC_API bool xx_buf_append_i64(xx_buf_t *buf, int64_t value);

/**
 * @brief Append a value in hexadecimal, no prefix, no leading zeros
 *        ("0" for 0); @p upper selects A-F over a-f.
 */
XXFC_API bool xx_buf_append_hex(xx_buf_t *buf, uint64_t value, bool upper);

/** @brief The content as a C string: "" when nothing was ever appended. */
XXFC_API const char *xx_buf_cstr(const xx_buf_t *buf);

/**
 * @brief Cut the content to its first @p size bytes (no-op when it is not
 *        longer). The allocation is kept.
 */
XXFC_API void xx_buf_truncate(xx_buf_t *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_BUF_TEXT_H */
