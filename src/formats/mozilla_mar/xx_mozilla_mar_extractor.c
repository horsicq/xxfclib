/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/mozilla_mar/xx_mozilla_mar.h"

static const xx_file_type_t mar_types[] = { XX_FILE_TYPE_MOZILLA_MAR };
static const uint8_t mar_magic[] = { 'M', 'A', 'R', '1' };
static const xx_format_search_anchor mar_anchors[] = {
    { mar_magic, sizeof(mar_magic), 0U }
};

static Abstractformat *mar_search_open(xx_io_device *window) {
    xx_mozilla_mar *archive = xx_mozilla_mar_create(window, 0);
    return archive ? &archive->format : NULL;
}

static void mar_search_close(Abstractformat *format) {
    xx_mozilla_mar_free((xx_mozilla_mar *)format);
}

static const xx_format_search_desc mar_desc = {
    mar_types, sizeof(mar_types) / sizeof(mar_types[0]),
    mar_anchors, sizeof(mar_anchors) / sizeof(mar_anchors[0]),
    mar_search_open, mar_search_close, false
};

static xx_format_search_state *mar_create_search(
    xx_format_extractor *self, xx_io_device *device,
    const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&mar_desc, device, options, pd);
}

static const xx_format_search_info *mar_current_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool mar_next_search(xx_format_extractor *self,
                            xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void mar_free_search(xx_format_extractor *self,
                            xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_mozilla_mar_extractor = {
    mar_create_search, mar_current_search, mar_next_search, mar_free_search
};
