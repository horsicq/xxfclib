/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_inteduft_extractor.c - search raw data for INTEDUFT.
 *
 * Scans for:
 *   7C 2E 07 04 at +0  ("|...")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the inteduft reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/inteduft/xx_inteduft.h"

static const uint8_t k_anchor0[] = { 0x7C, 0x2E, 0x07, 0x04 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_INTEDUFT };

static Abstractformat *xx_inteduft_search_open(xx_io_device *window) {
    xx_inteduft *reader = xx_inteduft_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_inteduft_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_inteduft_free((xx_inteduft *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_inteduft_search_open, xx_inteduft_search_close
};

static xx_format_search_state *xx_inteduft_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_inteduft_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_inteduft_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_inteduft_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_inteduft_extractor = {
    xx_inteduft_create_format_search,
    xx_inteduft_get_current_format_info,
    xx_inteduft_format_search_find_next,
    xx_inteduft_free_format_search
};
