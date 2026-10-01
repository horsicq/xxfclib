/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_hsf.h @brief High Sierra (pre-ISO 9660) CD-ROM image reader.
 *
 * A High Sierra volume is laid out like ISO 9660 but with its own field
 * positions.  Offsets below are from the start of the image (the reader's
 * base address); the volume descriptors sit on 2048-byte sectors from 16 on.
 *
 * Volume descriptor (one 2048-byte sector each, sector 16 upwards):
 *   0x00  u32 LE + u32 BE  sector number of this descriptor
 *   0x08  u8               type: 1 = standard file structure (SFSVD),
 *                          255 = sequence terminator
 *   0x09  "CDROM"
 *   0x0E  u8               version, 1
 *   0x10  char[32]         system identifier
 *   0x30  char[32]         volume identifier
 *   0x58  u32 LE + u32 BE  volume space size (logical blocks)
 *   0x88  u16 LE + u16 BE  logical block size (512, 1024 or 2048)
 *   0xB4  34 bytes         root directory record
 *
 * Directory record (records never cross a logical block):
 *   0x00  u8               record length (0 = rest of the block is padding)
 *   0x01  u8               extended attribute record length (blocks)
 *   0x02  u32 LE + u32 BE  extent (logical block)
 *   0x0A  u32 LE + u32 BE  data length
 *   0x12  u8[6]            date: years since 1900, month, day, h, m, s
 *   0x18  u8               flags: 0x02 directory, 0x80 not the final extent
 *   0x1A  u8, u8           interleave unit size / gap size (blocks)
 *   0x1C  u16 LE + u16 BE  volume sequence number
 *   0x20  u8               identifier length, identifier at 0x21
 *
 * Member names drop a final ";1" and a '.' before it (other versions stay
 * in the name, as Deark and The Unarchiver name them), are joined
 * with '/', and a later member whose path equals an earlier one (ignoring
 * case) gets "~2", "~3", ... appended so extraction never overwrites.
 */

#ifndef XXFCLIB_FORMAT_HSF_H
#define XXFCLIB_FORMAT_HSF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_hsf xx_hsf;
typedef struct xx_hsf xx_hsf_t;

struct xx_hsf {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t logical_block_size;
    uint32_t volume_space_size;
    /** End of the volume as declared, which may lie past the device end. */
    int64_t volume_end;
    bool truncated;
    char volume_id[33];
    void *internal;
};

XXFC_API void xx_hsf_init(xx_hsf *hsf, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_hsf *xx_hsf_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_hsf_destroy(xx_hsf *hsf);
XXFC_API void xx_hsf_free(xx_hsf *hsf);

XXFC_API bool xx_hsf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_hsf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_hsf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_hsf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hsf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hsf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hsf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hsf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hsf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_hsf_to_format(xx_hsf *hsf) {
    return hsf ? &hsf->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HSF_H */
