/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Derived from SpecAbstract's MIT-licensed NFD table representation.
 */

#include "xxfclib/nfd_engine/nfd_tables.h"
#include "xxfclib/rt/xx_rt.h"

#include "generated/xx_nfd_engine_tables.inc"

const xx_nfd_table *xx_nfd_tables_get(size_t *count)
{
    if (count) *count = XX_NFD_GENERATED_TABLE_COUNT;
    return xx_nfd_tables;
}

size_t xx_nfd_tables_row_count(void)
{
    return XX_NFD_GENERATED_ROW_COUNT;
}

const char *xx_nfd_tables_content_sha256(void)
{
    return XX_NFD_GENERATED_CONTENT_SHA256;
}

const char *xx_nfd_tables_data_version(void)
{
    return XX_NFD_GENERATED_DATA_VERSION;
}

bool xx_nfd_signature_match(const xx_nfd_signature_row *row, xx_io_device *device,
                            int64_t offset, const xx_data_sig_context *context,
                            int64_t *end_offset)
{
    if (!row || !device || row->signature.count <= 0) return false;
    return xx_io_signature_match(device, offset, &row->signature, context, end_offset);
}

bool xx_nfd_string_match(const xx_nfd_string_row *row, const void *data, size_t size)
{
    if (!row || row->is_expression || (!data && size)) return false;
    return size == row->string_size && xx_rt_memcmp(data, row->string, size) == 0;
}

bool xx_nfd_const_match(const xx_nfd_const_row *row, uint64_t value1, uint64_t value2)
{
    return row && row->value1 == value1 && row->value2 == value2;
}

static bool resource_part_match(bool is_string, const char *expected, uint32_t expected_id,
                                const char *name, uint32_t id)
{
    if (is_string) return name && xx_rt_strcmp(expected, name) == 0;
    return expected_id == id;
}

bool xx_nfd_resource_match(const xx_nfd_resource_row *row,
                           const char *name1, uint32_t id1,
                           const char *name2, uint32_t id2)
{
    return row && resource_part_match(row->is_string1, row->name1, row->id1, name1, id1) &&
           resource_part_match(row->is_string2, row->name2, row->id2, name2, id2);
}

bool xx_nfd_msrich_match(const xx_nfd_msrich_row *row, uint16_t id, uint32_t build)
{
    return row && row->id == id && (row->build == UINT32_MAX || row->build == build);
}
