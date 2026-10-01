/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_flash_video_flv_extractor.c - search raw data for flash_video_flv.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the flash_video_flv reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/flash_video_flv/xx_flash_video_flv.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_FLASH_VIDEO_FLV };

static Abstractformat *xx_flash_video_flv_search_open(xx_io_device *window) {
    xx_flash_video_flv *reader = xx_flash_video_flv_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_flash_video_flv_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_flash_video_flv_free((xx_flash_video_flv *)format);
}

static const uint8_t anchor_bytes[] = {0x46,0x4c,0x56};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_flash_video_flv_search_open, xx_flash_video_flv_search_close
};

static xx_format_search_state *xx_flash_video_flv_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_flash_video_flv_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_flash_video_flv_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_flash_video_flv_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_flash_video_flv_extractor = {
    xx_flash_video_flv_create_format_search,
    xx_flash_video_flv_get_current_format_info,
    xx_flash_video_flv_format_search_find_next,
    xx_flash_video_flv_free_format_search
};
