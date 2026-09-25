/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installer_vise_windows.h
 *  @brief Installer VISE for Windows (MindVision) self-installing executable. */

#ifndef XXFCLIB_FORMAT_INSTALLER_VISE_WINDOWS_H
#define XXFCLIB_FORMAT_INSTALLER_VISE_WINDOWS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Installer VISE for Windows package.
 *
 * The carrier is a Win32 PE stub (it exports ViseMain).  Its payload is a
 * private container that opens with "ESIV" and is found in one of two places:
 *
 *   - at the PE overlay (the first byte after the last section), or
 *   - at the start of a PE section (named _mvdata) behind an 8-byte wrapper
 *     "SIVM" + u32 size, where the size runs from the wrapper to the footer.
 *
 * An 8-byte footer "ESIV" + u32 closes the container; its u32 is the file
 * offset of the header (or of the SIVM wrapper).  A signed package carries
 * its Authenticode block after the footer.
 *
 * Container layout (offsets from the "ESIV" header):
 *
 *   +0x00 "ESIV"  +0x04 u32 build time  +0x08 u32 1  +0x0c u32 variant
 *   +0x10 str8 (u8 length + bytes), u8 n, n & 0x3f bytes, str8
 *         u16 count, then count setup files, each
 *             str8 name, u16 DOS date, u16 DOS time, 8 bytes, u32 packed,
 *             packed bytes
 *   language block (u8, u8, u16, str16 x2, u8, u8, str16 x2,
 *         u16 n, n x (u16, str16, str16)) then u32 packed + the packed
 *         "ESIVCSIM" settings stream
 *   installer script: variables, a second table of setup files laid out as
 *         above, and the install objects.  An install-file object carries
 *             u16 name length, name, 4 or 6 reserved bytes,
 *             u32 unpacked size, u32 packed size, u32 flags (1),
 *             u32 offset of the packed data from the "ESIV" header
 *   data area: the packed install files, back to back, up to the footer
 *
 * Every packed stream is raw Deflate written through a 16-bit word writer:
 * each pair of bytes is swapped, a stored block is aligned to (and padded
 * to) a 16-bit boundary, and the stream is padded to an even length.  A
 * stream that ends with a stored block carries one extra zero word.
 *
 * The script layout differs between VISE releases, so only the header, the
 * first table and the language block are walked; the second table and the
 * install objects are located by their fixed fields and each one is accepted
 * only after its whole stream decodes (to the declared size, where there is
 * one) and ends exactly at its packed size.  The stub is never run.
 */
typedef struct xx_installer_vise_windows {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t header_offset;   /**< "ESIV" header, from the base address. */
    int64_t wrapper_offset;  /**< "SIVM" wrapper, or -1. */
    int64_t footer_offset;   /**< "ESIV" footer, or -1 when absent. */
    int64_t container_end;   /**< End of the packed data (the footer). */
    void *parsed;            /**< Internal member table cache. */
} xx_installer_vise_windows;

typedef xx_installer_vise_windows xx_installer_vise_windows_t;

XXFC_API void xx_installer_vise_windows_init(xx_installer_vise_windows *archive,
                                             xx_io_device *device,
                                             int64_t base_address);
XXFC_API xx_installer_vise_windows *xx_installer_vise_windows_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installer_vise_windows_destroy(
    xx_installer_vise_windows *archive);
XXFC_API void xx_installer_vise_windows_free(xx_installer_vise_windows *archive);

XXFC_API bool xx_installer_vise_windows_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_installer_vise_windows_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API int64_t xx_installer_vise_windows_get_format_size(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_installer_vise_windows_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installer_vise_windows_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installer_vise_windows_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installer_vise_windows_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installer_vise_windows_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installer_vise_windows_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one VISE packed stream (byte-swapped, word-aligned Deflate).
 *
 * @param packed       the stream as stored in the file (even length)
 * @param packed_size  its length
 * @param output       receives the decoded bytes (may be NULL to verify)
 * @param output_size  capacity of @p output (the decode fails beyond it)
 * @param written      optional; bytes produced
 * @return true when the stream ends exactly at @p packed_size
 */
XXFC_API bool xx_installer_vise_windows_decode_memory(const uint8_t *packed,
                                                      size_t packed_size,
                                                      uint8_t *output,
                                                      size_t output_size,
                                                      size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLER_VISE_WINDOWS_H */
