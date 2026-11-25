/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qnx_lmf.h @brief QNX Load Module Format executable reader. */

#ifndef XXFCLIB_FORMAT_QNX_LMF_H
#define XXFCLIB_FORMAT_QNX_LMF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_QNX_LMF_RECORD_HEADER_SIZE UINT32_C(6)
#define XX_QNX_LMF_HEADER_SIZE UINT32_C(48)
#define XX_QNX_LMF_DATA_HEADER_SIZE UINT32_C(6)
#define XX_QNX_LMF_RESOURCE_HEADER_SIZE UINT32_C(8)
#define XX_QNX_LMF_MAX_RECORD_DATA_SIZE UINT32_C(0x7e00)
#define XX_QNX_LMF_MAX_RECORDS UINT32_C(65536)

typedef enum xx_qnx_lmf_record_type_e {
    XX_QNX_LMF_RECORD_HEADER = 0,
    XX_QNX_LMF_RECORD_COMMENT = 1,
    XX_QNX_LMF_RECORD_LOAD = 2,
    XX_QNX_LMF_RECORD_FIXUP = 3,
    XX_QNX_LMF_RECORD_8087_FIXUP = 4,
    XX_QNX_LMF_RECORD_IMAGE_END = 5,
    XX_QNX_LMF_RECORD_RESOURCE = 6,
    XX_QNX_LMF_RECORD_READ_WRITE_END = 7,
    XX_QNX_LMF_RECORD_LINEAR_FIXUP = 8
} xx_qnx_lmf_record_type;

typedef struct xx_qnx_lmf_header {
    uint16_t version;
    uint16_t cflags;
    uint16_t cpu;
    uint16_t fpu;
    uint16_t code_index;
    uint16_t stack_index;
    uint16_t heap_index;
    uint16_t argv_index;
    uint16_t spare2[4];
    uint32_t code_offset;
    uint32_t stack_nbytes;
    uint32_t heap_nbytes;
    uint32_t image_base;
    uint32_t spare3[2];
} xx_qnx_lmf_header;

typedef struct xx_qnx_lmf_record {
    xx_qnx_lmf_record_type type;
    int64_t file_offset;
    int64_t data_offset;
    uint16_t data_size;
} xx_qnx_lmf_record;

typedef struct xx_qnx_lmf_section {
    uint16_t segment;
    uint32_t load_offset;
    int64_t record_offset;
    int64_t raw_data_offset;
    int64_t payload_offset;
    uint32_t size;
} xx_qnx_lmf_section;

typedef struct xx_qnx_lmf_resource {
    uint16_t type;
    int64_t record_offset;
    int64_t data_offset;
    uint32_t size;
} xx_qnx_lmf_resource;

typedef struct xx_qnx_lmf_fixup {
    xx_qnx_lmf_record_type type;
    uint16_t segment;
    uint32_t offset;
    int64_t record_offset;
} xx_qnx_lmf_fixup;

typedef struct xx_qnx_lmf {
    Abstractformat format;
    xx_qnx_lmf_header header;
    xx_qnx_lmf_record *records;
    xx_qnx_lmf_section *sections;
    xx_qnx_lmf_resource *resources;
    xx_qnx_lmf_fixup *fixups;
    uint32_t record_count;
    uint32_t section_count;
    uint32_t resource_count;
    uint32_t fixup_count;
    uint16_t verification;
    uint32_t signature;
    bool has_read_write_end;
} xx_qnx_lmf;

XXFC_API void xx_qnx_lmf_init(xx_qnx_lmf *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_qnx_lmf *xx_qnx_lmf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_qnx_lmf_destroy(xx_qnx_lmf *reader);
XXFC_API void xx_qnx_lmf_free(xx_qnx_lmf *reader);

XXFC_API bool xx_qnx_lmf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qnx_lmf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qnx_lmf_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_qnx_lmf_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_qnx_lmf_header *xx_qnx_lmf_get_header(xx_qnx_lmf *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_qnx_lmf_get_number_of_records(xx_qnx_lmf *reader, xx_pd_struct *pd);
XXFC_API const xx_qnx_lmf_record *xx_qnx_lmf_get_record(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API uint32_t xx_qnx_lmf_get_number_of_sections(xx_qnx_lmf *reader, xx_pd_struct *pd);
XXFC_API const xx_qnx_lmf_section *xx_qnx_lmf_get_section(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API bool xx_qnx_lmf_read_section(xx_qnx_lmf *reader, uint32_t index, void *buffer, size_t size, xx_pd_struct *pd);
XXFC_API uint32_t xx_qnx_lmf_get_number_of_resources(xx_qnx_lmf *reader, xx_pd_struct *pd);
XXFC_API const xx_qnx_lmf_resource *xx_qnx_lmf_get_resource(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API bool xx_qnx_lmf_read_resource(xx_qnx_lmf *reader, uint32_t index, void *buffer, size_t size, xx_pd_struct *pd);
XXFC_API uint32_t xx_qnx_lmf_get_number_of_fixups(xx_qnx_lmf *reader, xx_pd_struct *pd);
XXFC_API const xx_qnx_lmf_fixup *xx_qnx_lmf_get_fixup(xx_qnx_lmf *reader, uint32_t index, xx_pd_struct *pd);

static inline Abstractformat *xx_qnx_lmf_to_format(xx_qnx_lmf *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QNX_LMF_H */
