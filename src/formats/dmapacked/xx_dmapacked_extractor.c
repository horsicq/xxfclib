/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_dmapacked_extractor.c - search raw data for DMA PACKED.
 *
 * Scans for:
 *   50 41 4B 50 41 4B 00 2A at +26  ("PAKPAK..")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the dmapacked reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dmapacked/xx_dmapacked.h"

static const uint8_t k_anchor0[] = { 0x50, 0x41, 0x4B, 0x50, 0x41, 0x4B, 0x00, 0x2A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 26U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_DMA_PACKED };

static Abstractformat *xx_dmapacked_search_open(xx_io_device *window) {
    xx_dmapacked *reader = xx_dmapacked_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_dmapacked_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_dmapacked_free((xx_dmapacked *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_dmapacked_search_open, xx_dmapacked_search_close
};

static xx_format_search_state *xx_dmapacked_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_dmapacked_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_dmapacked_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_dmapacked_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_dmapacked_extractor = {
    xx_dmapacked_create_format_search,
    xx_dmapacked_get_current_format_info,
    xx_dmapacked_format_search_find_next,
    xx_dmapacked_free_format_search
};
