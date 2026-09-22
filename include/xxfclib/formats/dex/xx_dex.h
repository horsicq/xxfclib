/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_DEX_H
#define XXFCLIB_FORMAT_DEX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DEX_MAGIC_SIZE 8U
#define XX_DEX_SIGNATURE_SIZE 20U
#define XX_DEX_HEADER_SIZE 112U
#define XX_DEX_ENDIAN_CONSTANT UINT32_C(0x12345678)
#define XX_DEX_REVERSE_ENDIAN_CONSTANT UINT32_C(0x78563412)

typedef enum xx_dex_map_item_type_e {
    XX_DEX_MAP_HEADER_ITEM = 0x0000,
    XX_DEX_MAP_STRING_ID_ITEM = 0x0001,
    XX_DEX_MAP_TYPE_ID_ITEM = 0x0002,
    XX_DEX_MAP_PROTO_ID_ITEM = 0x0003,
    XX_DEX_MAP_FIELD_ID_ITEM = 0x0004,
    XX_DEX_MAP_METHOD_ID_ITEM = 0x0005,
    XX_DEX_MAP_CLASS_DEF_ITEM = 0x0006,
    XX_DEX_MAP_CALL_SITE_ID_ITEM = 0x0007,
    XX_DEX_MAP_METHOD_HANDLE_ITEM = 0x0008,
    XX_DEX_MAP_MAP_LIST = 0x1000,
    XX_DEX_MAP_TYPE_LIST = 0x1001,
    XX_DEX_MAP_ANNOTATION_SET_REF_LIST = 0x1002,
    XX_DEX_MAP_ANNOTATION_SET_ITEM = 0x1003,
    XX_DEX_MAP_CLASS_DATA_ITEM = 0x2000,
    XX_DEX_MAP_CODE_ITEM = 0x2001,
    XX_DEX_MAP_STRING_DATA_ITEM = 0x2002,
    XX_DEX_MAP_DEBUG_INFO_ITEM = 0x2003,
    XX_DEX_MAP_ANNOTATION_ITEM = 0x2004,
    XX_DEX_MAP_ENCODED_ARRAY_ITEM = 0x2005,
    XX_DEX_MAP_ANNOTATIONS_DIRECTORY_ITEM = 0x2006,
    XX_DEX_MAP_HIDDENAPI_CLASS_DATA_ITEM = 0xf000
} xx_dex_map_item_type_t;

typedef struct xx_dex_header_s {
    uint8_t magic[XX_DEX_MAGIC_SIZE];
    uint32_t checksum;
    uint8_t signature[XX_DEX_SIGNATURE_SIZE];
    uint32_t file_size;
    uint32_t header_size;
    uint32_t endian_tag;
    uint32_t link_size;
    uint32_t link_off;
    uint32_t map_off;
    uint32_t string_ids_size;
    uint32_t string_ids_off;
    uint32_t type_ids_size;
    uint32_t type_ids_off;
    uint32_t proto_ids_size;
    uint32_t proto_ids_off;
    uint32_t field_ids_size;
    uint32_t field_ids_off;
    uint32_t method_ids_size;
    uint32_t method_ids_off;
    uint32_t class_defs_size;
    uint32_t class_defs_off;
    uint32_t data_size;
    uint32_t data_off;
} xx_dex_header;

typedef struct xx_dex_map_item_s {
    uint16_t type;
    uint32_t count;
    uint32_t offset;
} xx_dex_map_item;

typedef struct xx_dex {
    Abstractformat format;
    xx_dex_header header;
    uint32_t version_number;
    bool is_big_endian;
} xx_dex;

typedef xx_dex xx_dex_t;
typedef xx_dex XDEX;

XXFC_API void xx_dex_init(xx_dex *dex, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_dex *xx_dex_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dex_destroy(xx_dex *dex);
XXFC_API void xx_dex_free(xx_dex *dex);

XXFC_API bool xx_dex_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dex_handle_base_info(Abstractformat *self,
                                      xx_pd_struct *pd);
XXFC_API int64_t xx_dex_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_dex_get_memory_map(Abstractformat *self,
                                    xx_memory_map_mode_t mode,
                                    xx_memory_map *output,
                                    xx_pd_struct *pd);

XXFC_API const xx_dex_header *xx_dex_get_header(const xx_dex *dex);
XXFC_API uint32_t xx_dex_get_version_number(const xx_dex *dex);
XXFC_API bool xx_dex_is_big_endian(const xx_dex *dex);
XXFC_API const char *xx_dex_map_item_type_to_string(uint16_t type);

static inline Abstractformat *xx_dex_to_format(xx_dex *dex) {
    return dex ? &dex->format : NULL;
}

static inline const Abstractformat *xx_dex_to_format_const(
    const xx_dex *dex) {
    return dex ? &dex->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DEX_H */
