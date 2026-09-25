/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_ace_extractor.c - search raw data for ACE.
 *
 * Scans for:
 *   2A 2A 41 43 45 2A 2A at +7  ("..ACE..")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the ace reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ace/xx_ace.h"

static const uint8_t k_anchor0[] = { 0x2A, 0x2A, 0x41, 0x43, 0x45, 0x2A, 0x2A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 7U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ACE };

static Abstractformat *xx_ace_search_open(xx_io_device *window) {
    xx_ace *reader = xx_ace_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ace_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_ace_free((xx_ace *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_ace_search_open, xx_ace_search_close
};

static xx_format_search_state *xx_ace_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ace_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ace_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ace_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ace_extractor = {
    xx_ace_create_format_search,
    xx_ace_get_current_format_info,
    xx_ace_format_search_find_next,
    xx_ace_free_format_search
};
