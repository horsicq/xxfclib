/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_DOTNET_READER_H
#define XXFCLIB_DOTNET_READER_H

#include "xxfclib/formats/dotnet/xx_dotnet_inspect.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dotnet_method_body {
    int64_t header_offset, code_offset;
    uint32_t code_size, local_signature_token, total_size;
    uint16_t flags, max_stack;
    bool init_locals, has_exception_handlers;
} xx_dotnet_method_body;

/* Queries borrow an analyzed inspection and allocate nothing. Offsets are
 * relative to its format base, including when the underlying device embeds
 * the PE. Both the inspection-view and parent-device cursors are preserved.
 * Failure clears every supplied output. Each query resets the inspection's
 * read-work/failure state and honors its cancellation object. */

/* RID is one-based. The complete parsed table extent must fit both its
 * metadata-tables stream and the bounded inspection input. */
XXFC_API bool xx_dotnet_inspect_table_row(xx_dotnet_inspection *state,
    unsigned table, uint32_t rid, int64_t *offset, uint32_t *size);

/* Return the #Blob payload after its canonical compressed-length prefix.
 * Index zero denotes the reserved empty entry (heap offset, zero size),
 * and requires that the heap's initial reserved byte exists and is zero. */
XXFC_API bool xx_dotnet_inspect_blob(xx_dotnet_inspection *state,
    uint32_t index, int64_t *offset, uint32_t *size);

/* Read a tiny or 12-byte fat CIL header and validate the entire method extent
 * within one contiguous raw-backed PE range; virtual zero-fill is excluded.
 * total_size includes headers, IL, alignment and chained EH sections.
 * Local-signature and catch-type tokens must name existing metadata rows.
 * Exception clause ranges and filter offsets are bounded by code_size.
 * Limits: 16 MiB IL, 64 EH sections and 4096 exception clauses. This is a
 * structural reader, not a CIL verifier or an exception implementation. */
XXFC_API bool xx_dotnet_inspect_method_body(xx_dotnet_inspection *state,
    uint32_t rva, xx_dotnet_method_body *body);

#ifdef __cplusplus
}
#endif
#endif
