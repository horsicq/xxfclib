/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_blindwrite_5_6_image.h @brief BlindWrite 5/6 disc image
 *  descriptor (.b5t / .b6t) reader: splits the referenced .b00, .b01 ...
 *  data files into the disc's tracks. */

#ifndef XXFCLIB_FORMAT_BLINDWRITE_5_6_IMAGE_H
#define XXFCLIB_FORMAT_BLINDWRITE_5_6_IMAGE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most data blocks one descriptor may list (a 50 GB disc split into 2 GB
 *  files needs 25; a CD needs one per track mode). */
#define XX_BLINDWRITE_5_6_IMAGE_MAX_BLOCKS 256

/**
 * @brief A BlindWrite 5/6 descriptor.  All integers little-endian.
 *
 *   0x000  char[16] "BWT5 STREAM SIGN"
 *   0x010  disc block 1, 112 bytes:
 *            +0x20 u16 disc type (MMC profile: 0x08 CD-ROM, 0x10 DVD-ROM ...)
 *            +0x22 u16 number of sessions
 *            +0x30 u8 MCN valid, char[13] MCN
 *            +0x50 u16 PMA, u16 ATIP, u16 CD-TEXT, u16 CD info lengths
 *            +0x58 u32 BCA length
 *            +0x68 u32 DVD structures length, u32 DVD info length
 *   0x080  32 unknown bytes
 *   0x0A0  INQUIRY vendor[8] product[16] revision[4] vendor-specific[20]
 *   0x0D0  char[32] volume identifier
 *   0x0F0  disc block 2: u32 lengths of mode page 0x2A, unknown block,
 *          data-block list, session list, internal DPM data
 *   0x104  mode page 0x2A, unknown block, PMA, ATIP, CD-TEXT, BCA, DVD
 *          structures, then the disc info block (the CD length for disc
 *          types 0x08..0x0A, the DVD length otherwise)
 *          data-block list: u32 count, u32 drive path length, drive path
 *            (UTF-16), then per block: u32 type (bit 15: audio), u32 length
 *            in bytes, 4 x u32, u32 offset in its data file, 3 x u32,
 *            s32 first sector, s32 length in sectors, u32 file name length
 *            in bytes, UTF-16LE file name, u32
 *          sessions: u16 number, u8 entry count, u8, s32 start, s32 end,
 *            u16 first track, u16 last track, then entries of 64 bytes:
 *            u8 type (0 not a track, 1 audio, 2 mode 1, 3 mode 2,
 *            4 mode 2 form 1, 5 mode 2 form 2, 6 DVD) ... u8 point (track
 *            number) at +12, u32 pregap at +22, s32 first sector at +42,
 *            s32 length at +46; entries of types 1..5 carry 8 more bytes
 *          internal DPM data, u32 declared descriptor length,
 *          char[16] "BWT5 STREAM FOOT"
 *
 * The descriptor holds no sector data.  Its members are the tracks, cut out
 * of the data blocks exactly as stored (sector size = block bytes / block
 * sectors; no sector cooking, subchannel kept when stored):
 *   trackNN.iso   2048-byte sectors (DVD tracks, cooked CD data)
 *   trackNN.cdda  audio tracks stored as 2352-byte sectors
 *   trackNN.bin   anything else (raw data sectors, sectors + subchannel)
 * An xx_io_device has no path, so the data files cannot be found from the
 * descriptor alone: the tracks are listed and measured, but unpacking needs
 * the data files attached first, by a caller that knows the descriptor's
 * path (xx_blindwrite_5_6_image_open_data_files) or one device per data
 * block (xx_blindwrite_5_6_image_set_data_device).
 */
typedef struct xx_blindwrite_5_6_image {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t number_of_blocks;
    uint32_t number_of_sessions;
    uint16_t disc_type;
    int64_t descriptor_size;
    /** Data device per data block, in descriptor order (NULL: none). */
    xx_io_device *data[XX_BLINDWRITE_5_6_IMAGE_MAX_BLOCKS];
    /** True when the reader opened data[i] itself and must close it. */
    bool data_owned[XX_BLINDWRITE_5_6_IMAGE_MAX_BLOCKS];
} xx_blindwrite_5_6_image;

typedef xx_blindwrite_5_6_image xx_blindwrite_5_6_image_t;

XXFC_API void xx_blindwrite_5_6_image_init(xx_blindwrite_5_6_image *archive,
                                           xx_io_device *device,
                                           int64_t base_address);
XXFC_API xx_blindwrite_5_6_image *xx_blindwrite_5_6_image_create(
    xx_io_device *device, int64_t base_address);
/** Releases parse state and closes the data devices the reader opened. */
XXFC_API void xx_blindwrite_5_6_image_destroy(
    xx_blindwrite_5_6_image *archive);
XXFC_API void xx_blindwrite_5_6_image_free(xx_blindwrite_5_6_image *archive);

XXFC_API bool xx_blindwrite_5_6_image_check_is_valid(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API bool xx_blindwrite_5_6_image_handle_base_info(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API int64_t xx_blindwrite_5_6_image_get_format_size(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_blindwrite_5_6_image_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_blindwrite_5_6_image_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_blindwrite_5_6_image_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_blindwrite_5_6_image_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_blindwrite_5_6_image_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_blindwrite_5_6_image_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Detector prefilter: "BWT5 STREAM SIGN" in the first 16 bytes. */
XXFC_API bool xx_blindwrite_5_6_image_test_magic(const uint8_t *magic,
                                                 size_t magic_size);

/**
 * @brief Attach the data device of data block @p block_index (0-based,
 * descriptor order).  The device stays owned by the caller and must outlive
 * the reader's use of it; NULL detaches.
 */
XXFC_API bool xx_blindwrite_5_6_image_set_data_device(
    xx_blindwrite_5_6_image *archive, uint32_t block_index,
    xx_io_device *device);

/**
 * @brief Open every data block's file next to the descriptor.
 * @p descriptor_path is the UTF-8 path the descriptor was opened from.  Only
 * the last component of each stored file name is used (the names may carry
 * the imaging machine's path), looked up in the descriptor's own directory;
 * a name that is empty, "." / "..", has control or reserved characters, or
 * is a Windows device name is never opened.  The devices are owned by the
 * reader.
 * @return how many data blocks now have a data device.
 */
XXFC_API uint32_t xx_blindwrite_5_6_image_open_data_files(
    xx_blindwrite_5_6_image *archive, const char *descriptor_path);

/** Number of data blocks (0 when the descriptor is not valid). */
XXFC_API uint32_t xx_blindwrite_5_6_image_get_number_of_blocks(
    xx_blindwrite_5_6_image *archive);
/**
 * @brief The stored file name of data block @p block_index, as UTF-8.
 * Free with xx_str_free; NULL when out of range or not valid.
 */
XXFC_API char *xx_blindwrite_5_6_image_get_block_file_name(
    xx_blindwrite_5_6_image *archive, uint32_t block_index);

#ifdef __cplusplus
}
#endif

#endif
