/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_wasp_windows_auto.h
 *  @brief WASP (Windows Auto Setup Package, Cerious Software 1994) reader. */

#ifndef XXFCLIB_FORMAT_SFX_WASP_WINDOWS_AUTO_H
#define XXFCLIB_FORMAT_SFX_WASP_WINDOWS_AUTO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A WASP self-extracting setup: the Win16 NE stub WASP.EXE ("Wasp
 * v1.0 (c)1994 Cerious Software", module name WASP) with the files to
 * install added to its own resources.
 *
 * Everything is found through the NE resource table (at e_lfanew + the u16
 * at NE+0x24, running to the resident-name table at NE+0x26):
 *
 *   u16 alignment shift, then type blocks until a zero type id:
 *     u16 type id (bit 15 set: integer; clear: offset of a length-prefixed
 *         name from the start of the table), u16 count, u32 reserved,
 *     count x { u16 offset, u16 length (both in alignment units, from the
 *               start of the file), u16 flags, u16 id, u32 reserved }
 *
 * Member n (n = 1, 2, ...) is described by string n of the RT_STRING table
 * (block n / 16 + 1, entry n % 16; each block holds 16 strings of a length
 * byte and that many characters):
 *
 *     "<NAME>,<decimal size>" optionally followed by '*'
 *
 * and its bytes are the first <size> bytes of the resource of the named
 * type "FILE" whose integer id is n.  The '*' marks the program the stub
 * runs once everything is copied to its temporary directory.  The files are
 * stored as they are to be written: an MS Setup package keeps its KWAJ
 * compressed ".XX_" files compressed, and its own SETUP.EXE expands them.
 *
 * The stub looks members up by number (its own message reads "Unable to
 * load information for file %d"), so the member list ends at the first id
 * without a well-formed string or without its FILE resource.  The first
 * member must also lie wholly inside the file.
 */
typedef struct xx_sfx_wasp_windows_auto {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t total_unpacked;   /**< Sum of the declared member sizes. */
    uint32_t ne_offset;        /**< e_lfanew, relative to the base address. */
    uint32_t alignment_shift;  /**< Resource table alignment shift. */
    uint32_t file_resources;   /**< Resources of type "FILE". */
    int32_t run_index;         /**< Record marked '*', or -1. */
} xx_sfx_wasp_windows_auto;

typedef xx_sfx_wasp_windows_auto xx_sfx_wasp_windows_auto_t;

XXFC_API void xx_sfx_wasp_windows_auto_init(xx_sfx_wasp_windows_auto *archive,
                                            xx_io_device *device,
                                            int64_t base_address);
XXFC_API xx_sfx_wasp_windows_auto *xx_sfx_wasp_windows_auto_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_wasp_windows_auto_destroy(
    xx_sfx_wasp_windows_auto *archive);
XXFC_API void xx_sfx_wasp_windows_auto_free(xx_sfx_wasp_windows_auto *archive);

XXFC_API bool xx_sfx_wasp_windows_auto_check_is_valid(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API bool xx_sfx_wasp_windows_auto_handle_base_info(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_wasp_windows_auto_get_format_size(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_wasp_windows_auto_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_wasp_windows_auto_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_wasp_windows_auto_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_wasp_windows_auto_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_wasp_windows_auto_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_wasp_windows_auto_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_WASP_WINDOWS_AUTO_H */
