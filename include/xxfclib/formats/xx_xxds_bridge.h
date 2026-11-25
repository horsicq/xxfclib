/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The reader side of the DataStructs scripts. The script host and the .xxds
 * scripts live in the separate xxdatastructs library; what only a reader can
 * know stays here, next to the readers (src/formats/xx_xxds_readers.c):
 * which script serves a reader the file type alone does not identify, the
 * decoded inner format of a wrapper type, alias types, and fact tables.
 * No structure descriptions live in xxfclib.
 *
 * The host installs itself with xx_format_set_datastructs_provider()
 * (xx_format_streams.h); until then xxfclib reports no DataStructs.
 */
#ifndef XXFCLIB_XX_XXDS_BRIDGE_H
#define XXFCLIB_XX_XXDS_BRIDGE_H

#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/xx_format_streams.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- *
 * Reader fact tables (RULES.md, "What stays in C"). A reader that knows a
 * boundary only after decoding publishes it as rows of named values, which
 * a script reads with Format.table(name). Offsets are relative to the start
 * of the format. Each function returns false once the table has failed.
 *
 * The host owns the table: it embeds xx_xxds_table as the first member of
 * its own structure and points `ops` at its implementation.
 * ---------------------------------------------------------------------- */
typedef struct xx_xxds_table xx_xxds_table;

typedef struct xx_xxds_table_ops {
    bool (*row)(xx_xxds_table *table);
    bool (*set_int)(xx_xxds_table *table, const char *key, int64_t value);
    bool (*set_str)(xx_xxds_table *table, const char *key, const char *value, size_t size);
} xx_xxds_table_ops;

struct xx_xxds_table {
    const xx_xxds_table_ops *ops;
};

XXFC_API bool xx_xxds_table_row(xx_xxds_table *table);
XXFC_API bool xx_xxds_table_int(xx_xxds_table *table, const char *key, int64_t value);
XXFC_API bool xx_xxds_table_str(xx_xxds_table *table, const char *key, const char *value, size_t size);

/* Script name for a reader recognized by identity (its callbacks) rather
 * than by file type, or NULL to select by file type. */
XXFC_API const char *xx_xxds_reader_script(Abstractformat *format);

/* The format whose structures are reported for `format`: the decoded inner
 * format of a wrapper type, otherwise `format` itself. NULL when the inner
 * format cannot be opened. */
XXFC_API Abstractformat *xx_xxds_reader_inner(Abstractformat *format, xx_pd_struct *pd);

/* The span of the outer container a reader authenticated around its format (PSA_DISK, BINSH_STARKIT): the
 * structures of such a type may lie before the format base. `base` is a device offset below
 * format->base_address, `size` the container size. False for every other reader. */
XXFC_API bool xx_xxds_reader_container(Abstractformat *format, int64_t *base, int64_t *size);

/* Alias types: a file type whose structures are those of another (canonical) type, read by that type's
 * reader over the same bytes (catalog names of music formats, TAR in an outer compressor). The canonical
 * type of `type`, or `type` itself. */
XXFC_API xx_file_type_t xx_xxds_reader_alias_type(xx_file_type_t type);

/* Opens the canonical reader of an alias format on the same device and base. True with *canonical == NULL
 * when `format` is not an alias; false when it is one and the canonical reader rejects the bytes. The caller
 * closes a reader it got with xx_xxds_reader_alias_close(). */
XXFC_API bool xx_xxds_reader_alias_open(Abstractformat *format, xx_pd_struct *pd, Abstractformat **canonical);
XXFC_API void xx_xxds_reader_alias_close(Abstractformat *canonical);

/* Fills a fact table of `format`. False when the reader has no such table. */
XXFC_API bool xx_xxds_reader_table(Abstractformat *format, const char *name, xx_xxds_table *table, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_XX_XXDS_BRIDGE_H */
