/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfxstart.h
 *  @brief "SFXSTART" self-extracting executable (stored setup-kit container). */

#ifndef XXFCLIB_FORMAT_SFXSTART_H
#define XXFCLIB_FORMAT_SFXSTART_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A "SFXSTART" self-extracting executable.
 *
 * A small MSVC 2.0 PE32 stub ("Self-extracting EXE", "sfx.dat") that copies a
 * set-up kit into the temporary directory, expands the SZDD members through
 * LZ32.DLL and runs a command.  Its payload is a private, uncompressed record
 * chain appended to the image as the PE overlay.  All integers are
 * little-endian; every offset below is relative to the start of the
 * executable.
 *
 * Header, at the overlay (0x4a00 behind every stub seen so far):
 *
 *   0x00  char[8]  "SFXSTART"
 *   0x08  u32      payload size   bytes after this 16-byte header, up to and
 *                                 including the trailer
 *   0x0c  u32      record count - 1
 *
 * Then record count records, each:
 *
 *   char     '*'
 *   u32      name length          1..255
 *   char[]   name                 ANSI, not NUL terminated
 *   char[2]  "->"
 *   u32      data size
 *   byte[]   data                 stored as is (SZDD .ex_/.dl_ files, plain
 *                                 executables, MS CAB, text)
 *   byte[3]  "->" 0xF0            separator, after the last record too
 *
 * Then the trailer, whose last four bytes point back at the header:
 *
 *   char     '+'
 *   u32      command length
 *   char[]   command              the program started after extraction
 *   char     '-'
 *   u32      offset of "SFXSTART"
 *
 * Locating the payload: the header is expected where the PE image ends (the
 * furthest end of SizeOfHeaders and every section's raw data).  When it is
 * not there, the offset stored in the file's last four bytes is tried, and
 * that route is accepted only with a complete trailer ending on the last
 * byte.  Only the MZ header, the PE headers, the section table and the
 * payload are read; nothing in the stub is executed or emulated.
 *
 * Members are listed and extracted exactly as stored: an SZDD member stays
 * compressed (the MS COMPRESS reader opens it), as it does in the kit.
 */
typedef struct xx_sfxstart {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset;  /**< "SFXSTART", relative to base_address. */
    int64_t payload_end;     /**< End of the declared payload, relative. */
    int64_t trailer_offset;  /**< The '+' trailer, relative; -1 if none. */
    uint64_t stored_total;   /**< Sum of the members' data sizes. */
    uint32_t locator;        /**< XX_SFXSTART_LOCATOR_*. */
    char run_command[256];   /**< Trailer command, raw ANSI; "" if none. */
    void *table;             /**< Member table cached by handle_base_info. */
} xx_sfxstart;

typedef xx_sfxstart xx_sfxstart_t;

#define XX_SFXSTART_TAG "SFXSTART"
#define XX_SFXSTART_TAG_SIZE 8
#define XX_SFXSTART_HEADER_SIZE 16
#define XX_SFXSTART_MAX_NAME 255
/** Largest record count accepted (the header stores count - 1). */
#define XX_SFXSTART_MAX_RECORDS 65536U

#define XX_SFXSTART_LOCATOR_NONE 0U
#define XX_SFXSTART_LOCATOR_PE_OVERLAY 1U
#define XX_SFXSTART_LOCATOR_TRAILER 2U

/** Compression method published in XX_META_ID_COMPRESSION_METHOD. */
#define XX_SFXSTART_METHOD_STORED 0U

XXFC_API void xx_sfxstart_init(xx_sfxstart *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_sfxstart *xx_sfxstart_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_sfxstart_destroy(xx_sfxstart *archive);
XXFC_API void xx_sfxstart_free(xx_sfxstart *archive);

XXFC_API bool xx_sfxstart_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_sfxstart_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_sfxstart_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_sfxstart_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sfxstart_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfxstart_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfxstart_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfxstart_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfxstart_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFXSTART_H */
