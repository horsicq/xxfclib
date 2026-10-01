/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/sfx_softpaq4/xx_sfx_softpaq4.h"

static const xx_file_type_t types[] = {XX_FILE_TYPE_SFX_SOFTPAQ4};
static const uint8_t mz[] = {'M', 'Z'};
static const xx_format_search_anchor anchors[] = {{mz, sizeof(mz), 0}};

static Abstractformat *open_reader(xx_io_device *device) {
    xx_sfx_softpaq4 *reader = xx_sfx_softpaq4_create(device, 0);
    return reader ? &reader->format : NULL;
}

static void close_reader(Abstractformat *format) {
    xx_sfx_softpaq4_free((xx_sfx_softpaq4 *)format);
}

static const xx_format_search_desc desc = {
    types, 1U, anchors, 1U, open_reader, close_reader
};

static xx_format_search_state *create_search(xx_format_extractor *self,
                                             xx_io_device *device,
                                             const xx_list_s *options,
                                             xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&desc, device, options, pd);
}

static const xx_format_search_info *current(xx_format_extractor *self,
                                            xx_format_search_state *state) {
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

xx_format_extractor xx_sfx_softpaq4_extractor = {
    create_search, current, next, free_search
};
