/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_wrzl_extractor.c - search raw data for WRZL.
 *
 * Scans for:
 *   57 52 5A 4C at +0  ("WRZL")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the wrzl reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/wrzl/xx_wrzl.h"

static const uint8_t k_anchor0[] = { 0x57, 0x52, 0x5A, 0x4C };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_WRZL };

static Abstractformat *xx_wrzl_search_open(xx_io_device *window) {
    xx_wrzl *reader = xx_wrzl_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_wrzl_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_wrzl_free((xx_wrzl *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_wrzl_search_open, xx_wrzl_search_close
};

static xx_format_search_state *xx_wrzl_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_wrzl_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_wrzl_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_wrzl_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_wrzl_extractor = {
    xx_wrzl_create_format_search,
    xx_wrzl_get_current_format_info,
    xx_wrzl_format_search_find_next,
    xx_wrzl_free_format_search
};
