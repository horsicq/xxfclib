/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_zip_extractor.c - search raw data for ZIP.
 *
 * Scans for:
 *   50 4B 03 04 at +0  ("PK..")
 *   50 4B 05 06 at +0  ("PK..")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the zip reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/zip/xx_zip.h"

static const uint8_t k_anchor0[] = { 0x50, 0x4B, 0x03, 0x04 };
static const uint8_t k_anchor1[] = { 0x50, 0x4B, 0x05, 0x06 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ZIP, XX_FILE_TYPE_ZIP64 };

static Abstractformat *xx_zip_search_open(xx_io_device *window) {
    xx_zip *reader = xx_zip_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_zip_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_zip_free((xx_zip *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_zip_search_open, xx_zip_search_close
};

static xx_format_search_state *xx_zip_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_zip_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_zip_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_zip_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_zip_extractor = {
    xx_zip_create_format_search,
    xx_zip_get_current_format_info,
    xx_zip_format_search_find_next,
    xx_zip_free_format_search
};
