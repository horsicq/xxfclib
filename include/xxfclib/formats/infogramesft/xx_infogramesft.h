/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_infogramesft.h @brief Infogrames / AITD engine .PAK reader. */

#ifndef XXFCLIB_FORMAT_INFOGRAMESFT_H
#define XXFCLIB_FORMAT_INFOGRAMESFT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Infogrames / Alone in the Dark engine resource container.
 *
 * The file is HEADERLESS: it opens directly on a u32 LE offset table whose
 * slot 0 is a hard zero and whose slot 1 is the byte size of the table, which
 * doubles as the offset of the first record.
 *
 *   offset table, at 0, `table size` bytes, u32 LE slots:
 *     [0]      0
 *     [1]      table size == offset of the first chain record
 *     [2..]    one slot per resource id; 0 means "no resource".  The map is
 *              many-to-one: ids the archive never filled alias a record that
 *              is already present, and the dead slots are not confined to the
 *              end.  Membership therefore comes from the record chain, and the
 *              table only names members and validates the file.
 *
 *   record chain, from `table size` to the last byte:
 *     optional 16-byte PKZIP crumb - a local file header cut off after two CRC
 *     bytes: "PK\3\4", u16 version, u16 flags, u16 method, u16 DOS time,
 *     u16 DOS date, u16 CRC-low.  The checksum is meant to be the LOW HALF of
 *     the CRC32 of that record's UNPACKED member, but archives re-use crumbs
 *     across records, so it is neither verified nor published; only the DOS
 *     stamp is kept.
 *
 *     record header, 16 bytes:
 *       0x00  u32 LE additional-descriptor size
 *       0x04  u32 LE packed size
 *       0x08  u32 LE unpacked size
 *       0x0c  u8  method: 0 stored, 1 Infogrames implode, 4 raw deflate
 *       0x0d  u8  codec parameter; zero in every known record
 *       0x0e  u16 LE inline-descriptor size
 *     then the inline descriptor (0x49, its own size, NUL-padded 8.3 name),
 *     then the additional descriptor, then the payload.  The next member
 *     begins where that payload ends.
 *
 *   A trailing 16-byte "PK\1\2" crumb - the head of a central directory record
 *   cut off with everything behind it - ends the chain.
 *
 * Members are published in chain order as "NNNNN_<stored name>", NNNNN being
 * the lowest resource id naming the record.  A record without a usable stored
 * name is published as "NNNNN.bin": a name is unusable when it holds a control
 * or non-ASCII byte, a separator or one of * ? " < > |, is only dots and
 * blanks, ends in a dot or blank, or is a Windows device name (CON, PRN, AUX,
 * NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$, with or without extension).
 *
 * Detection decodes the first compressed member as a trial, bounded to 64 KiB
 * of output (and 260 KiB of packed input) whatever the member declares.
 *
 * Layout, validation rule and the method-1 decoder are ported from XArchive's
 * games/xinfogramespak.cpp and Algos/xinfogramespakdecoder.cpp.
 */
typedef struct xx_infogramesft {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t table_size;
} xx_infogramesft;

typedef xx_infogramesft xx_infogramesft_t;

XXFC_API void xx_infogramesft_init(xx_infogramesft *archive,
                                   xx_io_device *device, int64_t base_address);
XXFC_API xx_infogramesft *xx_infogramesft_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_infogramesft_destroy(xx_infogramesft *archive);
XXFC_API void xx_infogramesft_free(xx_infogramesft *archive);

/**
 * @brief Detector pre-check over the first bytes of a file (the 64-byte
 * magic window).  True when slot 0 is zero, slot 1 is a table size that fits
 * the file, every slot inside the window is 0 or points between the table's
 * end and the file's end, and - when the first record header lies inside the
 * window - its method and parameter bytes are legal.  A necessary condition
 * only: the probe (check_is_valid) decides.
 */
XXFC_API bool xx_infogramesft_test_magic(const uint8_t *magic,
                                         size_t magic_size,
                                         int64_t total_size);

XXFC_API bool xx_infogramesft_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_infogramesft_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_infogramesft_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_infogramesft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_infogramesft_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_infogramesft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_infogramesft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_infogramesft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_infogramesft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INFOGRAMESFT_H */
