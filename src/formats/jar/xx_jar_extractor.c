/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_jar_extractor.c - search raw data for JAR.
 *
 * Scans for:
 *   50 4B 03 04 at +0  ("PK..")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the jar reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/jar/xx_jar.h"

static const uint8_t k_anchor0[] = { 0x50, 0x4B, 0x03, 0x04 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_JAR };

static Abstractformat *xx_jar_search_open(xx_io_device *window) {
    xx_jar *reader = xx_jar_create(window, 0);
    return reader ? &reader->zip.format : NULL;
}

static void xx_jar_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_jar_free((xx_jar *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_jar_search_open, xx_jar_search_close
};

static xx_format_search_state *xx_jar_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_jar_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_jar_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_jar_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_jar_extractor = {
    xx_jar_create_format_search,
    xx_jar_get_current_format_info,
    xx_jar_format_search_find_next,
    xx_jar_free_format_search
};
