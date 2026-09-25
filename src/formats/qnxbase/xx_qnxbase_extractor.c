/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_qnxbase_extractor.c - search raw data for QNXBASE.
 *
 * Scans for:
 *   EB 4C 44 44 44 44 at +0  (".LDDDD")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the qnxbase reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/qnxbase/xx_qnxbase.h"

static const uint8_t k_anchor0[] = { 0xEB, 0x4C, 0x44, 0x44, 0x44, 0x44 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_QNXBASE };

static Abstractformat *xx_qnxbase_search_open(xx_io_device *window) {
    xx_qnxbase *reader = xx_qnxbase_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_qnxbase_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_qnxbase_free((xx_qnxbase *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_qnxbase_search_open, xx_qnxbase_search_close
};

static xx_format_search_state *xx_qnxbase_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_qnxbase_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_qnxbase_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_qnxbase_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_qnxbase_extractor = {
    xx_qnxbase_create_format_search,
    xx_qnxbase_get_current_format_info,
    xx_qnxbase_format_search_find_next,
    xx_qnxbase_free_format_search
};
