/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_copydisk_extractor.c - search raw data for CopyDisk.
 *
 * Scans for:
 *   43 4F 50 59 44 49 53 4B at +0  ("COPYDISK")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the copydisk reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/copydisk/xx_copydisk.h"

static const uint8_t k_anchor0[] = { 0x43, 0x4F, 0x50, 0x59, 0x44, 0x49, 0x53, 0x4B };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_COPYDISK };

static Abstractformat *xx_copydisk_search_open(xx_io_device *window) {
    xx_copydisk *reader = xx_copydisk_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_copydisk_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_copydisk_free((xx_copydisk *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_copydisk_search_open, xx_copydisk_search_close
};

static xx_format_search_state *xx_copydisk_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_copydisk_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_copydisk_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_copydisk_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_copydisk_extractor = {
    xx_copydisk_create_format_search,
    xx_copydisk_get_current_format_info,
    xx_copydisk_format_search_find_next,
    xx_copydisk_free_format_search
};
