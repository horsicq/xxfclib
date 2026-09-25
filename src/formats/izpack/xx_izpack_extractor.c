/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_izpack_extractor.c - search raw data for IzPack.
 *
 * Scans for:
 *   73 72 00 1B 63 6F 6D 2E 69 7A 66 6F 72 67 65 2E 69 7A 70 61 63 6B 2E 50 61 63 6B 46 69 6C 65 at +10  ("sr..com.izforge.izpack.PackFile")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the izpack reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/izpack/xx_izpack.h"

static const uint8_t k_anchor0[] = { 0x73, 0x72, 0x00, 0x1B, 0x63, 0x6F, 0x6D, 0x2E, 0x69, 0x7A, 0x66, 0x6F, 0x72, 0x67, 0x65, 0x2E, 0x69, 0x7A, 0x70, 0x61, 0x63, 0x6B, 0x2E, 0x50, 0x61, 0x63, 0x6B, 0x46, 0x69, 0x6C, 0x65 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 10U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_IZPACK };

static Abstractformat *xx_izpack_search_open(xx_io_device *window) {
    xx_izpack *reader = xx_izpack_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_izpack_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_izpack_free((xx_izpack *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_izpack_search_open, xx_izpack_search_close
};

static xx_format_search_state *xx_izpack_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_izpack_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_izpack_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_izpack_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_izpack_extractor = {
    xx_izpack_create_format_search,
    xx_izpack_get_current_format_info,
    xx_izpack_format_search_find_next,
    xx_izpack_free_format_search
};
