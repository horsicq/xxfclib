/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_packit_extractor.c - search raw data for PACKIT.
 *
 * Scans for:
 *   50 41 43 4B 49 54 20 62 79 20 4D 4A 50 0D 0A 1A at +0  ("PACKIT by MJP...")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the packit reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/packit/xx_packit.h"

static const uint8_t k_anchor0[] = { 0x50, 0x41, 0x43, 0x4B, 0x49, 0x54, 0x20, 0x62, 0x79, 0x20, 0x4D, 0x4A, 0x50, 0x0D, 0x0A, 0x1A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PACKIT };

static Abstractformat *xx_packit_search_open(xx_io_device *window) {
    xx_packit *reader = xx_packit_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_packit_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_packit_free((xx_packit *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_packit_search_open, xx_packit_search_close
};

static xx_format_search_state *xx_packit_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_packit_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_packit_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_packit_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_packit_extractor = {
    xx_packit_create_format_search,
    xx_packit_get_current_format_info,
    xx_packit_format_search_find_next,
    xx_packit_free_format_search
};
