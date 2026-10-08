/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_relic_chunky_extractor.c - search raw data for relic_chunky.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the relic_chunky reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/relic_chunky/xx_relic_chunky.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RELIC_CHUNKY };

static Abstractformat *xx_relic_chunky_search_open(xx_io_device *window) {
    xx_relic_chunky *reader = xx_relic_chunky_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_relic_chunky_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_relic_chunky_free((xx_relic_chunky *)format);
}

static const uint8_t anchor_bytes[] = {0x52,0x65,0x6c,0x69,0x63,0x20,0x43,0x68,0x75,0x6e,0x6b,0x79,0x0d,0x0a,0x1a,0x00};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_relic_chunky_search_open, xx_relic_chunky_search_close, false
};

static xx_format_search_state *xx_relic_chunky_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_relic_chunky_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_relic_chunky_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_relic_chunky_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_relic_chunky_extractor = {
    xx_relic_chunky_create_format_search,
    xx_relic_chunky_get_current_format_info,
    xx_relic_chunky_format_search_find_next,
    xx_relic_chunky_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(relic_chunky, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
