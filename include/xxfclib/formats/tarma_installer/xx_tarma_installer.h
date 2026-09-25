/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tarma_installer.h @brief Tarma Installer 5 pre-setup loader (tiz3). */

#ifndef XXFCLIB_FORMAT_TARMA_INSTALLER_H
#define XXFCLIB_FORMAT_TARMA_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Tarma Installer 5 / InstallMate "pre-setup loader" executable.
 *
 * The loader ("Tarma.Installer5.Loader", PE32 or PE64) carries its payload in
 * the PE overlay, i.e. right behind the raw data of the last section.  The
 * payload is a chain of sections, each one LZMA stream:
 *
 *   +0x00  u8[16]  opaque (differs per section)
 *   +0x10  char[4] "tiz3"
 *   +0x14  u16 LE  minor version (6), u16 LE major version (5)
 *   +0x18  u64     zero
 *   +0x20  u64 LE  section size, counting this header
 *   +0x28  u8[24]  zero, or a 16-byte id at +0x30 naming the database
 *   +0x40  u32 LE  A, u32 LE B with A ^ B == 0x35BC6F82
 *   +0x48  u8[5]   LZMA properties (lc/lp/pb byte, u32 LE dictionary size)
 *   +0x4D  LZMA range-coder stream, terminated by an end marker
 *
 * An optional 16-byte separator {u32 X, u32 ~X, u64 0} may sit between two
 * sections.  The decoded stream of a section is a run of blocks:
 *
 *   +0x00  char[4] "tzf3"
 *   +0x04  u32     zero
 *   +0x08  u64     block id
 *   +0x10  i64 LE  data size
 *   +0x18  FILETIME (creation)
 *   +0x20  FILETIME (last write)
 *   +0x28  u32     kind, then version-resource fields up to +0x40
 *   +0x40  the block's data
 *
 * Blocks carry no names; members are numbered "1", "2", ... across all
 * sections in stream order.  One block is the setup database ("tin5").
 */
typedef struct xx_tarma_installer {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;   /**< Sum of the listed blocks' data sizes. */
    uint32_t number_of_sections;
    int64_t payload_offset;   /**< First section, relative to the base. */
    int64_t payload_end;      /**< End of the last section, same origin. */
    bool truncated;           /**< The last section runs past the file end. */
    bool damaged;             /**< A section stopped decoding early. */
} xx_tarma_installer;

typedef xx_tarma_installer xx_tarma_installer_t;

/** XX_META_ID_COMPRESSION_METHOD value (the ZIP method number of LZMA). */
#define XX_TARMA_INSTALLER_METHOD_LZMA 14U

XXFC_API void xx_tarma_installer_init(xx_tarma_installer *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_tarma_installer *xx_tarma_installer_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_tarma_installer_destroy(xx_tarma_installer *archive);
XXFC_API void xx_tarma_installer_free(xx_tarma_installer *archive);

XXFC_API bool xx_tarma_installer_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_tarma_installer_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_tarma_installer_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_tarma_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_tarma_installer_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tarma_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tarma_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tarma_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tarma_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode the current record's data into @p destination.
 *
 * @p destination may be NULL to only verify the data.  Records are decoded
 * from one pass over each solid section; asking for a record again restarts
 * its section.
 */
XXFC_API bool xx_tarma_installer_unpack_current_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TARMA_INSTALLER_H */
