/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact OLECF (Compound File Binary Format) helpers.
 *
 * xxfclib's regular CFBF support is an internal detection/extraction parser
 * shared with the Office Binder reader. The functions in this header instead
 * reproduce, bit for bit, the parsing behavior of YARA-X 1.21.0's olecf
 * module and its Olecf container (lib/src/modules/olecf/parser.rs): DIFAT /
 * FAT / MiniFAT resolution, directory entries decoded in physical IndexMap
 * order, the 512/4096-byte sector size handling, and stream reassembly
 * through the mini stream for entries below 4096 bytes with the 256 MiB cap.
 *
 * The document borrows the scanned buffer and must be closed with
 * xx_olecf_yx_close. Sequential chains borrow the input (*owner is NULL on
 * return of xx_olecf_yx_entry_data); any other chain is gathered into
 * *owner, which the caller frees.
 */

#ifndef XXFCLIB_OLECF_YX_H
#define XXFCLIB_OLECF_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Public YARA-X olecf module fields. The module is experimental and must be
 * enabled explicitly in YARA-X v1.21.0; these helpers implement its exposed
 * is_olecf flag and streams collection. */

typedef struct {
    char name[128]; /* UTF-8, NUL-terminated */
    size_t name_length;
    uint32_t type; /* UNKNOWN=0, STORAGE=1, STREAM=2, ROOT=5 */
    uint64_t size;
    uint32_t start_sector; /* first FAT or MiniFAT sector */
} xx_olecf_yx_stream;

/* Nested enum constants: StreamType.UNKNOWN/STORAGE/STREAM/ROOT. */
bool xx_olecf_yx_constant(const char *member, uint64_t *value);

/* Always defined: false for non-OLECF or malformed input. */
bool xx_olecf_yx_is_olecf(const uint8_t *data, size_t size);

/* Always defined for valid arguments, including count zero on non-OLECF.
 * Returns false only for invalid arguments or allocation failure. */
bool xx_olecf_yx_stream_count(const uint8_t *data, size_t size,
                            uint64_t *count);

/* The zero-based stream index follows the order in YARA-X's IndexMap.
 * Returns false when index is out of range or the file is invalid. */
bool xx_olecf_yx_stream_get(const uint8_t *data, size_t size,
                          uint64_t index, xx_olecf_yx_stream *stream);

/* A compound file parsed once, for readers of stream contents (the msi
 * module). It mirrors YARA-X's Olecf (lib/src/modules/olecf/parser.rs). */
typedef struct xx_olecf_yx_document xx_olecf_yx_document;

/* NULL for non-OLECF input (or allocation failure). */
xx_olecf_yx_document *xx_olecf_yx_open(const uint8_t *data, size_t size);
void xx_olecf_yx_close(xx_olecf_yx_document *document);
size_t xx_olecf_yx_sector_size(const xx_olecf_yx_document *document);
size_t xx_olecf_yx_entry_count(const xx_olecf_yx_document *document);
/* Directory entries in IndexMap order (Olecf::streams). */
const xx_olecf_yx_stream *xx_olecf_yx_entry(const xx_olecf_yx_document *document,
                                      size_t index);

/* Contents of entry `index`, like Olecf::get_stream_data: entries smaller
 * than 4096 bytes other than the root storage are read through the MiniFAT
 * from the root mini stream, the others through the FAT. A sequential chain
 * borrows from the scanned data (*owner is NULL); any other chain is
 * gathered into *owner, which the caller frees, and *bytes points into it.
 * Fails for a broken, cyclic or truncated chain or a stream above 256 MiB. */
bool xx_olecf_yx_entry_data(const xx_olecf_yx_document *document, size_t index,
                         const uint8_t **bytes, size_t *size,
                         uint8_t **owner);

#endif
