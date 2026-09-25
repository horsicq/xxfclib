/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_arq_extractor.c - search raw data for ARQ.
 *
 * Scans for:
 *   67 57 04 01 at +0  ("gW..")
 *   67 57 04 02 at +0  ("gW..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the arq reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/arq/xx_arq.h"

static const uint8_t k_anchor0[] = { 0x67, 0x57, 0x04, 0x01 };
static const uint8_t k_anchor1[] = { 0x67, 0x57, 0x04, 0x02 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ARQ };

static Abstractformat *xx_arq_search_open(xx_io_device *window) {
    xx_arq *reader = xx_arq_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_arq_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_arq_free((xx_arq *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_arq_search_open, xx_arq_search_close
};

static xx_format_search_state *xx_arq_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_arq_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_arq_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_arq_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_arq_extractor = {
    xx_arq_create_format_search,
    xx_arq_get_current_format_info,
    xx_arq_format_search_find_next,
    xx_arq_free_format_search
};
