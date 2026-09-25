/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_udf_extractor.c - search raw data for UDF.
 *
 * Scans for:
 *   00 42 45 41 30 31 at +32768  (".BEA01")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the udf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/udf/xx_udf.h"

static const uint8_t k_anchor0[] = { 0x00, 0x42, 0x45, 0x41, 0x30, 0x31 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 32768U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_UDF };

static Abstractformat *xx_udf_search_open(xx_io_device *window) {
    xx_udf *reader = xx_udf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_udf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_udf_free((xx_udf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_udf_search_open, xx_udf_search_close
};

static xx_format_search_state *xx_udf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_udf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_udf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_udf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_udf_extractor = {
    xx_udf_create_format_search,
    xx_udf_get_current_format_info,
    xx_udf_format_search_find_next,
    xx_udf_free_format_search
};
