/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_spirv_extractor.c - search raw data for spirv.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the spirv reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/spirv/xx_spirv.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SPIRV };

static Abstractformat *xx_spirv_search_open(xx_io_device *window) {
    xx_spirv *reader = xx_spirv_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_spirv_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_spirv_free((xx_spirv *)format);
}

static const uint8_t anchor_0[] = {0x03,0x02,0x23,0x07};
static const uint8_t anchor_1[] = {0x07,0x23,0x02,0x03};
static const xx_format_search_anchor anchors[] = {
    { anchor_0,sizeof(anchor_0),0 },
    { anchor_1,sizeof(anchor_1),0 },
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, sizeof(anchors)/sizeof(anchors[0]),
    xx_spirv_search_open, xx_spirv_search_close
};

static xx_format_search_state *xx_spirv_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_spirv_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_spirv_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_spirv_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_spirv_extractor = {
    xx_spirv_create_format_search,
    xx_spirv_get_current_format_info,
    xx_spirv_format_search_find_next,
    xx_spirv_free_format_search
};
