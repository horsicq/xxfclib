/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_dsl2_extractor.c - search raw data for DSL2.
 *
 * Scans for:
 *   44 53 27 4C 20 69 6E 73 74 61 6C 6C 20 32 2E 30 at +0  ("DS'L install 2.0")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the dsl2 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dsl2/xx_dsl2.h"

static const uint8_t k_anchor0[] = { 0x44, 0x53, 0x27, 0x4C, 0x20, 0x69, 0x6E, 0x73, 0x74, 0x61, 0x6C, 0x6C, 0x20, 0x32, 0x2E, 0x30 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_DSL2 };

static Abstractformat *xx_dsl2_search_open(xx_io_device *window) {
    xx_dsl2 *reader = xx_dsl2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_dsl2_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_dsl2_free((xx_dsl2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_dsl2_search_open, xx_dsl2_search_close
};

static xx_format_search_state *xx_dsl2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_dsl2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_dsl2_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_dsl2_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_dsl2_extractor = {
    xx_dsl2_create_format_search,
    xx_dsl2_get_current_format_info,
    xx_dsl2_format_search_find_next,
    xx_dsl2_free_format_search
};
