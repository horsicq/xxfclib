/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for F Install disk data.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/finstall/xx_finstall.h"

static const uint8_t k_anchor0[] = { 0x01, 0x46, 0x20, 0x49, 0x6E, 0x73, 0x74, 0x61, 0x6C, 0x6C, 0x20 };
static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_FINSTALL
};

static Abstractformat *xx_finstall_search_open(xx_io_device *window) {
    xx_finstall *reader = xx_finstall_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_finstall_search_close(Abstractformat *format) {
    xx_finstall_free((xx_finstall *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_finstall_search_open, xx_finstall_search_close
};

static xx_format_search_state *xx_finstall_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_finstall_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_finstall_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_finstall_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_finstall_extractor = {
    xx_finstall_create_format_search,
    xx_finstall_get_current_format_info,
    xx_finstall_format_search_find_next,
    xx_finstall_free_format_search
};
