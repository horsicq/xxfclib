/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_avro_object_extractor.c - search raw data for avro_object.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the avro_object reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/avro_object/xx_avro_object.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_AVRO_OBJECT };

static Abstractformat *xx_avro_object_search_open(xx_io_device *window) {
    xx_avro_object *reader = xx_avro_object_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_avro_object_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_avro_object_free((xx_avro_object *)format);
}

static const uint8_t anchor_0[] = {0x4f,0x62,0x6a,0x01};
static const xx_format_search_anchor anchors[] = {
    { anchor_0,sizeof(anchor_0),0 },
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, sizeof(anchors)/sizeof(anchors[0]),
    xx_avro_object_search_open, xx_avro_object_search_close
};

static xx_format_search_state *xx_avro_object_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_avro_object_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_avro_object_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_avro_object_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_avro_object_extractor = {
    xx_avro_object_create_format_search,
    xx_avro_object_get_current_format_info,
    xx_avro_object_format_search_find_next,
    xx_avro_object_free_format_search
};
