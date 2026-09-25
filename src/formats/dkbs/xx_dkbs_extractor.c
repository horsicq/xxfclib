/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_dkbs_extractor.c - search raw data for DKBS.
 *
 * Scans for:
 *   5F 64 6B 62 73 5F at +7  ("_dkbs_")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the dkbs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dkbs/xx_dkbs.h"

static const uint8_t k_anchor0[] = { 0x5F, 0x64, 0x6B, 0x62, 0x73, 0x5F };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 7U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_DKBS };

static Abstractformat *xx_dkbs_search_open(xx_io_device *window) {
    xx_dkbs *reader = xx_dkbs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_dkbs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_dkbs_free((xx_dkbs *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_dkbs_search_open, xx_dkbs_search_close
};

static xx_format_search_state *xx_dkbs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_dkbs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_dkbs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_dkbs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_dkbs_extractor = {
    xx_dkbs_create_format_search,
    xx_dkbs_get_current_format_info,
    xx_dkbs_format_search_find_next,
    xx_dkbs_free_format_search
};
