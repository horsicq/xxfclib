/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cue.h @brief CDRWIN cue sheet (.cue) reader: splits the
 *  referenced BIN / WAVE data files into their tracks. */

#ifndef XXFCLIB_FORMAT_CUE_H
#define XXFCLIB_FORMAT_CUE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most FILE statements one sheet may carry (a disc has at most 99 tracks). */
#define XX_CUE_MAX_FILES 100
/** Largest sheet accepted; real sheets are a few KiB. */
#define XX_CUE_MAX_SHEET (1024 * 1024)

/**
 * @brief A cue sheet: a text file that lays the tracks of a CD out over one
 * or more data files.
 *
 *   FILE "<name>" BINARY|MOTOROLA|WAVE|AIFF|MP3|...
 *     TRACK nn AUDIO|CDG|MODE1/2048|MODE1/2352|MODE2/2336|MODE2/2352|...
 *       INDEX 00 mm:ss:ff    optional pregap start
 *       INDEX 01 mm:ss:ff    track start (required), INDEX 02..99 optional
 *   plus REM / TITLE / PERFORMER / CATALOG / FLAGS / ISRC / PREGAP /
 *   POSTGAP lines that carry no data.  mm:ss:ff counts 75 frames a second;
 *   a frame is one sector of the track's size, so an INDEX is a byte offset
 *   into the current FILE once the sizes of the sectors before it are known.
 *
 * The sheet itself holds no payload.  Its members are the tracks, cut out of
 * the data files exactly as stored (no sector cooking):
 *   trackNN.pregap.<ext>  INDEX 00 up to INDEX 01, when the data file holds it
 *   trackNN.<ext>         INDEX 01 up to the next track, or the end of the file
 * with <ext> "iso" for 2048-byte data tracks, "bin" for raw data tracks,
 * "cdda" for audio and "cdg" for CD+G.  A WAVE file contributes the PCM of
 * its data chunk (44.1 kHz, 16-bit, stereo).
 *
 * An xx_io_device has no path, so the data files cannot be found from the
 * sheet alone: the tracks are listed and measured as far as the sheet allows,
 * but unpacking needs the data files attached first, either by a caller that
 * knows the sheet's path (xx_cue_open_data_files) or one device at a time
 * (xx_cue_set_data_device).
 */
typedef struct xx_cue {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t number_of_tracks;
    uint32_t number_of_files;
    int64_t sheet_size;
    /** Data device per FILE statement, in sheet order (NULL: not attached). */
    xx_io_device *data[XX_CUE_MAX_FILES];
    /** True when the reader opened data[i] itself and must close it. */
    bool data_owned[XX_CUE_MAX_FILES];
} xx_cue;

typedef xx_cue xx_cue_t;

XXFC_API void xx_cue_init(xx_cue *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_cue *xx_cue_create(xx_io_device *device, int64_t base_address);
/** Releases parse state and closes the data devices the reader opened. */
XXFC_API void xx_cue_destroy(xx_cue *archive);
XXFC_API void xx_cue_free(xx_cue *archive);

XXFC_API bool xx_cue_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cue_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cue_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_cue_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cue_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cue_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cue_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cue_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cue_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Detector prefilter over the first bytes of a file.
 *
 * True when, after an optional UTF-8 byte order mark and blank space, the
 * text starts with a keyword a cue sheet can open with (REM, FILE, TITLE,
 * PERFORMER, SONGWRITER, CATALOG, CDTEXTFILE; any case) followed by a
 * blank.  Only a cheap gate: xx_cue_check_is_valid decides.
 */
XXFC_API bool xx_cue_test_magic(const uint8_t *magic, size_t magic_size);

/**
 * @brief Attach the data device of the FILE statement @p file_index
 * (0-based, sheet order).  The device stays owned by the caller and must
 * outlive the reader's use of it; NULL detaches.
 */
XXFC_API bool xx_cue_set_data_device(xx_cue *archive, uint32_t file_index,
                                     xx_io_device *device);

/**
 * @brief Open every FILE statement's data file next to the sheet.
 *
 * @p cue_path is the UTF-8 path the sheet was opened from.  Only the last
 * component of each FILE name is used (sheets often carry the ripping
 * machine's absolute path), and it is looked up in the sheet's own
 * directory; a name that is empty, "." / "..", has control or reserved
 * characters, or is a Windows device name is never opened.  The devices are
 * owned by the reader.
 * @return how many FILE statements now have a data device.
 */
XXFC_API uint32_t xx_cue_open_data_files(xx_cue *archive,
                                         const char *cue_path);

/** Number of FILE statements (0 when the sheet is not valid). */
XXFC_API uint32_t xx_cue_get_number_of_files(xx_cue *archive);
/** Number of TRACK statements (0 when the sheet is not valid). */
XXFC_API uint32_t xx_cue_get_number_of_tracks(xx_cue *archive);
/**
 * @brief The name of FILE statement @p file_index as written (quotes
 * removed), newly allocated; free it with xx_str_free.  NULL on error.
 */
XXFC_API char *xx_cue_get_file_name(xx_cue *archive, uint32_t file_index);

static inline Abstractformat *xx_cue_to_format(xx_cue *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CUE_H */
