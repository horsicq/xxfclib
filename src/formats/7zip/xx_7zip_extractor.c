/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_7zip_extractor.c - search raw data for 7ZIP.
 *
 * Scans for:
 *   37 7A BC AF 27 1C at +0  ("7z..'.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the 7zip reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/7zip/xx_7zip.h"

static const uint8_t k_anchor0[] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_7ZIP };

static Abstractformat *xx_7zip_search_open(xx_io_device *window) {
    xx_7zip *reader = xx_7zip_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_7zip_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_7zip_free((xx_7zip *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_7zip_search_open, xx_7zip_search_close
};

static xx_format_search_state *xx_7zip_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_7zip_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_7zip_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_7zip_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_7zip_extractor = {
    xx_7zip_create_format_search,
    xx_7zip_get_current_format_info,
    xx_7zip_format_search_find_next,
    xx_7zip_free_format_search
};
