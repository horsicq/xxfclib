/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_chd_extractor.c - search raw data for MAME CHD.
 *
 * Scans for:
 *   4D 43 6F 6D 70 72 48 44 at +0
 * Each candidate must be accepted by the chd reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/chd/xx_chd.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x43, 0x6F, 0x6D, 0x70, 0x72, 0x48, 0x44 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_CHD };

static Abstractformat *xx_chd_search_open(xx_io_device *window) {
    xx_chd *reader = xx_chd_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_chd_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_chd_free((xx_chd *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_chd_search_open, xx_chd_search_close, false
};

static xx_format_search_state *xx_chd_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_chd_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_chd_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_chd_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_chd_extractor = {
    xx_chd_create_format_search,
    xx_chd_get_current_format_info,
    xx_chd_format_search_find_next,
    xx_chd_free_format_search
};


/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(chd, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
