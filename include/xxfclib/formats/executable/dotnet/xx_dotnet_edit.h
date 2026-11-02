/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_DOTNET_EDIT_H
#define XXFCLIB_DOTNET_EDIT_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DOTNET_EDIT_MAX_IMAGE UINT32_C(0x10000000)
#define XX_DOTNET_US_MAX_HEAP UINT32_C(0x1000000)

/* Canonical UTF8 to UTF16 code units. NULL output validates and queries the
 * required unit count. Failure clears *units and does not change output.
 * Input is bounded by XX_DOTNET_US_MAX_HEAP; empty input may be NULL.
 * Input/output overlap is rejected. No allocation
 * or Unicode normalization occurs. */
XXFC_API bool xx_dotnet_utf8_to_utf16(const uint8_t *input, size_t input_size,
    uint16_t *output, size_t capacity, size_t *units);

/* Encode one ECMA-335 #US entry: compressed byte count, little-endian UTF16
 * units and terminal special-character flag. Lone surrogates are preserved.
 * NULL output queries its full encoded size. Failure clears *encoded_size
 * and leaves output unchanged. Input/output overlap is rejected. The entry
 * is bounded by the #US token limit. */
XXFC_API bool xx_dotnet_us_encode(const uint16_t *units, size_t count,
    uint8_t *output, size_t capacity, size_t *encoded_size);

/* Relocate PE/CLI metadata and replace its #US heap in a fresh owned buffer.
 * This independently validates the input file's complete editable structural
 * layout; it trusts no caller-provided offsets or prior inspection. It admits
 * unsigned IL-only PE32/PE32+ files with four/five standard metadata streams,
 * no overlay/native CLR directories, and an extendible final section. Stream
 * offsets need not be aligned. Tables and other heaps remain byte-identical;
 * method bodies are not decoded or executed. A missing #US header is added.
 *
 * Input metadata and pool are bounded by XX_DOTNET_US_MAX_HEAP. pool must
 * have its reserved initial zero byte.
 * max_output is an explicit positive file-size bound, at most MAX_IMAGE.
 * On failure output/size are cleared; error, when supplied, is static text.
 * Input and pool are unchanged. Release success output with edit_free(). */
XXFC_API bool xx_dotnet_rebuild_user_strings(const uint8_t *file, size_t file_size,
    const uint8_t *pool, size_t pool_size, size_t max_output,
    uint8_t **output, size_t *output_size, const char **error);
/* Turn an unsigned IL-only netmodule into an executable assembly using an
 * unsigned donor's Assembly identity and a validated static managed entry
 * MethodDef token. Existing-assembly input keeps its identity. Metadata and
 * heaps are rebuilt in a new owned file; original method/resource bytes stay
 * unchanged. Heap-index width changes, signed identities and unsupported entry
 * signatures are rejected. The same editable PE layout bounds as above apply.
 * A source _CorDllMain import is renamed _CorExeMain when promoting a module.
 * On failure output/size are cleared. Release output with edit_free(). */
XXFC_API bool xx_dotnet_promote_module(const uint8_t *file, size_t file_size,
    const uint8_t *donor, size_t donor_size, uint32_t entry_token, size_t max_output,
    uint8_t **output, size_t *output_size, const char **error);
XXFC_API void xx_dotnet_edit_free(void *buffer);

#ifdef __cplusplus
}
#endif
#endif
