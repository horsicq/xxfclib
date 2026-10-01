/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_westwood_vqa_extractor.c - search raw data for westwood_vqa.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the westwood_vqa reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/westwood_vqa/xx_westwood_vqa.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_WESTWOOD_VQA };

static Abstractformat *xx_westwood_vqa_search_open(xx_io_device *window) {
    xx_westwood_vqa *reader = xx_westwood_vqa_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_westwood_vqa_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_westwood_vqa_free((xx_westwood_vqa *)format);
}

static const uint8_t anchor_0[] = {0x57,0x56,0x51,0x41};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),8 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_westwood_vqa_search_open, xx_westwood_vqa_search_close
};

static xx_format_search_state *xx_westwood_vqa_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_westwood_vqa_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_westwood_vqa_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_westwood_vqa_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_westwood_vqa_extractor = {
    xx_westwood_vqa_create_format_search,
    xx_westwood_vqa_get_current_format_info,
    xx_westwood_vqa_format_search_find_next,
    xx_westwood_vqa_free_format_search
};
