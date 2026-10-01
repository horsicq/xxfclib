/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_daemon_tools_mdx.h @brief DAEMON Tools MDX / MDS v2 disc image reader. */

#ifndef XXFCLIB_FORMAT_DAEMON_TOOLS_MDX_H
#define XXFCLIB_FORMAT_DAEMON_TOOLS_MDX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A DAEMON Tools "new format" disc image: single-file .mdx, or an
 *        MDS v2 descriptor (.mds) whose sector data lives in .mdf file(s).
 *
 * File header (48 bytes, little endian):
 *
 *   0x00  char[16] "MEDIA DESCRIPTOR"
 *   0x10  u8       major version, 2
 *   0x11  u8       minor version, 0 or 1
 *   0x12  char[26] copyright string
 *   0x2C  u32      MDS v2: offset of the 512-byte key header (it ends the
 *                  file, the descriptor lies between 0x30 and it);
 *                  MDX: 0xFFFFFFFF, and 0x30 holds u64 descriptor offset and
 *                  u64 descriptor length (the length counts the key
 *                  header's 64-byte salt, which starts the key header)
 *
 * Key header (512 bytes): a 64-byte salt, then 448 bytes enciphered with
 * AES-256 in a whitened CBC mode under a key derived by PBKDF2-HMAC-
 * RIPEMD-160 (2000 rounds) from a password that is itself computed from the
 * salt. Deciphered it carries "TRUE", the CRC-32 of 256 bytes of key data,
 * and the compressed and plain sizes of the descriptor. The descriptor is
 * AES-256 enciphered with that key data (whitened CBC, 512-byte units) and
 * zlib-compressed. It holds sessions (32 bytes each), track blocks (80
 * bytes), per-track extra blocks (pregap, length) and footer blocks (32
 * bytes) that place each track's sectors - optionally zlib/RLE compressed in
 * groups and AES-LRW enciphered - in the data file.
 *
 * Each track becomes one member, "trackNN.iso" (2048-byte data sectors),
 * "trackNN.cdda" (audio) or "trackNN.bin", holding the stored sectors
 * (main data plus any subchannel) after decompression and deciphering.
 * Pregap sectors that the image does not store are not synthesised. Tracks
 * whose data needs a user password are listed as encrypted and not
 * unpacked.
 */

#define XX_DAEMON_TOOLS_MDX_MAX_FILES 16U

typedef struct xx_daemon_tools_mdx {
    Abstractformat format;
    void *image;                 /**< Parsed descriptor (private). */
    uint64_t number_of_records;
    bool is_mdx;                 /**< Single-file MDX (data inside). */
    uint8_t version_minor;
    uint16_t medium_type;        /**< 0 CD-ROM, 3 DVD-ROM. */
    uint16_t number_of_sessions;
    bool data_encrypted;         /**< Track data is AES-LRW enciphered. */
    bool data_key_available;     /**< ...and the key could be derived. */
    uint32_t number_of_files;    /**< MDS v2: distinct data file names. */
    xx_io_device *data[XX_DAEMON_TOOLS_MDX_MAX_FILES];
    bool data_owned[XX_DAEMON_TOOLS_MDX_MAX_FILES];
} xx_daemon_tools_mdx;

typedef xx_daemon_tools_mdx xx_daemon_tools_mdx_t;

XXFC_API void xx_daemon_tools_mdx_init(xx_daemon_tools_mdx *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_daemon_tools_mdx *xx_daemon_tools_mdx_create(xx_io_device *device,
                                                         int64_t base_address);
XXFC_API void xx_daemon_tools_mdx_destroy(xx_daemon_tools_mdx *archive);
XXFC_API void xx_daemon_tools_mdx_free(xx_daemon_tools_mdx *archive);

XXFC_API bool xx_daemon_tools_mdx_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_daemon_tools_mdx_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_daemon_tools_mdx_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_daemon_tools_mdx_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_daemon_tools_mdx_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_daemon_tools_mdx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_daemon_tools_mdx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_daemon_tools_mdx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_daemon_tools_mdx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief MDS v2 only: name of data file @p index as the descriptor gives it
 *        (UTF-8; "*.mdf" means "the descriptor's name with extension mdf").
 */
XXFC_API const char *xx_daemon_tools_mdx_get_file_name(
    xx_daemon_tools_mdx *archive, uint32_t index);

/** @brief MDS v2 only: supply the device for data file @p index (not owned). */
XXFC_API bool xx_daemon_tools_mdx_set_data_device(xx_daemon_tools_mdx *archive,
                                                  uint32_t index,
                                                  xx_io_device *device);

/**
 * @brief MDS v2 only: open the data files next to @p mds_path.
 * @return number of data files that are available afterwards
 */
XXFC_API uint32_t xx_daemon_tools_mdx_open_data_files(
    xx_daemon_tools_mdx *archive, const char *mds_path);

/** @brief RIPEMD-160 of @p size bytes (exposed for the unit test). */
XXFC_API void xx_daemon_tools_mdx_rmd160(const void *data, size_t size,
                                         uint8_t digest[20]);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DAEMON_TOOLS_MDX_H */
