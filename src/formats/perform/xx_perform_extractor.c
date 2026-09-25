/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_perform_extractor.c - search raw data for PerFORM.
 *
 * Scans for:
 *   50 65 72 46 4F 52 4D 20 63 6F 6D 70 72 65 73 73 65 64 20 64 61 74 61 62 61 73 65 20 31 2E 30 30 20 00 at +0  ("PerFORM compressed database 1.00 .")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the perform reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/perform/xx_perform.h"

static const uint8_t k_anchor0[] = { 0x50, 0x65, 0x72, 0x46, 0x4F, 0x52, 0x4D, 0x20, 0x63, 0x6F, 0x6D, 0x70, 0x72, 0x65, 0x73, 0x73, 0x65, 0x64, 0x20, 0x64, 0x61, 0x74, 0x61, 0x62, 0x61, 0x73, 0x65, 0x20, 0x31, 0x2E, 0x30, 0x30, 0x20, 0x00 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PERFORM };

static Abstractformat *xx_perform_search_open(xx_io_device *window) {
    xx_perform *reader = xx_perform_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_perform_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_perform_free((xx_perform *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_perform_search_open, xx_perform_search_close
};

static xx_format_search_state *xx_perform_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_perform_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_perform_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_perform_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_perform_extractor = {
    xx_perform_create_format_search,
    xx_perform_get_current_format_info,
    xx_perform_format_search_find_next,
    xx_perform_free_format_search
};
