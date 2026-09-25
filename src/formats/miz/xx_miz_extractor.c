/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_miz_extractor.c - search raw data for MIZ.
 *
 * Scans for:
 *   44 4B 43 4C at +0  ("DKCL")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the miz reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/miz/xx_miz.h"

static const uint8_t k_anchor0[] = { 0x44, 0x4B, 0x43, 0x4C };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MIZ };

static Abstractformat *xx_miz_search_open(xx_io_device *window) {
    xx_miz *reader = xx_miz_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_miz_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_miz_free((xx_miz *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_miz_search_open, xx_miz_search_close
};

static xx_format_search_state *xx_miz_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_miz_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_miz_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_miz_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_miz_extractor = {
    xx_miz_create_format_search,
    xx_miz_get_current_format_info,
    xx_miz_format_search_find_next,
    xx_miz_free_format_search
};
