/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_zoom.h @brief Zoom (Amiga floppy imager, "ZOM5") reader. */

#ifndef XXFCLIB_FORMAT_ZOOM_H
#define XXFCLIB_FORMAT_ZOOM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Zoom container: a whole Amiga floppy, big-endian throughout.
 *
 * It holds ONE product - an .adf image of
 * (last_cylinder - first_cylinder + 1) * 0x2C00 bytes - so exactly one archive
 * record is published and the whole container is handed to the codec: the
 * chunk index is spread across the records themselves and no per-member extent
 * exists.
 */
typedef struct xx_zoom {
    Abstractformat format;      /**< Must stay first: the reader casts to it. */
    uint64_t number_of_records; /**< Always 1 for a valid container. */
    int64_t chunks_offset;      /**< First chunk record, past any note block. */
    int64_t uncompressed_size;  /**< Size of the .adf the codec would emit. */
    uint8_t first_cylinder;
    uint8_t last_cylinder;
    bool is_protected;          /**< Password flag at +0x24. */
} xx_zoom;

typedef xx_zoom xx_zoom_t;

XXFC_API void xx_zoom_init(xx_zoom *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_zoom *xx_zoom_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_zoom_destroy(xx_zoom *archive);
XXFC_API void xx_zoom_free(xx_zoom *archive);

XXFC_API bool xx_zoom_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_zoom_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_zoom_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_zoom_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_zoom_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_zoom_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_zoom_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_zoom_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_zoom_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API int64_t xx_zoom_get_uncompressed_size(const xx_zoom *archive);
XXFC_API bool xx_zoom_get_is_protected(const xx_zoom *archive);

static inline Abstractformat *xx_zoom_to_format(xx_zoom *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ZOOM_H */
