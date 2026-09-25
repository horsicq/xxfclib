/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_jetbbs_extractor.c - search raw data for JETBBS.
 *
 * Scans for:
 *   2D 6D 67 30 2D at +2  ("-mg0-")
 *   2D 6D 67 34 2D at +2  ("-mg4-")
 *   2D 6D 67 35 2D at +2  ("-mg5-")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the jetbbs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/jetbbs/xx_jetbbs.h"

static const uint8_t k_anchor0[] = { 0x2D, 0x6D, 0x67, 0x30, 0x2D };
static const uint8_t k_anchor1[] = { 0x2D, 0x6D, 0x67, 0x34, 0x2D };
static const uint8_t k_anchor2[] = { 0x2D, 0x6D, 0x67, 0x35, 0x2D };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 2U },
    { k_anchor1, sizeof(k_anchor1), 2U },
    { k_anchor2, sizeof(k_anchor2), 2U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_JETBBS };

static Abstractformat *xx_jetbbs_search_open(xx_io_device *window) {
    xx_jetbbs *reader = xx_jetbbs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_jetbbs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_jetbbs_free((xx_jetbbs *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_jetbbs_search_open, xx_jetbbs_search_close
};

static xx_format_search_state *xx_jetbbs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_jetbbs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_jetbbs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_jetbbs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_jetbbs_extractor = {
    xx_jetbbs_create_format_search,
    xx_jetbbs_get_current_format_info,
    xx_jetbbs_format_search_find_next,
    xx_jetbbs_free_format_search
};
