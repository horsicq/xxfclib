/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rawcd.h @brief Raw CD sector image (BIN / IMG / MDF payload)
 *  reader: cooks the 2048-byte user data out of raw sectors. */

#ifndef XXFCLIB_FORMAT_RAWCD_H
#define XXFCLIB_FORMAT_RAWCD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A raw CD image: the sectors of a disc (or of one data track) as a
 * drive reads them in raw mode, back to back, with no header of its own.
 * It is the payload of CDRWIN .bin, CloneCD .img, Alcohol .mdf and the
 * "trackNN.bin" members the cue / nrg / chd readers cut out.
 *
 * Sector layouts (ECMA-130, CD-ROM XA):
 *   2352  sync 00 FF*10 00 | header: BCD minute, second, frame, mode |
 *         mode 1: user data 2048 at +16, EDC, 8 zero, P/Q parity
 *         mode 2 (XA): subheader 8 at +16, form 1 user data 2048 at +24
 *                      (form 2: 2324 at +24)
 *         mode 0: 2336 zero bytes
 *   2368  a 2352 sector followed by 16 bytes of PQ subchannel
 *   2448  a 2352 sector followed by 96 bytes of P-W subchannel
 *   2336  a mode 2 sector without its 16 sync + header bytes (MODE2/2336):
 *         subheader at +0, user data at +8
 *
 * Members:
 *   image.iso   the 2048-byte user data of every sector of the leading data
 *               run - an ISO 9660 / UDF / HFS image with 2048-byte blocks.
 *               Mode 1 takes +16, mode 2 takes +24 (form 2 sectors give the
 *               first 2048 bytes of their payload), mode 0 gives zeros, and
 *               a sector inside the run that lost its sync takes the track's
 *               offset.
 *   audio.cdda  only when whole sectors follow the data run up to the end of
 *               the device (a mixed-mode disc: data track 1, audio after):
 *               the 2352-byte audio frames of those sectors, subchannel
 *               dropped.
 *
 * Detection: sync at sector 0 (mode 1 or 2, BCD address) and at sector 1
 * with the next address, which fixes the stride; and the image must begin a
 * disc (address 00:02:00) or carry a volume descriptor (CD001, CD-I, CDROM,
 * BEA01) at its sector 16.  Images with no sync at sector 0 (MODE2/2336, or
 * a 2352 / 2448 image whose first sectors were blanked) are accepted only
 * with ISO 9660 / CD-i descriptors at sectors 16 and 17 and a device size
 * that is a whole number of sectors.  A file that ends in a Nero footer is
 * an NRG image and is left to that reader.
 */
typedef struct xx_rawcd {
    Abstractformat format;
    uint64_t number_of_records;
    /** Stored bytes per sector: 2336, 2352, 2368 or 2448. */
    uint32_t sector_size;
    /** User-data offset for sectors without a usable header (8, 16 or 24). */
    uint32_t data_offset;
    /** Sectors carry sync and header (every layout except 2336). */
    bool has_sync;
    /** Sectors in the leading data run, from the image start. */
    int64_t data_sectors;
    /** Whole sectors after the run, up to the end of the device. */
    int64_t audio_sectors;
    int64_t archive_end;
} xx_rawcd;

typedef xx_rawcd xx_rawcd_t;

/** Bytes of user data each sector contributes to image.iso. */
#define XX_RAWCD_USER_DATA 2048
/** Bytes of each sector audio.cdda keeps. */
#define XX_RAWCD_AUDIO_FRAME 2352

XXFC_API void xx_rawcd_init(xx_rawcd *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_rawcd *xx_rawcd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rawcd_destroy(xx_rawcd *archive);
XXFC_API void xx_rawcd_free(xx_rawcd *archive);

XXFC_API bool xx_rawcd_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rawcd_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_rawcd_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_rawcd_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rawcd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rawcd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rawcd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rawcd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rawcd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Write the cooked 2048-byte-sector image (image.iso) to
 * @p destination; NULL only reads it through.  Handles base info first.
 */
XXFC_API bool xx_rawcd_cook_to_device(xx_rawcd *archive,
                                      xx_io_device *destination,
                                      xx_pd_struct *pd);

static inline Abstractformat *xx_rawcd_to_format(xx_rawcd *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RAWCD_H */
