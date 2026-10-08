/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_mng_animation_extractor.c - search raw data for mng_animation.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the mng_animation reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/mng_animation/xx_mng_animation.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_MNG_ANIMATION };

static Abstractformat *xx_mng_animation_search_open(xx_io_device *window) {
    xx_mng_animation *reader = xx_mng_animation_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_mng_animation_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_mng_animation_free((xx_mng_animation *)format);
}

static const uint8_t anchor_0[] = {0x8a,0x4d,0x4e,0x47,0x0d,0x0a,0x1a,0x0a};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_mng_animation_search_open, xx_mng_animation_search_close, false
};

static xx_format_search_state *xx_mng_animation_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_mng_animation_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_mng_animation_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_mng_animation_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_mng_animation_extractor = {
    xx_mng_animation_create_format_search,
    xx_mng_animation_get_current_format_info,
    xx_mng_animation_format_search_find_next,
    xx_mng_animation_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(mng_animation, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
