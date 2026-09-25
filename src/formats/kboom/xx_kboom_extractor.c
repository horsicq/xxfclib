/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_kboom_extractor.c - search raw data for KBOOM.
 *
 * Scans for:
 *   A8 4D 50 A8 at +0  (".MP.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the kboom reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/kboom/xx_kboom.h"

static const uint8_t k_anchor0[] = { 0xA8, 0x4D, 0x50, 0xA8 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_KBOOM };

static Abstractformat *xx_kboom_search_open(xx_io_device *window) {
    xx_kboom *reader = xx_kboom_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_kboom_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_kboom_free((xx_kboom *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_kboom_search_open, xx_kboom_search_close
};

static xx_format_search_state *xx_kboom_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_kboom_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_kboom_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_kboom_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_kboom_extractor = {
    xx_kboom_create_format_search,
    xx_kboom_get_current_format_info,
    xx_kboom_format_search_find_next,
    xx_kboom_free_format_search
};
