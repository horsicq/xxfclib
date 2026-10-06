/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* NeXT's 46-byte diskimage wrapper has no magic. The search engine probes
 * offset zero only; the reader cross-checks all redundant geometry fields. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nextstep_diskimage/xx_nextstep_diskimage.h"

static const xx_file_type_t nd_types[] = {
    XX_FILE_TYPE_NEXTSTEP_DISKIMAGE
};
static Abstractformat *nd_open(xx_io_device *window) {
    xx_nextstep_diskimage *reader =
        xx_nextstep_diskimage_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void nd_close(Abstractformat *format) {
    xx_nextstep_diskimage_free((xx_nextstep_diskimage *)format);
}
static const xx_format_search_desc nd_search = {
    nd_types, sizeof(nd_types) / sizeof(nd_types[0]),
    NULL, 0U, nd_open, nd_close, false
};
static xx_format_search_state *nd_create_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&nd_search, device, options, pd);
}
static const xx_format_search_info *nd_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool nd_next(xx_format_extractor *self,
                    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void nd_free_search(xx_format_extractor *self,
                           xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_nextstep_diskimage_extractor = {
    nd_create_search, nd_current, nd_next, nd_free_search
};
