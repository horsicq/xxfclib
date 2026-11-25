/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_TRANSLATION_XX_TRANSLATION_H
#define XXFCLIB_TRANSLATION_XX_TRANSLATION_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_translation xx_translation;
typedef enum xx_translation_status {
    XX_TRANSLATION_OK = 0,
    XX_TRANSLATION_INVALID_ARGUMENT,
    XX_TRANSLATION_OUT_OF_MEMORY,
    XX_TRANSLATION_INVALID_PO,
    XX_TRANSLATION_UNSUPPORTED_CHARSET,
    XX_TRANSLATION_IO_ERROR
} xx_translation_status;

/* Parse an owned, immutable UTF-8 GNU PO catalog. Input is copied, need not be
 * NUL terminated, and must not contain NUL bytes. UTF-8 BOM and CRLF are OK.
 * Comments, obsolete/fuzzy entries, contexts, multiline strings, C escapes
 * and Plural-Forms expressions are supported. Non-UTF-8 charsets are rejected.
 * On failure *out is NULL; error_line (optional) receives the 1-based line.
 * Catalogs support up to 16 plural forms and expressions of 256 nodes/64 levels.
 * Read-only lookups may run concurrently. Destruction requires exclusive access.
 */
XXFC_API xx_translation_status xx_translation_load_po(const void *data, size_t size, xx_translation **out, size_t *error_line);

/* Optional I/O adapter; the UTF-8 path is opened through xxfclib generic I/O. */
XXFC_API xx_translation_status xx_translation_load_po_file(const char *path, xx_translation **out, size_t *error_line);
XXFC_API void xx_translation_destroy(xx_translation *catalog);

/* Returned text is borrowed until destroy. Missing, fuzzy and empty
 * translations return the caller's source string. NULL catalog means English.
 * NULL context and "" context are distinct, as in gettext. NULL msgid returns
 * "". gettext uses no context; pgettext uses the exact supplied context.
 */
XXFC_API const char *xx_translation_gettext(const xx_translation *catalog, const char *msgid);
XXFC_API const char *xx_translation_pgettext(const xx_translation *catalog, const char *context, const char *msgid);
XXFC_API const char *xx_translation_ngettext(const xx_translation *catalog, const char *singular, const char *plural, uint64_t n);
XXFC_API const char *xx_translation_npgettext(const xx_translation *catalog, const char *context, const char *singular, const char *plural, uint64_t n);

#ifdef __cplusplus
}
#endif
#endif
