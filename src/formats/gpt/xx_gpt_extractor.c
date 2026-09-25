/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gpt_extractor.c - search raw data for GPT.
 *
 * Scans for:
 *   45 46 49 20 50 41 52 54 at +512  ("EFI PART")
 * (hand-written: the detector establishes this signature outside its
 * 64-byte prefilter window)
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the gpt reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gpt/xx_gpt.h"

static const uint8_t k_anchor0[] = { 0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 512U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GPT };

static Abstractformat *xx_gpt_search_open(xx_io_device *window) {
    xx_gpt *reader = xx_gpt_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gpt_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gpt_free((xx_gpt *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_gpt_search_open, xx_gpt_search_close
};

static xx_format_search_state *xx_gpt_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gpt_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gpt_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gpt_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gpt_extractor = {
    xx_gpt_create_format_search,
    xx_gpt_get_current_format_info,
    xx_gpt_format_search_find_next,
    xx_gpt_free_format_search
};
