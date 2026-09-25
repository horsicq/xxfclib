/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pc_install_setup.h @brief PC-Install self-extracting setup. */

#ifndef XXFCLIB_FORMAT_PC_INSTALL_SETUP_H
#define XXFCLIB_FORMAT_PC_INSTALL_SETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PC-Install setup program (16-bit NE or 32-bit PE engine) with
 * its "[20/20]" payload appended as the overlay.
 *
 *   overlay+0  char[8] "[20/20]\0"         head tag
 *   then a chain of records, each a 0x114-byte header and its payload:
 *     +0x00  u32 LE  absolute offset of the next record, 0 on the last
 *     +0x04  u32 LE  DOS attributes (low 16 bits)
 *     +0x08  u16 LE  DOS time          +0x0c u16 LE DOS date
 *     +0x10  u32 LE  payload size
 *     +0x14  char[256] staging name, NUL terminated
 *   EOF-16   char[8] "[20/20]\0", u32 LE first record, u32 LE last record
 *
 * A payload is either a file the engine uses verbatim (a .PIF, the scrambled
 * setup .CFG, a helper .EXE) or a member group:
 *     +0x00  14 zero bytes (cross-volume link; unused inside one file)
 *     +0x0e  u16 0x0074    +0x10 u16 member count    +0x12 u16 0x0074
 *     +0x3a  per member: a 0xa8-byte info block
 *              +0x00 char[128] installed name   +0x80 u32 attributes
 *              +0x84 u32 0    +0x88 u32 packed size
 *              +0x8c u16 DOS date (u32 slot)    +0x90 u16 DOS time (u32)
 *              +0x9c u32 unpacked size, 0 when not recorded
 *            followed by the member's raw PKWARE DCL implode stream.
 *
 * Group members are listed under their installed names and decoded; plain
 * records are listed under their staging names and copied.
 */
typedef struct xx_pc_install_setup {
    Abstractformat format;
    uint64_t number_of_records;   /**< Members listed (group members expanded). */
    uint64_t number_of_chunks;    /**< Records in the [20/20] chain. */
    int64_t head_offset;          /**< Head tag, relative to the base address. */
    int64_t trailer_offset;       /**< Trailer, relative to the base address. */
    bool relocated;               /**< The trailer's offsets were stale. */
} xx_pc_install_setup;

typedef xx_pc_install_setup xx_pc_install_setup_t;

/** Values published as XX_META_ID_COMPRESSION_METHOD. */
#define XX_PC_INSTALL_SETUP_METHOD_STORE 0U
#define XX_PC_INSTALL_SETUP_METHOD_DCL 1U

XXFC_API void xx_pc_install_setup_init(xx_pc_install_setup *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_pc_install_setup *xx_pc_install_setup_create(xx_io_device *device,
                                                         int64_t base_address);
XXFC_API void xx_pc_install_setup_destroy(xx_pc_install_setup *archive);
XXFC_API void xx_pc_install_setup_free(xx_pc_install_setup *archive);

XXFC_API bool xx_pc_install_setup_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_pc_install_setup_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_pc_install_setup_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_pc_install_setup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_pc_install_setup_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pc_install_setup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pc_install_setup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pc_install_setup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pc_install_setup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PC_INSTALL_SETUP_H */
