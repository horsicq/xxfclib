/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The decoders in the implementation are a C99 port of the DMS decoder from
 * the "ancient" library, Copyright (C) Teemu Suutari. Which chunks form the
 * disk follows Deark's modules/dms.c (MIT, Copyright (C) Jason Summers).
 */

/** @file xx_dms.h @brief DMS (Amiga DiskMasher System) reader. */

/* DMS is the archiver Amiga bulletin boards used to ship floppy images.  The
 * payload is a disk, not a file tree: the container carries one compressed
 * chunk per physical track and the result of decoding them all is an ADF
 * image.  Everything is BIG endian.
 *
 *   archive header, 56 bytes
 *     +0   "DMS!"
 *     +4   u32 unused; the CRC16 at +54 covers +4..+53
 *     +8   u32 info bits - bit 1 obfuscated, bit 4 HD, bit 5 MS-DOS
 *     +12  u32 creation date
 *     +16  u16 first track of the disk's range
 *     +18  u16 last track of the disk's range
 *     +20  u32 packed size
 *     +24  u32 unpacked size
 *     +46  u16 version of the creating program
 *     +50  u16 disk type
 *     +52  u16 compression mode of the disk as a whole
 *     +54  u16 CRC16 of bytes 4..53
 *   The range at +16/+18 is meaningless when both are zero and a size is
 *   zero too; the tracks actually present then define it.
 *
 *   track header, 20 bytes, repeated
 *     +0   "TR"
 *     +2   u16 track number; 80 and anything >= 0x8000 are informational
 *     +6   u16 packed length of this track's chunk
 *     +8   u16 intermediate length, after the first of two decode stages
 *     +10  u16 unpacked length
 *     +12  u8  flags - bit 0 keep context, bit 1 re-read tables,
 *                      bit 2 run the RLE stage (heavy modes only)
 *     +13  u8  compression mode, 0..6
 *     +14  u16 additive checksum of the unpacked track
 *     +16  u16 CRC16 of the packed chunk
 *     +18  u16 CRC16 of bytes 0..17 of this header
 *     +20  the packed chunk
 *
 * The seven modes are DMS's own: 0 NONE, 1 SIMPLE (RLE only), 2 QUICK,
 * 3 MEDIUM, 4 DEEP, 5 HEAVY1, 6 HEAVY2.  Modes 2..6 keep an LZ context
 * across tracks unless a track clears flag bit 0, and modes 2..4 plus the
 * heavy modes with flag bit 2 run their LZ output through the same RLE
 * stage SIMPLE uses.  All seven are implemented here.
 *
 * Real archives carry more than the disk: BBS banners (track 0xFFFF), a
 * FILE_ID.DIZ (track 80), boot-block ads stored as short chunks numbered
 * track 0, and trainer tracks appended outside the header's range.  For
 * every track number of the range the LAST chunk of more than 2048 bytes
 * is the disk's; every other chunk is an "extra" chunk.
 *
 * Record 0 is disk.adf, the image from the lowest to the highest real track
 * present (a track missing inside that span reads back as zeros).  Then
 * one record per chunk, in file order:
 *   track_NNNNN              a real track
 *   extra_III_track_NNNNN    any other chunk, III being its position in the
 *                            file, so repeated numbers never collide
 * Every chunk is checked against its packed CRC and its unpacked checksum;
 * one that fails is not extracted, and disk.adf is only extracted when all
 * real tracks pass.
 *
 * Obfuscated ("password protected") archives are recognised and listed but
 * not decoded: recovering the key means brute-forcing a 17-bit space with a
 * full image decode per candidate, which is a CPU bomb an attacker controls
 * for free.  Such records are published with the encrypted flag set and
 * extraction of them is refused.
 *
 * Track numbers and lengths are attacker-controlled: the chunk count, the
 * packed size, the image and the total output of the extra chunks are all
 * capped, and a real track can only be placed inside the image.
 */

#ifndef XXFCLIB_FORMAT_DMS_H
#define XXFCLIB_FORMAT_DMS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_dms xx_dms;
typedef struct xx_dms xx_dms_t;
typedef struct xx_dms XDms;

struct xx_dms {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint64_t number_of_tracks; /**< Real tracks, the ones forming the image. */
    uint32_t image_size;       /**< 80 tracks worth of ADF, 901120 or 1802240. */
    uint32_t raw_size;         /**< Bytes the recorded tracks actually cover. */
    uint32_t raw_offset;       /**< Image offset the first recorded track sits at. */
    bool is_hd;                /**< High density, 22528 bytes per track. */
    bool is_obfuscated;        /**< Password protected; extraction is refused. */
    int64_t archive_end;       /**< base_address + packed size, or -1. */
    void *internal;
};

XXFC_API void xx_dms_init(xx_dms *dms, xx_io_device *dev, int64_t base_address);
XXFC_API xx_dms *xx_dms_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dms_destroy(xx_dms *dms);
XXFC_API void xx_dms_free(xx_dms *dms);

XXFC_API bool xx_dms_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dms_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dms_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_dms_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dms_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dms_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dms_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dms_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dms_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dms_get_number_of_records(const xx_dms *dms);
XXFC_API uint64_t xx_dms_get_number_of_members(const xx_dms *dms);
XXFC_API uint64_t xx_dms_get_number_of_tracks(const xx_dms *dms);
XXFC_API uint32_t xx_dms_get_image_size(const xx_dms *dms);
XXFC_API uint32_t xx_dms_get_raw_size(const xx_dms *dms);
XXFC_API int64_t xx_dms_get_archive_end(const xx_dms *dms);
/** Human readable name of a compression mode, "Unknown" outside 0..6. */
XXFC_API const char *xx_dms_mode_to_string(uint32_t mode);

static inline Abstractformat *xx_dms_to_format(xx_dms *dms) {
    return dms ? &dms->format : NULL;
}
static inline void XDms_init(xx_dms *dms, xx_io_device *dev,
                             int64_t base_address) {
    xx_dms_init(dms, dev, base_address);
}
static inline xx_dms *XDms_create(xx_io_device *dev, int64_t base_address) {
    return xx_dms_create(dev, base_address);
}
static inline void XDms_free(xx_dms *dms) { xx_dms_free(dms); }
static inline bool XDms_is_valid(xx_dms *dms, xx_pd_struct *pd) {
    return dms ? xx_format_is_valid(&dms->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DMS_H */
