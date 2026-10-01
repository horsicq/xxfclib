/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_step_part21_extractor.c - search raw data for step_part21.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the step_part21 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/step_part21/xx_step_part21.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_STEP_PART21 };

static Abstractformat *xx_step_part21_search_open(xx_io_device *window) {
    xx_step_part21 *reader = xx_step_part21_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_step_part21_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_step_part21_free((xx_step_part21 *)format);
}

static const uint8_t anchor_bytes[] = {0x49,0x53,0x4f,0x2d,0x31,0x30,0x33,0x30,0x33,0x2d,0x32,0x31,0x3b};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_step_part21_search_open, xx_step_part21_search_close
};

static xx_format_search_state *xx_step_part21_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_step_part21_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_step_part21_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_step_part21_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_step_part21_extractor = {
    xx_step_part21_create_format_search,
    xx_step_part21_get_current_format_info,
    xx_step_part21_format_search_find_next,
    xx_step_part21_free_format_search
};
