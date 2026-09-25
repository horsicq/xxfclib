/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_borlandpack_extractor.c - search raw data for BORLANDPACK.
 *
 * Scans for:
 *   54 68 69 73 20 69 73 20 61 20 70 61 63 6B 65 64 20 66 69 6C 65 2E 1A at +0  ("This is a packed file..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the borlandpack reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/borlandpack/xx_borlandpack.h"

static const uint8_t k_anchor0[] = { 0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x70, 0x61, 0x63, 0x6B, 0x65, 0x64, 0x20, 0x66, 0x69, 0x6C, 0x65, 0x2E, 0x1A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_BORLANDPACK };

static Abstractformat *xx_borlandpack_search_open(xx_io_device *window) {
    xx_borlandpack *reader = xx_borlandpack_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_borlandpack_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_borlandpack_free((xx_borlandpack *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_borlandpack_search_open, xx_borlandpack_search_close
};

static xx_format_search_state *xx_borlandpack_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_borlandpack_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_borlandpack_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_borlandpack_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_borlandpack_extractor = {
    xx_borlandpack_create_format_search,
    xx_borlandpack_get_current_format_info,
    xx_borlandpack_format_search_find_next,
    xx_borlandpack_free_format_search
};
