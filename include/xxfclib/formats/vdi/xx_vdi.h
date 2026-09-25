/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vdi.h @brief VirtualBox VDI disk image (dynamic, fixed, diff). */

#ifndef XXFCLIB_FORMAT_VDI_H
#define XXFCLIB_FORMAT_VDI_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A VirtualBox VDI image: a 72-byte pre-header (text banner, signature
 * 0xBEDA107F at 0x40, version 1.x), a version-1 header, a block map of u32
 * slots and the data blocks.  The guest disk is published as ONE member,
 * "disk.img", rebuilt through the block map. */
typedef struct xx_vdi {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;      /**< base_address + format size, or -1. */
    uint64_t disk_size;       /**< Guest disk size in bytes. */
    uint32_t image_type;      /**< 1 dynamic, 2 fixed, 3 undo, 4 diff. */
    uint32_t block_size;      /**< Bytes per block (VirtualBox: 1 MiB). */
    uint32_t blocks;          /**< Block map entries. */
    uint32_t blocks_allocated;/**< Data blocks stored in the file. */
    uint32_t version;         /**< 0x00010001 for version 1.1. */
} xx_vdi;

typedef struct xx_vdi xx_vdi_t;

XXFC_API void xx_vdi_init(xx_vdi *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_vdi *xx_vdi_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vdi_destroy(xx_vdi *archive);
XXFC_API void xx_vdi_free(xx_vdi *archive);
XXFC_API bool xx_vdi_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vdi_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vdi_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_vdi_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vdi_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vdi_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vdi_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vdi_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vdi_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vdi_to_format(xx_vdi *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VDI_H */
