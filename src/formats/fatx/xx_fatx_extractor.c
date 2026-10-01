/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/fatx/xx_fatx.h"

static const xx_file_type_t fatx_types[] = { XX_FILE_TYPE_FATX };
static const uint8_t fatx_magic[] = { 'F', 'A', 'T', 'X' };
static const xx_format_search_anchor fatx_anchors[] = {
    { fatx_magic, sizeof(fatx_magic), 0 }
};

static Abstractformat *fatx_search_open(xx_io_device *window) {
    xx_fatx *reader = xx_fatx_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void fatx_search_close(Abstractformat *format) {
    xx_fatx_free((xx_fatx *)format);
}
static const xx_format_search_desc fatx_desc = {
    fatx_types, sizeof(fatx_types) / sizeof(fatx_types[0]),
    fatx_anchors, sizeof(fatx_anchors) / sizeof(fatx_anchors[0]),
    fatx_search_open, fatx_search_close
};
static xx_format_search_state *fatx_search_create(
    xx_format_extractor *self, xx_io_device *device,
    const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&fatx_desc, device, options, pd);
}
static const xx_format_search_info *fatx_search_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool fatx_search_next(xx_format_extractor *self,
                             xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void fatx_search_free(xx_format_extractor *self,
                             xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_fatx_extractor = {
    fatx_search_create, fatx_search_current, fatx_search_next, fatx_search_free
};
