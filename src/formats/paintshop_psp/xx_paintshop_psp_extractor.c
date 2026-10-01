/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_paintshop_psp_extractor.c - search raw data for paintshop_psp.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the paintshop_psp reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/paintshop_psp/xx_paintshop_psp.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PAINTSHOP_PSP };

static Abstractformat *xx_paintshop_psp_search_open(xx_io_device *window) {
    xx_paintshop_psp *reader = xx_paintshop_psp_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_paintshop_psp_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_paintshop_psp_free((xx_paintshop_psp *)format);
}

static const uint8_t anchor_bytes[] = {0x50,0x61,0x69,0x6e,0x74,0x20,0x53,0x68,0x6f,0x70,0x20,0x50,0x72,0x6f,0x20,0x49,0x6d,0x61,0x67,0x65,0x20,0x46,0x69,0x6c,0x65,0x0a,0x1a};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_paintshop_psp_search_open, xx_paintshop_psp_search_close
};

static xx_format_search_state *xx_paintshop_psp_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_paintshop_psp_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_paintshop_psp_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_paintshop_psp_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_paintshop_psp_extractor = {
    xx_paintshop_psp_create_format_search,
    xx_paintshop_psp_get_current_format_info,
    xx_paintshop_psp_format_search_find_next,
    xx_paintshop_psp_free_format_search
};
