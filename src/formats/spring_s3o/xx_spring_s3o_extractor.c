/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_spring_s3o_extractor.c - search raw data for spring_s3o.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the spring_s3o reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/spring_s3o/xx_spring_s3o.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SPRING_S3O };

static Abstractformat *xx_spring_s3o_search_open(xx_io_device *window) {
    xx_spring_s3o *reader = xx_spring_s3o_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_spring_s3o_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_spring_s3o_free((xx_spring_s3o *)format);
}

static const uint8_t anchor_bytes[] = {0x53,0x70,0x72,0x69,0x6e,0x67,0x20,0x75,0x6e,0x69,0x74,0x00};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_spring_s3o_search_open, xx_spring_s3o_search_close, false
};

static xx_format_search_state *xx_spring_s3o_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_spring_s3o_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_spring_s3o_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_spring_s3o_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_spring_s3o_extractor = {
    xx_spring_s3o_create_format_search,
    xx_spring_s3o_get_current_format_info,
    xx_spring_s3o_format_search_find_next,
    xx_spring_s3o_free_format_search
};
