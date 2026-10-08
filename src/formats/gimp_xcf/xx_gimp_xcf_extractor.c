/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gimp_xcf_extractor.c - search raw data for gimp_xcf.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the gimp_xcf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gimp_xcf/xx_gimp_xcf.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GIMP_XCF };

static Abstractformat *xx_gimp_xcf_search_open(xx_io_device *window) {
    xx_gimp_xcf *reader = xx_gimp_xcf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gimp_xcf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gimp_xcf_free((xx_gimp_xcf *)format);
}

static const uint8_t anchor_0[] = {0x67,0x69,0x6d,0x70,0x20,0x78,0x63,0x66,0x20};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_gimp_xcf_search_open, xx_gimp_xcf_search_close, false
};

static xx_format_search_state *xx_gimp_xcf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gimp_xcf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gimp_xcf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gimp_xcf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gimp_xcf_extractor = {
    xx_gimp_xcf_create_format_search,
    xx_gimp_xcf_get_current_format_info,
    xx_gimp_xcf_format_search_find_next,
    xx_gimp_xcf_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(gimp_xcf, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
