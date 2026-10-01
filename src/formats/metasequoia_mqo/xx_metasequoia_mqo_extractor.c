/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_metasequoia_mqo_extractor.c - search raw data for metasequoia_mqo.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the metasequoia_mqo reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/metasequoia_mqo/xx_metasequoia_mqo.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_METASEQUOIA_MQO };

static Abstractformat *xx_metasequoia_mqo_search_open(xx_io_device *window) {
    xx_metasequoia_mqo *reader = xx_metasequoia_mqo_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_metasequoia_mqo_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_metasequoia_mqo_free((xx_metasequoia_mqo *)format);
}

static const uint8_t anchor_bytes[] = {0x4d,0x65,0x74,0x61,0x73,0x65,0x71,0x75,0x6f,0x69,0x61,0x20,0x44,0x6f,0x63,0x75,0x6d,0x65,0x6e,0x74};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_metasequoia_mqo_search_open, xx_metasequoia_mqo_search_close
};

static xx_format_search_state *xx_metasequoia_mqo_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_metasequoia_mqo_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_metasequoia_mqo_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_metasequoia_mqo_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_metasequoia_mqo_extractor = {
    xx_metasequoia_mqo_create_format_search,
    xx_metasequoia_mqo_get_current_format_info,
    xx_metasequoia_mqo_format_search_find_next,
    xx_metasequoia_mqo_free_format_search
};
