/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_issetupstream.h
 *  @brief InstallShield "ISSetupStream" payload of a 2009+ setup.exe. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_ISSETUPSTREAM_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_ISSETUPSTREAM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The member container an InstallShield 2009 (and later) Setup.exe,
 * or the Binary.ISSetup.dll stream of an InstallShield-authored MSI, carries
 * behind its PE image.
 *
 * The carrier is an ordinary PE file.  The reader parses only its section
 * table, takes the end of the furthest section's raw data (the start of the
 * overlay) and expects the container there.  A device that starts with the
 * tag itself (a carved or dumped payload) is accepted as well.  The carrier
 * also holds the 14-byte tag as a plain string in .rdata; that copy is never
 * looked at, and would be refused anyway (record count 0).
 *
 * Header, 46 bytes, little endian:
 *   +0x00  char[14]  "ISSetupStream\0"
 *   +0x0E  u16       record count, non-zero
 *   +0x10  u8        container version, 2 or 3 (every sample is 3)
 *   +0x11  u8[29]    zero
 *
 * Record, 24 bytes, then the name, then the member stream:
 *   +0x00  u32  name size in BYTES: non-zero, even, at most 0x10000
 *   +0x04  u32  cipher selector: 0, 0xFFFFFFFF, 0xFFFFFFFD = none;
 *               2, 6 = name-keyed filter; anything else is unsupported
 *   +0x08  u16  zero
 *   +0x0A  u32  stream size as stored, below 2^31
 *   +0x0E  u64  zero
 *   +0x16  u16  storage: 0 = the stream is the file, 1 = zlib stream
 *   then   the name, UTF-16LE, exactly "name size" bytes
 *   then   the stream, exactly "stream size" bytes
 *
 * The filter key is the member's own name converted to UTF-8, XORed with
 * the repeating constant EC CA 79 F8.  Each stream byte has its nibbles
 * swapped and is XORed with a key byte: selector 6 uses key byte
 * ((pos mod 1024) mod key length), selector 2 key byte (pos mod key length).
 * The container records no unpacked size for a zlib member; its Adler-32
 * trailer (the last four bytes of the stream) is checked instead.
 */
typedef struct xx_installshield_issetupstream {
    Abstractformat format;
    uint64_t number_of_records;  /**< Complete records present. */
    uint32_t declared_records;   /**< The header's record count. */
    uint32_t container_version;  /**< 2 or 3. */
    int64_t stream_offset;       /**< Device offset of the tag. */
    int64_t payload_end;         /**< Device offset behind the last record. */
    bool in_pe;                  /**< Found at the overlay of a PE carrier. */
    bool truncated;              /**< Fewer complete records than declared. */
} xx_installshield_issetupstream;

typedef xx_installshield_issetupstream xx_installshield_issetupstream_t;

XXFC_API void xx_installshield_issetupstream_init(
    xx_installshield_issetupstream *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_installshield_issetupstream *xx_installshield_issetupstream_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_issetupstream_destroy(
    xx_installshield_issetupstream *archive);
XXFC_API void xx_installshield_issetupstream_free(
    xx_installshield_issetupstream *archive);

XXFC_API bool xx_installshield_issetupstream_check_is_valid(Abstractformat *self,
                                                            xx_pd_struct *pd);
XXFC_API bool xx_installshield_issetupstream_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_issetupstream_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_issetupstream_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_issetupstream_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_issetupstream_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_issetupstream_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_issetupstream_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_issetupstream_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_ISSETUPSTREAM_H */
