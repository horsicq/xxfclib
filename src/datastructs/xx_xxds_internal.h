/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The seam between the script host (xx_xxds.c) and the readers
 * (xx_xxds_readers.c). Readers contribute three things and no structure
 * logic: which script serves a reader the file type alone does not identify,
 * the decoded inner format of a wrapper type, and fact tables.
 */
#ifndef XX_XXDS_INTERNAL_H
#define XX_XXDS_INTERNAL_H

#include "xxfclib/datastructs/xx_xxds.h"

/* Script name for a reader recognized by identity (its callbacks) rather
 * than by file type, or NULL to select by file type. */
const char *xx_xxds_reader_script(Abstractformat *format);

/* The format whose structures are reported for `format`: the decoded inner
 * format of a wrapper type, otherwise `format` itself. NULL when the inner
 * format cannot be opened. */
Abstractformat *xx_xxds_reader_inner(Abstractformat *format, xx_pd_struct *pd);

/* The span of the outer container a reader authenticated around its format (PSA_DISK, BINSH_STARKIT): the
 * structures of such a type may lie before the format base. `base` is a device offset below
 * format->base_address, `size` the container size. False for every other reader. */
bool xx_xxds_reader_container(Abstractformat *format, int64_t *base, int64_t *size);

/* Alias types: a file type whose structures are those of another (canonical) type, read by that type's
 * reader over the same bytes (catalog names of music formats, TAR in an outer compressor). The canonical
 * type of `type`, or `type` itself. */
xx_file_type_t xx_xxds_reader_alias_type(xx_file_type_t type);

/* Opens the canonical reader of an alias format on the same device and base. True with *canonical == NULL
 * when `format` is not an alias; false when it is one and the canonical reader rejects the bytes. The caller
 * closes a reader it got with xx_xxds_reader_alias_close(). */
bool xx_xxds_reader_alias_open(Abstractformat *format, xx_pd_struct *pd, Abstractformat **canonical);
void xx_xxds_reader_alias_close(Abstractformat *canonical);

/* Fills a fact table of `format`. False when the reader has no such table. */
bool xx_xxds_reader_table(Abstractformat *format, const char *name, xx_xxds_table *table, xx_pd_struct *pd);

/* Decodes one field of an occurrence into a record: the common formatter
 * plus the extended field types (xx_xxds_fields.c). `format` is the format
 * the script ran on; the record is owned by the caller on success. */
bool xx_xxds_populate_field(xx_data_struct_record *record, Abstractformat *format, const xx_data_struct *parent, const xx_data_struct_field_desc *field,
                            bool big_endian);

#endif /* XX_XXDS_INTERNAL_H */
