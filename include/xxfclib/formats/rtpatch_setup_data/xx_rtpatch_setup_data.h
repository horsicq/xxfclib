/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rtpatch_setup_data.h @brief RTPatch Setup data volume reader. */

#ifndef XXFCLIB_FORMAT_RTPATCH_SETUP_DATA_H
#define XXFCLIB_FORMAT_RTPATCH_SETUP_DATA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A data volume of the Pocket Soft RTPatch distribution setup
 * (DISK1.001, DISK2.001, ...), which the setup program on the first disk
 * reads and expands.  The volume is a plain file, not an executable.
 *
 * Little endian throughout.  The volume is a chain of records from its first
 * byte; there is no volume header, no count and no terminator.  Each record:
 *
 *   +0x00  u32  packed size: bytes of stream that follow the name
 *   +0x04  u32  unpacked size
 *   +0x08  u16  DOS attributes (0x20, 0x21 seen; directory and volume-label
 *               bits never occur)
 *   +0x0A  u16  DOS date of last write
 *   +0x0C  u16  DOS time of last write
 *   +0x0E  u16  name length, 2..13, counting the terminating NUL
 *   +0x10  the 8.3 name, that many bytes, the last one NUL
 *   ...    the member: one RTPatch adaptive-Huffman/LZSS stream, packed-size
 *          bytes, opening with its own 8-byte header (0xB5 0x9C, a raw-
 *          literal flag of 0 or 1, 0xFF, two non-zero 12-bit rescale periods
 *          and a 4-bit window selector)
 *
 * Both sizes are signed 32-bit to the setup engine.  The next record starts
 * right behind the stream.  A volume that was cut at a disk boundary ends
 * inside its last member's stream; that member continues on another volume.
 *
 * Records: every record in file order.  A member whose stream runs past the
 * end of the volume is listed (its compressed size is what is present) but
 * refused on extraction, and the volume then extends to the end of the
 * file.  The chain otherwise ends at the end of the file or at the first
 * bytes that are not a record; the format size is where it ended.
 * XX_META_ID_COMPRESSION_METHOD is XX_RTPATCH_SETUP_DATA_METHOD_RTPATCH;
 * XX_META_ID_ATTRIBUTES, XX_META_ID_LAST_MOD_DATE and XX_META_ID_LAST_MOD_TIME
 * carry the record's DOS fields.
 *
 * Names: bytes outside 0x21..0x7E and any of % / \ : * ? " < > | become
 * "%XX" (upper-case hex), so a name is always one path component.  A name
 * that repeats an earlier one (compared without ASCII case) gets
 * "%_<record index>" inserted before its extension.  A name ending in '.' or
 * ' ' and a Windows device name are listed but refused on extraction.
 * Without XX_META_ID_OPT_OVERWRITE an existing file is never replaced.
 */
typedef struct xx_rtpatch_setup_data {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t chain_end; /**< Volume size from the base: end of the chain. */
    bool split;        /**< The last member continues on another volume. */
} xx_rtpatch_setup_data;

typedef xx_rtpatch_setup_data xx_rtpatch_setup_data_t;

#define XX_RTPATCH_SETUP_DATA_METHOD_RTPATCH 1U

XXFC_API void xx_rtpatch_setup_data_init(xx_rtpatch_setup_data *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_rtpatch_setup_data *xx_rtpatch_setup_data_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_rtpatch_setup_data_destroy(xx_rtpatch_setup_data *archive);
XXFC_API void xx_rtpatch_setup_data_free(xx_rtpatch_setup_data *archive);

XXFC_API bool xx_rtpatch_setup_data_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_rtpatch_setup_data_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_rtpatch_setup_data_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_rtpatch_setup_data_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_rtpatch_setup_data_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_rtpatch_setup_data_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rtpatch_setup_data_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rtpatch_setup_data_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rtpatch_setup_data_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RTPATCH_SETUP_DATA_H */
