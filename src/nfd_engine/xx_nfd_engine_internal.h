/* Copyright (c) 2017-2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Internal C state shared by the SpecAbstract-derived NFD modules.
 */

#ifndef XX_NFD_ENGINE_INTERNAL_H
#define XX_NFD_ENGINE_INTERNAL_H

#include "xxfclib/nfd_engine/nfd_tables.h"
#include "xxfclib/scan/xx_scan.h"

typedef struct xx_nfd_name_entry_s {
    const char *token;
    const char *display;
    uint32_t order;
} xx_nfd_name_entry;

const char *xx_nfd_record_type_name(const char *token);
const char *xx_nfd_record_name(const char *token);
uint32_t xx_nfd_record_name_order(const char *token);
const xx_nfd_table *xx_nfd_find_table(const char *name, xx_nfd_table_kind kind);

typedef enum xx_nfd_result_group_e {
    XX_NFD_GROUP_OPERATION_SYSTEM = 0,
    XX_NFD_GROUP_FORMAT,
    XX_NFD_GROUP_DOS_EXTENDER,
    XX_NFD_GROUP_LINKER,
    XX_NFD_GROUP_COMPILER,
    XX_NFD_GROUP_LANGUAGE,
    XX_NFD_GROUP_LIBRARY,
    XX_NFD_GROUP_TOOL,
    XX_NFD_GROUP_PACKER,
    XX_NFD_GROUP_SFX,
    XX_NFD_GROUP_PROTECTOR,
    XX_NFD_GROUP_APK_PROTECTOR,
    XX_NFD_GROUP_DONGLE,
    XX_NFD_GROUP_SIGN_TOOL,
    XX_NFD_GROUP_INSTALLER,
    XX_NFD_GROUP_JOINER,
    XX_NFD_GROUP_PE_TOOL,
    XX_NFD_GROUP_TEXT,
    XX_NFD_GROUP_ARCHIVE,
    XX_NFD_GROUP_CERTIFICATE,
    XX_NFD_GROUP_DEBUG_DATA,
    XX_NFD_GROUP_INSTALLER_DATA,
    XX_NFD_GROUP_SFX_DATA,
    XX_NFD_GROUP_PROTECTOR_DATA,
    XX_NFD_GROUP_LIBRARY_DATA,
    XX_NFD_GROUP_RESOURCE,
    XX_NFD_GROUP_DATABASE,
    XX_NFD_GROUP_IMAGE
} xx_nfd_result_group;

typedef struct xx_nfd_pending_record_s {
    xx_nfd_result_group group;
    uint32_t name_order;
    char *name_token;
    char *type;
    char *name;
    char *version;
    char *info;
    bool is_heuristic;
    bool is_aggressive;
    bool is_unknown;
} xx_nfd_pending_record;

typedef struct xx_nfd_binary_state_s {
    xx_io_device *device;
    const xx_scan_region *region;
    const xx_scan_options *options;
    const xx_nfd_signature_row **headers;
    size_t header_count;
    size_t header_capacity;
    xx_nfd_pending_record *records;
    size_t record_count;
    size_t record_capacity;
    bool is_plain_text;
    bool is_utf8;
    int unicode_type;
    char *header_text;
} xx_nfd_binary_state;

bool xx_nfd_binary_scan(xx_nfd_binary_state *state, xx_scan_result *result,
                        xx_pd_struct *pd);
bool xx_nfd_text_scan(xx_nfd_binary_state *state, xx_pd_struct *pd);
bool xx_nfd_pending_put(xx_nfd_binary_state *state, xx_nfd_result_group group,
                        const char *name_token, const char *type, const char *name,
                        const char *version, const char *info, bool heuristic,
                        bool aggressive, bool unknown, xx_pd_struct *pd);

#endif /* XX_NFD_ENGINE_INTERNAL_H */
