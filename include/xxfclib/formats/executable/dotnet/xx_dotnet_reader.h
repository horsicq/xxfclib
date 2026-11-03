/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_DOTNET_READER_H
#define XXFCLIB_DOTNET_READER_H

#include "xxfclib/formats/executable/dotnet/xx_dotnet_inspect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dotnet_method_body {
    int64_t header_offset, code_offset;
    uint32_t code_size, local_signature_token, total_size;
    uint16_t flags, max_stack;
    bool init_locals, has_exception_handlers;
} xx_dotnet_method_body;

#define XX_DOTNET_METADATA_MAX_COLUMNS 9
#define XX_DOTNET_METADATA_TEXT_CAPACITY 512
typedef enum xx_dotnet_metadata_kind {
    XX_DOTNET_METADATA_INTEGER,
    XX_DOTNET_METADATA_FLAGS,
    XX_DOTNET_METADATA_STRING,
    XX_DOTNET_METADATA_GUID,
    XX_DOTNET_METADATA_BLOB,
    XX_DOTNET_METADATA_TABLE,
    XX_DOTNET_METADATA_CODED,
    XX_DOTNET_METADATA_LIST,
    XX_DOTNET_METADATA_TOKEN
} xx_dotnet_metadata_kind;
typedef struct xx_dotnet_metadata_column {
    const char *name;
    xx_dotnet_metadata_kind kind;
    uint32_t raw, token;
    int64_t offset; /* Heap payload offset, or referenced row offset. */
    uint32_t size;  /* Heap payload bytes, or referenced row size. */
    bool valid, is_null, truncated;
    char text[XX_DOTNET_METADATA_TEXT_CAPACITY];
} xx_dotnet_metadata_column;
typedef struct xx_dotnet_metadata_row {
    const char *name;
    unsigned table, count;
    uint32_t token, size;
    int64_t offset;
    bool valid;
    xx_dotnet_metadata_column columns[XX_DOTNET_METADATA_MAX_COLUMNS];
} xx_dotnet_metadata_row;

/* ECMA-335 tables 0x00..0x2c and embedded Portable PDB tables 0x30..0x37.
 * NULL names identify reserved/unsupported table numbers. No allocation is
 * transferred to callers. A successful query preserves every raw value;
 * invalid heap/reference/signature values set column.valid and row.valid
 * false with a diagnostic in text. False means the row itself cannot be
 * read. Text is bounded, terminal-safe UTF-8; truncated is explicit. Large
 * blobs retain their complete offset/size plus a bounded hexadecimal preview.
 * Ordinary field/method/property/local/type/generic-instantiation signatures
 * are decoded with depth/work limits. Custom attribute, marshaling, security
 * and debug payloads retain their bytes, since interpretation can require
 * external assemblies or producer-specific schemas. */
XXFC_API const char *xx_dotnet_metadata_table_name(unsigned table);
XXFC_API bool xx_dotnet_inspect_decode_row(xx_dotnet_inspection *state, unsigned table, uint32_t rid, xx_dotnet_metadata_row *row);

/* Queries borrow an analyzed inspection and allocate nothing. Offsets are
 * relative to its format base, including when the underlying device embeds
 * the PE. Both the inspection-view and parent-device cursors are preserved.
 * Failure clears every supplied output. Each query resets the inspection's
 * read-work/failure state and honors its cancellation object. */

/* RID is one-based. The complete parsed table extent must fit both its
 * metadata-tables stream and the bounded inspection input. */
XXFC_API bool xx_dotnet_inspect_table_row(xx_dotnet_inspection *state, unsigned table, uint32_t rid, int64_t *offset, uint32_t *size);

/* Return the #Blob payload after its canonical compressed-length prefix.
 * Index zero denotes the reserved empty entry (heap offset, zero size),
 * and requires that the heap's initial reserved byte exists and is zero. */
XXFC_API bool xx_dotnet_inspect_blob(xx_dotnet_inspection *state, uint32_t index, int64_t *offset, uint32_t *size);

/* Read a tiny or 12-byte fat CIL header and validate the entire method extent
 * within one contiguous raw-backed PE range; virtual zero-fill is excluded.
 * total_size includes headers, IL, alignment and chained EH sections.
 * Local-signature and catch-type tokens must name existing metadata rows.
 * Exception clause ranges and filter offsets are bounded by code_size.
 * Limits: 16 MiB IL, 64 EH sections and 4096 exception clauses. This is a
 * structural reader, not a CIL verifier or an exception implementation. */
XXFC_API bool xx_dotnet_inspect_method_body(xx_dotnet_inspection *state, uint32_t rva, xx_dotnet_method_body *body);

/* Explicit static-analysis compatibility for historical producers whose fat
 * method header RVA is unaligned. EH sections still align to absolute RVAs;
 * every existing extent/token/count check remains enabled. The strict API
 * above is identical to calling _ex with flags=0. */
#define XX_DOTNET_METHOD_BODY_ALLOW_UNALIGNED_FAT UINT32_C(1)
XXFC_API bool xx_dotnet_inspect_method_body_ex(xx_dotnet_inspection *state, uint32_t rva,
    uint32_t flags, xx_dotnet_method_body *body);

#ifdef __cplusplus
}
#endif
#endif
