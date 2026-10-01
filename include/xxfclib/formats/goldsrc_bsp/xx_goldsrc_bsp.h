/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_goldsrc_bsp.h @brief GoldSrc (Half-Life) BSP v30 texture reader. */

#ifndef XXFCLIB_FORMAT_GOLDSRC_BSP_H
#define XXFCLIB_FORMAT_GOLDSRC_BSP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A GoldSrc engine map (Half-Life and its mods), BSP version 30.
 *
 *   0x00  i32 LE  version, 30
 *   0x04  15 x { i32 offset, i32 length }  lump directory
 *            0 entities   1 planes (20)   2 textures   3 vertexes (12)
 *            4 visibility 5 nodes (24)    6 texinfo (40) 7 faces (20)
 *            8 lighting   9 clipnodes (8) 10 leafs (28) 11 marksurfaces (2)
 *            12 edges (4) 13 surfedges (4) 14 models (64)
 *          (record sizes in brackets; Blue Shift maps swap lumps 0 and 1)
 *
 * Texture lump: i32 count, i32 offset[count] (relative to the lump, -1 for
 * an unused slot), then miptex records:
 *   char name[16]; u32 width, height; u32 mip_offset[4]  (relative to the
 *   miptex; all zero when the texture lives in an external WAD)
 *   four mip levels of 8-bit pixels, u16 palette count, count x RGB.
 *
 * Members: every embedded miptex, byte-exact from its header to the end of
 * its palette, as "<name>.mip"; and the entity lump, without its trailing
 * NUL, as "entities.ent".  Texture names are reduced to a safe file name
 * and made unique.
 */
typedef struct xx_goldsrc_bsp {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t number_of_textures;   /**< Slots in the texture lump. */
    uint32_t embedded_textures;    /**< Slots carrying pixel data. */
    uint32_t skipped_textures;     /**< Slots whose miptex was malformed. */
} xx_goldsrc_bsp;

typedef xx_goldsrc_bsp xx_goldsrc_bsp_t;

XXFC_API void xx_goldsrc_bsp_init(xx_goldsrc_bsp *archive, xx_io_device *device,
                                  int64_t base_address);
XXFC_API xx_goldsrc_bsp *xx_goldsrc_bsp_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_goldsrc_bsp_destroy(xx_goldsrc_bsp *archive);
XXFC_API void xx_goldsrc_bsp_free(xx_goldsrc_bsp *archive);

XXFC_API bool xx_goldsrc_bsp_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_goldsrc_bsp_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_goldsrc_bsp_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_goldsrc_bsp_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_goldsrc_bsp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_goldsrc_bsp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_goldsrc_bsp_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_goldsrc_bsp_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_goldsrc_bsp_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GOLDSRC_BSP_H */
