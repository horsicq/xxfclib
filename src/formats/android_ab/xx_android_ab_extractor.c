/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_android_ab_extractor.c - search raw data for Base64.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the android_ab reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/android_ab/xx_android_ab.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ANDROID_AB };

static Abstractformat *xx_android_ab_search_open(xx_io_device *window) {
    xx_android_ab *reader = xx_android_ab_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_android_ab_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_android_ab_free((xx_android_ab *)format);
}

static const uint8_t anchor_bytes[] = {0x41,0x4e,0x44,0x52,0x4f,0x49,0x44,0x20,0x42,0x41,0x43,0x4b,0x55,0x50,0x0a};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_android_ab_search_open, xx_android_ab_search_close, false
};

static xx_format_search_state *xx_android_ab_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_android_ab_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_android_ab_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_android_ab_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_android_ab_extractor = {
    xx_android_ab_create_format_search,
    xx_android_ab_get_current_format_info,
    xx_android_ab_format_search_find_next,
    xx_android_ab_free_format_search
};
