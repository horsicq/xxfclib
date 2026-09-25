/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_setup_factory.h @brief Setup Factory 5/6 installer reader. */

#ifndef XXFCLIB_FORMAT_SETUP_FACTORY_H
#define XXFCLIB_FORMAT_SETUP_FACTORY_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Indigo Rose Setup Factory 5/6 installer.
 *
 * The installer is an ordinary Win32 PE (the "irsetup" stub). Its payload
 * starts at the PE overlay, the first byte behind the last section's raw
 * data. The executable itself is only parsed far enough to find that
 * offset; no code is run or emulated.
 *
 * Overlay (all integers little-endian):
 *
 *   +0   u8[8]  E0 E1 E2 E3 E4 E5 E6 E7
 *   +8   u32    engine file count, 1..99
 *   +12  engine chain, count times:
 *          char[N] name, NUL terminated inside the field, zero padded
 *                  (N = 260 in the long layout, 16 in the short one)
 *          u32     packed size
 *          u32     CRC-32 of the unpacked file
 *          u8[]    the file, a PKWARE DCL ("implode") stream
 *   then the payload streams, back to back, in manifest order
 *
 * One engine file, irsetup.dat, is the manifest: an MFC CArchive holding a
 * u16 count and that many serialized CFileInfo objects. Every object gives
 * the member's name, destination folder ("%AppDir%\\..."), unpacked size,
 * packed size, CRC-32 and a method byte (0 or 'r': DCL; 1 or 'n': stored).
 * Setup Factory 6 writes the long layout; the short layout (16-byte engine
 * names and an older CFileInfo schema) is the variant U3 accepts next to it.
 *
 * Members are published as "<destination>/<name>" with "%AppDir%" dropped
 * and any other leading "%Var%" kept as a folder named Var. Only the payload
 * is listed; the engine files (irsetup.exe, the manifest, language and
 * bitmap files) are not.
 */
typedef struct xx_setup_factory {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t overlay_offset;  /**< Relative to the format's base address. */
    int64_t payload_offset;  /**< First payload byte, same origin. */
    int64_t payload_end;     /**< End of the last member present, same origin. */
    uint32_t engine_count;
    uint32_t layout;         /**< XX_SETUP_FACTORY_LAYOUT_* */
    uint32_t schema;         /**< CArchive schema number of CFileInfo. */
    bool truncated;          /**< The payload runs past the end of the file. */
} xx_setup_factory;

typedef xx_setup_factory xx_setup_factory_t;

/** Engine records with 16-byte names (older CFileInfo schema). */
#define XX_SETUP_FACTORY_LAYOUT_SHORT 2U
/** Engine records with 260-byte names (Setup Factory 6). */
#define XX_SETUP_FACTORY_LAYOUT_LONG 3U

/** XX_META_ID_COMPRESSION_METHOD values. */
#define XX_SETUP_FACTORY_METHOD_STORED 0U
#define XX_SETUP_FACTORY_METHOD_DCL 1U
#define XX_SETUP_FACTORY_METHOD_UNKNOWN 0xFFU

XXFC_API void xx_setup_factory_init(xx_setup_factory *archive,
                                    xx_io_device *device,
                                    int64_t base_address);
XXFC_API xx_setup_factory *xx_setup_factory_create(xx_io_device *device,
                                                   int64_t base_address);
XXFC_API void xx_setup_factory_destroy(xx_setup_factory *archive);
XXFC_API void xx_setup_factory_free(xx_setup_factory *archive);

XXFC_API bool xx_setup_factory_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_setup_factory_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_setup_factory_get_format_size(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API uint64_t xx_setup_factory_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_setup_factory_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_setup_factory_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_setup_factory_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_setup_factory_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_setup_factory_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SETUP_FACTORY_H */
