/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_winlink_extractor.c - search raw data for WinLink.
 *
 * The format has no fixed signature the detector checks in its first
 * 64 bytes, so it cannot be recognised inside other data: the search
 * tries offset 0 only.
 * Each candidate must be accepted by the winlink reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/winlink/xx_winlink.h"


static const xx_file_type_t k_types[] = { XX_FILE_TYPE_WINLINK };

static Abstractformat *xx_winlink_search_open(xx_io_device *window) {
    xx_winlink *reader = xx_winlink_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_winlink_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_winlink_free((xx_winlink *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_winlink_search_open, xx_winlink_search_close
};

static xx_format_search_state *xx_winlink_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_winlink_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_winlink_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_winlink_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_winlink_extractor = {
    xx_winlink_create_format_search,
    xx_winlink_get_current_format_info,
    xx_winlink_format_search_find_next,
    xx_winlink_free_format_search
};
