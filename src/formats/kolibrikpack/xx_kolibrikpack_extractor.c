/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_kolibrikpack_extractor.c - search raw data for KOLIBRIKPACK.
 *
 * Scans for:
 *   4B 50 43 4B at +0  ("KPCK")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the kolibrikpack reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/kolibrikpack/xx_kolibrikpack.h"

static const uint8_t k_anchor0[] = { 0x4B, 0x50, 0x43, 0x4B };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_KOLIBRIKPACK };

static Abstractformat *xx_kolibrikpack_search_open(xx_io_device *window) {
    xx_kolibrikpack *reader = xx_kolibrikpack_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_kolibrikpack_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_kolibrikpack_free((xx_kolibrikpack *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_kolibrikpack_search_open, xx_kolibrikpack_search_close
};

static xx_format_search_state *xx_kolibrikpack_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_kolibrikpack_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_kolibrikpack_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_kolibrikpack_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_kolibrikpack_extractor = {
    xx_kolibrikpack_create_format_search,
    xx_kolibrikpack_get_current_format_info,
    xx_kolibrikpack_format_search_find_next,
    xx_kolibrikpack_free_format_search
};
