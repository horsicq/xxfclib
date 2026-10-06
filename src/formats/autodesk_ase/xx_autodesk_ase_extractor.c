/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_autodesk_ase_extractor.c - search raw data for autodesk_ase.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the autodesk_ase reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/autodesk_ase/xx_autodesk_ase.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_AUTODESK_ASE };

static Abstractformat *xx_autodesk_ase_search_open(xx_io_device *window) {
    xx_autodesk_ase *reader = xx_autodesk_ase_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_autodesk_ase_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_autodesk_ase_free((xx_autodesk_ase *)format);
}

static const uint8_t anchor_bytes[] = {0x2a,0x33,0x44,0x53,0x4d,0x41,0x58,0x5f,0x41,0x53,0x43,0x49,0x49,0x45,0x58,0x50,0x4f,0x52,0x54};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_autodesk_ase_search_open, xx_autodesk_ase_search_close, false
};

static xx_format_search_state *xx_autodesk_ase_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_autodesk_ase_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_autodesk_ase_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_autodesk_ase_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_autodesk_ase_extractor = {
    xx_autodesk_ase_create_format_search,
    xx_autodesk_ase_get_current_format_info,
    xx_autodesk_ase_format_search_find_next,
    xx_autodesk_ase_free_format_search
};
