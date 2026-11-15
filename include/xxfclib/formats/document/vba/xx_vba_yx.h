/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* YARA-X-exact VBA project helpers.
 *
 * Reproduces, bit for bit, the parsing behavior of YARA-X 1.21.0's vba
 * module (and of the xxyara port of it): the OLE compound file reader
 * (DIFAT/FAT/MiniFAT with the 256 MiB stream cap), the ZIP-packaged Office
 * document container, _VBA_PROJECT / VBA / dir / PROJECT stream parsing, the
 * MS-OVBA compressed-stream decoder (MS-OVBA 2.4.1) with the 64 MiB
 * decompressed cap, and the legacy codepage tables used by the compressed
 * source decoding.
 *
 * The result owns every string, module code buffer and reference array;
 * release it with xx_vba_yx_result_free. A valid call always produces a
 * defined has_macros value. False indicates bad API arguments; malformed
 * input produces has_macros=false.
 */

#ifndef XXFCLIB_VBA_YX_H
#define XXFCLIB_VBA_YX_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Values of the public vba.ModuleType enum. */
enum {
    XX_VBA_YX_MODULE_STANDARD = 1,
    XX_VBA_YX_MODULE_CLASS = 2,
    XX_VBA_YX_MODULE_UNKNOWN = 3
};

typedef struct {
    char *name;
    size_t name_length;
    uint32_t type;
    char *code;
    size_t code_length;
} xx_vba_yx_module;

typedef struct {
    /* This field is defined even for non-Office or malformed input. */
    bool has_macros;
    xx_vba_yx_module *modules;
    size_t module_count;

    bool has_project_info;
    char *project_name;
    size_t project_name_length;
    char *project_version;
    size_t project_version_length;
    char **references;
    size_t reference_count;
    int32_t declared_module_count;
} xx_vba_yx_result;

/* Parse a VBA project from a scanned OLE compound file or ZIP-packaged Office
 * document. A valid call
 * always produces a defined has_macros value. False indicates bad API
 * arguments; malformed input produces has_macros=false. */
bool xx_vba_yx_parse(const uint8_t *data, size_t size, xx_vba_yx_result *result);
void xx_vba_yx_result_free(xx_vba_yx_result *result);

/* Decode an MS-OVBA compressed stream. Exposed for focused conformance tests.
 * Caller owns *output and must free it. False means invalid or truncated data,
 * or a resource limit was reached. */
bool xx_vba_yx_decompress(const uint8_t *data, size_t size,
                       uint8_t **output, size_t *output_size);

#endif
