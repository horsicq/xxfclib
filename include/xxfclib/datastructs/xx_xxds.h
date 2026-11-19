/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DataStructs scripts (.xxds). The structures of a file type are described
 * by a script in src/datastructs; this host runs it and serves the public
 * DataStructs stream API from its answers. src/datastructs/RULES.md is the
 * script contract.
 */
#ifndef XXFCLIB_XX_XXDS_H
#define XXFCLIB_XX_XXDS_H

#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/xx_format_streams.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The script-backed provider for a format, or NULL when no script serves it. */
XXFC_API const StreamDataStructs *xx_xxds_get_stream_datastructs(Abstractformat *format);

/* Load <name>.xxds from this directory in preference to the embedded copy.
 * A development and test aid: call it before the first DataStructs request.
 * NULL or "" restores the embedded scripts. */
XXFC_API void xx_xxds_set_script_directory(const char *path);

/* Message of the most recent script failure on any thread ("" when none).
 * Diagnostic only; valid until the next DataStructs call. */
XXFC_API const char *xx_xxds_last_error(void);

/* Releases every cached script context. Safe to call at exit. */
XXFC_API void xx_xxds_shutdown(void);

/* ---------------------------------------------------------------------- *
 * Reader fact tables (RULES.md, "What stays in C"). A reader that knows a
 * boundary only after decoding publishes it as rows of named values, which
 * a script reads with Format.table(name). Offsets are relative to the start
 * of the format. Each function returns false once the table has failed.
 * ---------------------------------------------------------------------- */
typedef struct xx_xxds_table xx_xxds_table;

XXFC_API bool xx_xxds_table_row(xx_xxds_table *table);
XXFC_API bool xx_xxds_table_int(xx_xxds_table *table, const char *key, int64_t value);
XXFC_API bool xx_xxds_table_str(xx_xxds_table *table, const char *key, const char *value, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_XXDS_H */
