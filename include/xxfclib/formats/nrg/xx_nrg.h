/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_nrg.h @brief Nero Burning ROM disc image (.nrg) reader. */

#ifndef XXFCLIB_FORMAT_NRG_H
#define XXFCLIB_FORMAT_NRG_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most tracks one image may describe (a CD holds at most 99). */
#define XX_NRG_MAX_TRACKS 99
/** Most members: a stored pregap plus the body for every track. */
#define XX_NRG_MAX_MEMBERS (2 * XX_NRG_MAX_TRACKS)

/**
 * @brief A Nero image: the track data first, then a chunk list, then a
 * footer that points back at the chunk list.  All numbers are big-endian.
 *
 *   footer  v2: "NER5" + u64 chunk-list offset, the last 12 bytes
 *           v1: "NERO" + u32 chunk-list offset, the last 8 bytes
 *   chunk   tag[4] + u32 payload size + payload; the list ends with "END!"
 *
 * Chunks that describe tracks (one set per session):
 *   CUEX / CUES  cue points, 8 bytes each (not needed to cut the tracks)
 *   DAOX / DAOI  disc-at-once: a 22-byte header (u32 size, mcn[13], four
 *                bytes, first and last track), then per track 42 / 30 bytes:
 *                  +0  isrc[12]
 *                  +12 u16 sector size (2048, 2336, 2352 or 2448)
 *                  +14 u8  mode code
 *                  +15 u8, u16 (unused here)
 *                  +18 index 0 (pregap start), index 1 (track start) and
 *                      end offsets: u64 each in DAOX, u32 each in DAOI
 *   ETN2 / ETNF  track-at-once: per track 32 / 20 bytes: offset, size
 *                (u64 in ETN2, u32 in ETNF), u32 mode code, u32 start LBA,
 *                then a reserved u64 / u32
 *   SINF, MTYP, CDTX and other chunks carry no extents and are skipped.
 *
 * Mode codes: 0x00 Mode 1 and 0x02 Mode 2 Form 1 user data (2048), 0x03
 * Mode 2 (2336), 0x05 Mode 1 / 0x06 Mode 2 raw (2352), 0x07 audio (2352),
 * 0x0F / 0x11 raw data and 0x10 audio with 96 bytes of interleaved
 * subchannel (2448).
 *
 * Members are the tracks cut out of the image exactly as stored, named like
 * the cue reader's (tracks numbered 01.. across all sessions):
 *   trackNN.pregap.<ext>  a DAO track's stored pregap (index 0 to index 1)
 *   trackNN.<ext>         the track body (index 1 to end, or the ETN extent)
 * with <ext> "iso" for 2048-byte data, "bin" for raw data and for cooked
 * pregaps, "cdda" for audio and "cdg" for audio with subchannel.
 * A TAO table that repeats extents a DAO table already covers is ignored.
 */
typedef struct xx_nrg {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t number_of_tracks;
    uint32_t number_of_sessions;
    /** 1 for a "NERO" footer, 2 for "NER5"; 0 before handle_base_info. */
    uint32_t footer_version;
    /** Offset of the chunk list from the image start (-1 before parsing). */
    int64_t chunk_list_offset;
} xx_nrg;

typedef xx_nrg xx_nrg_t;

XXFC_API void xx_nrg_init(xx_nrg *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_nrg *xx_nrg_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_nrg_destroy(xx_nrg *archive);
XXFC_API void xx_nrg_free(xx_nrg *archive);

XXFC_API bool xx_nrg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_nrg_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_nrg_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_nrg_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_nrg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_nrg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_nrg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_nrg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_nrg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_nrg_to_format(xx_nrg *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NRG_H */
