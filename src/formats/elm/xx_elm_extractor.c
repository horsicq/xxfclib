/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/elm/xx_elm.h"

/* The dotted version is not a distinctive embedded signature.  A complete
 * directory and every payload marker must validate before offset 0 is used. */
static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ELM };

static Abstractformat *xx_elm_search_open(xx_io_device *window) {
    xx_elm *reader = xx_elm_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_elm_search_close(Abstractformat *format) {
    xx_elm_free((xx_elm *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U, xx_elm_search_open, xx_elm_search_close
};

static xx_format_search_state *xx_elm_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_elm_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_elm_format_search_find_next(xx_format_extractor *self,
                                            xx_format_search_state *state,
                                            xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_elm_free_format_search(xx_format_extractor *self,
                                       xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_elm_extractor = {
    xx_elm_create_format_search,
    xx_elm_get_current_format_info,
    xx_elm_format_search_find_next,
    xx_elm_free_format_search
};
