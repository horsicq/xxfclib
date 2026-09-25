/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_inno_setup.h @brief Inno Setup installer (1.09 .. 7.x) reader. */

#ifndef XXFCLIB_FORMAT_INNO_SETUP_H
#define XXFCLIB_FORMAT_INNO_SETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Inno Setup installer: a small "Setup Loader" executable that
 * carries the compressed setup program, the setup data ("setup-0") and the
 * file data ("setup-1").
 *
 * The loader is parsed only as far as needed to find its offset table; no
 * code is run or emulated.  The table ("rDlPtS" + a 6-byte generation tag)
 * is found in one of these places:
 *
 *   1.09          "rDlPtSVx\x87eVx" inside the loader's DATA section
 *   1.11          "rDlPtS02\x87eVx" in the last 40 bytes of the file
 *   1.2.10-5.1.4  "Inno" at 0x30, then a pointer and its complement; the
 *                 pointer names the table (tags 02, 04, 05, 06, 07)
 *   5.1.5-7.x     RCDATA resource 11111, tag CD E6 D7 7B 0B 2A, with a
 *                 table revision (1: 32-bit fields, 2: 64-bit, 6.5+) and
 *                 a CRC-32
 *
 * The table gives the offset of setup-0 and of the embedded file data (0
 * when the data lives in external "setup-1.bin" slices).  Setup-0 starts
 * with a version ID (8 bytes on 1.09/1.11, 12 on 1.2.10, otherwise the
 * zero-padded 64-byte "Inno Setup Setup Data (x.y.z)"), followed by CRC
 * framed blocks: the setup header and entry arrays (zlib before 4.1.6,
 * LZMA afterwards), then the file location table.  1.09/1.11 use a record
 * per block with Adler-32 framing instead.  File data is a run of chunks,
 * each "zlb\x1a" followed by a zlib, bzip2, LZMA, LZMA2 or stored stream;
 * from 4.0.1 a chunk may hold several files (solid compression), and from
 * 4.1.8 executables may be passed through a CALL/JMP address filter.
 *
 * One record is produced per [Files] entry that has embedded data and a
 * destination name ("{app}/dir/name.ext").  Encrypted chunks and external
 * slices are listed but cannot be unpacked.
 */
typedef struct xx_inno_setup {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;      /**< (major << 24) | (minor << 16) | (patch << 8) | rev */
    bool unicode;
    bool isx;
    bool external_data;    /**< File data lives in setup-1 slices. */
    char version_text[72]; /**< The setup-data ID's version, e.g. "3.0.5" */
} xx_inno_setup;

typedef xx_inno_setup xx_inno_setup_t;

XXFC_API void xx_inno_setup_init(xx_inno_setup *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_inno_setup *xx_inno_setup_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_inno_setup_destroy(xx_inno_setup *archive);
XXFC_API void xx_inno_setup_free(xx_inno_setup *archive);

XXFC_API bool xx_inno_setup_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_inno_setup_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_inno_setup_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_inno_setup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_inno_setup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_inno_setup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_inno_setup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_inno_setup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_inno_setup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INNO_SETUP_H */
