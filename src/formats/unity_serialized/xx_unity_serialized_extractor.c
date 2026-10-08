/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_unity_serialized_extractor.c - search raw data for unity_serialized.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the unity_serialized reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/unity_serialized/xx_unity_serialized.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_UNITY_SERIALIZED };

static Abstractformat *xx_unity_serialized_search_open(xx_io_device *window) {
    xx_unity_serialized *reader = xx_unity_serialized_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_unity_serialized_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_unity_serialized_free((xx_unity_serialized *)format);
}

static const uint8_t anchor_bytes[] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x00};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_unity_serialized_search_open, xx_unity_serialized_search_close, false
};

static xx_format_search_state *xx_unity_serialized_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_unity_serialized_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_unity_serialized_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_unity_serialized_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_unity_serialized_extractor = {
    xx_unity_serialized_create_format_search,
    xx_unity_serialized_get_current_format_info,
    xx_unity_serialized_format_search_find_next,
    xx_unity_serialized_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(unity_serialized, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
