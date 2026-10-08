/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_hxc_stream_hfe.h @brief HxC "Stream HFE" flux image reader. */

#ifndef XXFCLIB_FORMAT_HXC_STREAM_HFE_H
#define XXFCLIB_FORMAT_HXC_STREAM_HFE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An HxC Floppy Emulator "Stream HFE" flux image (hxcfe HXC_STREAMHFE).
 *
 * Not to be confused with the classic bit-cell HFE ("HXCPICFE", reader hfe).
 * All fields are little endian. Layout as measured on images written by
 * hxcfe 2.16.13.1:
 *
 *   0x00  char[16] "HxC_Stream_Image"
 *   0x10  u32  format revision (0)
 *   0x14  u32  flags (0)
 *   0x18  u32  end of the reserved track-list area (0x400 / 0x1800 seen)
 *   0x1C  u32  track list offset (0x200)
 *   0x20  u32  offset of the first track stream
 *   0x24  u32  number of tracks (cylinders)
 *   0x28  u32  number of sides
 *   0x2C  u32  stream tick period (40000 seen)
 *
 * The track list holds tracks * sides entries of 32 bytes, track major
 * (track 0 side 0, track 0 side 1, track 1 side 0, ...):
 *
 *   +0x00 u32  flags; bit 0 = the stream is one raw LZ4 block
 *   +0x04 u32  stream offset from the start of the image
 *   +0x08 u32  stored (packed) size
 *   +0x0C u32  unpacked size
 *   +0x10 u32  (5000000 seen)
 *   +0x14 u32  number of flux pulses in the stream
 *   +0x18 u32[2] zero
 *
 * Each track is exported as one member "trackNNN_sideS.stream" holding the
 * unpacked pulse stream, which is HxC's own variable-length encoding of flux
 * intervals; it is not decoded further. An entry with offset and sizes all
 * zero is an absent track and is not exported.
 */
typedef struct xx_hxc_stream_hfe {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t revision;
    uint32_t flags;
    uint32_t number_of_tracks;
    uint32_t number_of_sides;
    uint32_t tick_period;
    int64_t archive_end; /**< Absolute end of the last track stream. */
} xx_hxc_stream_hfe;

typedef xx_hxc_stream_hfe xx_hxc_stream_hfe_t;

#define XX_HXC_STREAM_HFE_SIGNATURE "HxC_Stream_Image"
#define XX_HXC_STREAM_HFE_SIGNATURE_SIZE 16U
#define XX_HXC_STREAM_HFE_HEADER_SIZE 0x30U
#define XX_HXC_STREAM_HFE_ENTRY_SIZE 32U
#define XX_HXC_STREAM_HFE_MAX_TRACKS 256U
#define XX_HXC_STREAM_HFE_MAX_SIDES 2U
/** Per-track cap on the stored and the unpacked stream size. */
#define XX_HXC_STREAM_HFE_MAX_STREAM (64U * 1024U * 1024U)

XXFC_API void xx_hxc_stream_hfe_init(xx_hxc_stream_hfe *archive,
                                     xx_io_device *device,
                                     int64_t base_address);
XXFC_API xx_hxc_stream_hfe *xx_hxc_stream_hfe_create(xx_io_device *device,
                                                     int64_t base_address);
XXFC_API void xx_hxc_stream_hfe_destroy(xx_hxc_stream_hfe *archive);
XXFC_API void xx_hxc_stream_hfe_free(xx_hxc_stream_hfe *archive);

XXFC_API bool xx_hxc_stream_hfe_check_is_valid(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API bool xx_hxc_stream_hfe_handle_base_info(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API int64_t xx_hxc_stream_hfe_get_format_size(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API uint64_t xx_hxc_stream_hfe_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_hxc_stream_hfe_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_hxc_stream_hfe_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_hxc_stream_hfe_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_hxc_stream_hfe_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_hxc_stream_hfe_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hxc_stream_hfe_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hxc_stream_hfe_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hxc_stream_hfe_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_HXC_STREAM_HFE_H */
