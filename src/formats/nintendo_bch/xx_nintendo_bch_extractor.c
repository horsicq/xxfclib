/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_nintendo_bch_extractor.c - search raw data for nintendo_bch.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the nintendo_bch reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nintendo_bch/xx_nintendo_bch.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NINTENDO_BCH };

static Abstractformat *xx_nintendo_bch_search_open(xx_io_device *window) {
    xx_nintendo_bch *reader = xx_nintendo_bch_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_nintendo_bch_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_nintendo_bch_free((xx_nintendo_bch *)format);
}

static const uint8_t anchor_bytes[] = {0x42,0x43,0x48,0x00};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_nintendo_bch_search_open, xx_nintendo_bch_search_close
};

static xx_format_search_state *xx_nintendo_bch_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_nintendo_bch_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_nintendo_bch_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_nintendo_bch_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_nintendo_bch_extractor = {
    xx_nintendo_bch_create_format_search,
    xx_nintendo_bch_get_current_format_info,
    xx_nintendo_bch_format_search_find_next,
    xx_nintendo_bch_free_format_search
};
