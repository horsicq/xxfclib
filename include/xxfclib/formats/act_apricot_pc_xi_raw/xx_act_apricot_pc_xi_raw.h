/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_act_apricot_pc_xi_raw.h
 * @brief ACT Apricot PC / Xi raw floppy image (MS-DOS FAT12 volume that
 *        boots from an Apricot disk label instead of a PC boot sector).
 *
 * The image is a plain sector dump: cylinder by cylinder, the heads of a
 * cylinder interleaved, sector 1 first, so file offset = LBA * 512.  The
 * usual sizes are 315K (70 cylinders, 1 head, 9 sectors) and 720K
 * (80 cylinders, 2 heads, 9 sectors).
 *
 * Sector 0 is the Apricot disk label, NOT a PC boot sector: there is no jump
 * at +0, no BPB at +0x0B and usually no 0x55AA.  The fields this reader
 * relies on (little endian):
 *
 *   +0x00  char[8]  version / OEM text ("VR 1.3  ", "VR 1.5.1") or zeros
 *   +0x0E  u16      sector size, 512
 *   +0x10  u16      sectors per track
 *   +0x12  u32      cylinders
 *   +0x16  u8       heads
 *   +0x50  13-byte MS-DOS 2.0 BPB:
 *          +0x50 u16 bytes per sector (512)   +0x52 u8  sectors per cluster
 *          +0x53 u16 reserved sectors         +0x55 u8  number of FATs
 *          +0x56 u16 root directory entries   +0x58 u16 total sectors
 *          +0x5A u8  media descriptor         +0x5B u16 sectors per FAT
 *
 * The rest of the disk is ordinary FAT12: FAT copies after the reserved
 * sectors, the fixed root directory after them, then the data clusters.
 * Members are the files and directories of that volume.
 */

#ifndef XXFCLIB_FORMAT_ACT_APRICOT_PC_XI_RAW_H
#define XXFCLIB_FORMAT_ACT_APRICOT_PC_XI_RAW_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_act_apricot_pc_xi_raw {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t cylinders;
    uint32_t heads;
    uint32_t sectors_per_track;
    uint32_t total_sectors;     /**< BPB total, equal to the label geometry. */
    uint32_t bytes_per_cluster;
    uint32_t cluster_count;     /**< Data clusters; FAT12, so below 4085. */
    uint8_t media;
    char label[9];              /**< Label bytes 0..7, NUL terminated. */
} xx_act_apricot_pc_xi_raw;

typedef xx_act_apricot_pc_xi_raw xx_act_apricot_pc_xi_raw_t;

XXFC_API void xx_act_apricot_pc_xi_raw_init(xx_act_apricot_pc_xi_raw *image,
                                            xx_io_device *device,
                                            int64_t base_address);
XXFC_API xx_act_apricot_pc_xi_raw *xx_act_apricot_pc_xi_raw_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_act_apricot_pc_xi_raw_destroy(xx_act_apricot_pc_xi_raw *image);
XXFC_API void xx_act_apricot_pc_xi_raw_free(xx_act_apricot_pc_xi_raw *image);

XXFC_API bool xx_act_apricot_pc_xi_raw_check_is_valid(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API bool xx_act_apricot_pc_xi_raw_handle_base_info(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API int64_t xx_act_apricot_pc_xi_raw_get_format_size(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_act_apricot_pc_xi_raw_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_act_apricot_pc_xi_raw_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_act_apricot_pc_xi_raw_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_act_apricot_pc_xi_raw_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_act_apricot_pc_xi_raw_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_act_apricot_pc_xi_raw_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ACT_APRICOT_PC_XI_RAW_H */
