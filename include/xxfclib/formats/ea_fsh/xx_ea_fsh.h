/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ea_fsh.h @brief Electronic Arts FSH / SHPI image pack reader. */

#ifndef XXFCLIB_FORMAT_EA_FSH_H
#define XXFCLIB_FORMAT_EA_FSH_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Electronic Arts "SHPI" shape pack (.fsh; the .qfs variant is the
 *        same file wrapped in RefPack, which the earefpack reader removes).
 *
 *   0x00  char[4] "SHPI" (PC); "SHPP" / "SHPS" / "SHPX" on console ports
 *   0x04  u32 LE  total size of the pack, this header included
 *   0x08  u32 LE  number of directory entries
 *   0x0C  char[4] directory id ("GIMX", "G264", "G354", ...)
 *   0x10  entries, 8 bytes each:
 *           char[4] entry tag, NUL padded
 *           u32 LE  offset of the entry from the start of the pack
 *   then  optional "Buy ERTS" filler and alignment padding
 *   then  the entries: a 16-byte bitmap header (u8 record code, u24 link to
 *         the next attached block, u16 width, u16 height, 4 x u16 misc),
 *         pixel data and any attached blocks (palettes, text, hotspots).
 *
 * The directory has no sizes; an entry runs to the next higher entry offset
 * and the last one to the declared pack size. Each entry is published raw,
 * attachments and padding included, as "<tag>.<index as 6 digits>".
 */
typedef struct xx_ea_fsh {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t declared_size; /**< The u32 at 0x04. */
    bool truncated;        /**< Declared size runs past the device. */
} xx_ea_fsh;

typedef xx_ea_fsh xx_ea_fsh_t;

XXFC_API void xx_ea_fsh_init(xx_ea_fsh *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ea_fsh *xx_ea_fsh_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ea_fsh_destroy(xx_ea_fsh *archive);
XXFC_API void xx_ea_fsh_free(xx_ea_fsh *archive);

XXFC_API bool xx_ea_fsh_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ea_fsh_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ea_fsh_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ea_fsh_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ea_fsh_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ea_fsh_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ea_fsh_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ea_fsh_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ea_fsh_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EA_FSH_H */
