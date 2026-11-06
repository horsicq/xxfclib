/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Canonical record spelling is generated from XScanEngine/xscanengine.cpp.
 */

#include "xx_nfd_engine_internal.h"
#include "xxfclib/rt/xx_rt.h"

#include "generated/xx_nfd_engine_names.inc"

static const xx_nfd_name_entry *find_name(const xx_nfd_name_entry *entries,
                                          size_t count, const char *token)
{
    size_t i;
    if (!token) return NULL;
    for (i = 0; i < count; ++i) {
        if (xx_rt_strcmp(entries[i].token, token) == 0) return &entries[i];
    }
    return NULL;
}

const char *xx_nfd_record_type_name(const char *token)
{
    const xx_nfd_name_entry *entry = find_name(xx_nfd_record_types,
                                                XX_NFD_RECORD_TYPE_COUNT, token);
    return entry ? entry->display : "Unknown";
}

const char *xx_nfd_record_name(const char *token)
{
    const xx_nfd_name_entry *entry = find_name(xx_nfd_record_names,
                                                XX_NFD_RECORD_NAME_COUNT, token);
    return entry ? entry->display : "Unknown";
}

uint32_t xx_nfd_record_name_order(const char *token)
{
    const xx_nfd_name_entry *entry = find_name(xx_nfd_record_names,
                                                XX_NFD_RECORD_NAME_COUNT, token);
    return entry ? entry->order : UINT32_MAX;
}

const xx_nfd_table *xx_nfd_find_table(const char *name, xx_nfd_table_kind kind)
{
    const xx_nfd_table *tables;
    size_t count;
    size_t i;
    if (!name) return NULL;
    tables = xx_nfd_tables_get(&count);
    for (i = 0; i < count; ++i) {
        if (tables[i].kind == kind && xx_rt_strcmp(tables[i].name, name) == 0) {
            return &tables[i];
        }
    }
    return NULL;
}
