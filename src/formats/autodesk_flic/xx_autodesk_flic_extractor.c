/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_autodesk_flic_extractor.c - search raw data for autodesk_flic.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the autodesk_flic reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/autodesk_flic/xx_autodesk_flic.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_AUTODESK_FLIC };

static Abstractformat *xx_autodesk_flic_search_open(xx_io_device *window) {
    xx_autodesk_flic *reader = xx_autodesk_flic_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_autodesk_flic_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_autodesk_flic_free((xx_autodesk_flic *)format);
}

static const uint8_t anchor_0[] = {0x11,0xaf};
static const uint8_t anchor_1[] = {0x12,0xaf};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),4 },{ anchor_1,sizeof(anchor_1),4 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 2U,
    xx_autodesk_flic_search_open, xx_autodesk_flic_search_close, false
};

static xx_format_search_state *xx_autodesk_flic_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_autodesk_flic_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_autodesk_flic_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_autodesk_flic_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_autodesk_flic_extractor = {
    xx_autodesk_flic_create_format_search,
    xx_autodesk_flic_get_current_format_info,
    xx_autodesk_flic_format_search_find_next,
    xx_autodesk_flic_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(autodesk_flic, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
