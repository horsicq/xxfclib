/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ivf/xx_ivf.h"

static const xx_file_type_t types[] = {XX_FILE_TYPE_IVF};
static const uint8_t signature[] = {'D', 'K', 'I', 'F', 0, 0, 32, 0};
static const xx_format_search_anchor anchors[] = {
    {signature, sizeof(signature), 0}
};

static Abstractformat *open_reader(xx_io_device *device) {
    xx_ivf *reader = xx_ivf_create(device, 0);
    return reader ? &reader->format : NULL;
}

static void close_reader(Abstractformat *format) {
    xx_ivf_free((xx_ivf *)format);
}

static const xx_format_search_desc desc = {
    types, 1U, anchors, 1U, open_reader, close_reader
};

static xx_format_search_state *create_search(
    xx_format_extractor *self, xx_io_device *device,
    const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&desc, device, options, pd);
}

static const xx_format_search_info *current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool next(xx_format_extractor *self, xx_format_search_state *state,
                 xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void free_search(xx_format_extractor *self,
                        xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ivf_extractor = {
    create_search, current, next, free_search
};
