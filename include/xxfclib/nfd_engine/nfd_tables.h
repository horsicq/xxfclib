/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Const POD representation of the SpecAbstract NFD knowledge tables.
 */

#ifndef XX_NFD_TABLES_H
#define XX_NFD_TABLES_H

#include "xxfclib/formats/xx_data_signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_nfd_table_kind_e {
    XX_NFD_TABLE_SIGNATURE = 0,
    XX_NFD_TABLE_STRING,
    XX_NFD_TABLE_CONST,
    XX_NFD_TABLE_RESOURCE,
    XX_NFD_TABLE_MSRICH,
    XX_NFD_TABLE_VINTAGE
} xx_nfd_table_kind;

typedef struct xx_nfd_provenance_s {
    const char *origin;
    const char *source_file;
    uint32_t source_line;
} xx_nfd_provenance;

typedef struct xx_nfd_basic_info_s {
    uint32_t variant;
    const char *file_type;
    const char *record_type;
    const char *record_name;
    const char *version;
    const char *info;
} xx_nfd_basic_info;

typedef struct xx_nfd_signature_row_s {
    xx_nfd_basic_info basic;
    xx_data_signature signature;
    const char *source_pattern;
    xx_nfd_provenance provenance;
} xx_nfd_signature_row;

typedef struct xx_nfd_string_row_s {
    xx_nfd_basic_info basic;
    const char *string;
    size_t string_size;
    bool is_expression;
    xx_nfd_provenance provenance;
} xx_nfd_string_row;

typedef struct xx_nfd_const_row_s {
    xx_nfd_basic_info basic;
    uint64_t value1;
    uint64_t value2;
    xx_nfd_provenance provenance;
} xx_nfd_const_row;

typedef struct xx_nfd_resource_row_s {
    xx_nfd_basic_info basic;
    bool is_string1;
    const char *name1;
    uint32_t id1;
    bool is_string2;
    const char *name2;
    uint32_t id2;
    xx_nfd_provenance provenance;
} xx_nfd_resource_row;

typedef struct xx_nfd_msrich_row_s {
    xx_nfd_basic_info basic;
    uint16_t id;
    uint32_t build;
    xx_nfd_provenance provenance;
} xx_nfd_msrich_row;

typedef struct xx_nfd_vintage_row_s {
    const char *string;
    size_t string_size;
    const char *record_name;
    const char *version;
    xx_nfd_provenance provenance;
} xx_nfd_vintage_row;

typedef struct xx_nfd_table_s {
    const char *family;
    const char *name;
    xx_nfd_table_kind kind;
    const void *rows;
    size_t row_size;
    size_t row_count;
} xx_nfd_table;

XXFC_API const xx_nfd_table *xx_nfd_tables_get(size_t *count);
XXFC_API size_t xx_nfd_tables_row_count(void);
XXFC_API const char *xx_nfd_tables_content_sha256(void);
XXFC_API const char *xx_nfd_tables_data_version(void);

XXFC_API bool xx_nfd_signature_match(const xx_nfd_signature_row *row, xx_io_device *device,
                                     int64_t offset, const xx_data_sig_context *context,
                                     int64_t *end_offset);
XXFC_API bool xx_nfd_string_match(const xx_nfd_string_row *row, const void *data, size_t size);
XXFC_API bool xx_nfd_const_match(const xx_nfd_const_row *row, uint64_t value1, uint64_t value2);
XXFC_API bool xx_nfd_resource_match(const xx_nfd_resource_row *row,
                                    const char *name1, uint32_t id1,
                                    const char *name2, uint32_t id2);
XXFC_API bool xx_nfd_msrich_match(const xx_nfd_msrich_row *row, uint16_t id, uint32_t build);

#ifdef __cplusplus
}
#endif

#endif /* XX_NFD_TABLES_H */
