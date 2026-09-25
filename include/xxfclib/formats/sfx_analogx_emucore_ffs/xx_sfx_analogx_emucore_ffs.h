/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_sfx_analogx_emucore_ffs.h
 * @brief AnalogX installers: a Watcom PE stub carrying an EmuCore "FFS"
 *        (Flat/Fast File System) payload.
 *
 * AnalogX (1998-2003) shipped its freeware as self-installing executables
 * built on the company's "EmuCore" library.  The stub reads an FFS image
 * that is appended to it; the same image can also stand alone ("flat
 * file").  Integers are little-endian.
 *
 *   FFS header (16 bytes)
 *     +0   "FFS!"
 *     +4   u32 count          number of files, > 0
 *     +8   u32 ~count         bitwise complement of count
 *     +12  u32 flags          bit 0 = sequential-read shortcut; 6 in every
 *                             known image
 *   table: count x { u32 hash, u32 offset }, sorted by hash ascending
 *     hash    CRC-32 (ISO-HDLC, as zlib) of the upper-cased file name
 *     offset  where the file's record starts, counted from the start of
 *             the file the image was built into (the stub executable)
 *   records: { u32 hash (repeated), u32 length, length payload bytes }
 *     payload "FFC@" u32 ~size  then an ffs-cmp2 stream (below)
 *     payload "FFC!" u32 size   then an ffs-cmp1 stream (below)
 *     anything else             the file itself, stored
 *   trailer (appended to the stub only): u32 offset of "FFS!"
 *
 * The first record follows the table directly, so the offset base is the
 * table end minus the smallest offset; that makes a carved image (offsets
 * still relative to the executable) as readable as a flat one.
 *
 * ffs-cmp2 is Okumura/Yoshizaki LZHUF with one change: THRESHOLD 3, so the
 * alphabet has 313 symbols (256 literals and match lengths 4..60).  The
 * ring is 4096 bytes, its first 4036 preset to ' ' and the rest to zero,
 * and the write cursor starts at 4036.  The stream stops after `size`
 * bytes; there is no end symbol.
 *
 * ffs-cmp1 is a byte-oriented opcode stream (low nibble of each opcode
 * byte): 0 literal run, 1 fill (length 0 ends the stream), 2/3 copy with an
 * 8/16-bit distance, 4 constant delta, 5 multi-channel delta, 6..9 16-bit
 * word +/- small delta (both byte orders), 10 single byte from up to 16
 * back.  No known installer uses it; it is decoded as the stub does.
 *
 * File names are not stored.  They are recovered by hashing candidate
 * strings: the tokens of the installer script INSTALL.DAT (itself one of
 * the files) and the NUL-terminated strings of the stub.  A file whose
 * name is not found is listed as "<HASH>.bin".
 */

#ifndef XXFCLIB_FORMAT_SFX_ANALOGX_EMUCORE_FFS_H
#define XXFCLIB_FORMAT_SFX_ANALOGX_EMUCORE_FFS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Record payload encodings. */
#define XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_STORED 0U
#define XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP1 1U
#define XX_SFX_ANALOGX_EMUCORE_FFS_METHOD_CMP2 2U

typedef struct xx_sfx_analogx_emucore_ffs {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t ffs_offset;      /**< "FFS!", relative to the base. */
    int64_t offset_origin;   /**< Value of a table offset that means "FFS!". */
    uint32_t flags;          /**< Header dword at +12. */
    bool in_executable;      /**< Found through the trailer of an MZ image. */
} xx_sfx_analogx_emucore_ffs;

typedef xx_sfx_analogx_emucore_ffs xx_sfx_analogx_emucore_ffs_t;

XXFC_API void xx_sfx_analogx_emucore_ffs_init(xx_sfx_analogx_emucore_ffs *archive,
                                              xx_io_device *device,
                                              int64_t base_address);
XXFC_API xx_sfx_analogx_emucore_ffs *
xx_sfx_analogx_emucore_ffs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_analogx_emucore_ffs_destroy(xx_sfx_analogx_emucore_ffs *archive);
XXFC_API void xx_sfx_analogx_emucore_ffs_free(xx_sfx_analogx_emucore_ffs *archive);

XXFC_API bool xx_sfx_analogx_emucore_ffs_check_is_valid(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_sfx_analogx_emucore_ffs_handle_base_info(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_analogx_emucore_ffs_get_format_size(Abstractformat *self,
                                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_analogx_emucore_ffs_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_analogx_emucore_ffs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_analogx_emucore_ffs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_analogx_emucore_ffs_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_analogx_emucore_ffs_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_analogx_emucore_ffs_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Decode one record payload held in memory ("FFC@", "FFC!" or
 *        stored) into @p output, which must hold exactly @p output_size.
 *
 * @return true when the payload decodes to exactly @p output_size bytes.
 */
XXFC_API bool xx_sfx_analogx_emucore_ffs_decode_memory(const uint8_t *payload,
                                                       size_t payload_size,
                                                       uint8_t *output,
                                                       size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_ANALOGX_EMUCORE_FFS_H */
