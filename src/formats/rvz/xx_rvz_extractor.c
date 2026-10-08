/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rvz/xx_rvz.h"

static const uint8_t rvz_magic[] = {'R', 'V', 'Z', 1};
static const xx_format_search_anchor rvz_anchors[] = {
    {rvz_magic, sizeof(rvz_magic), 0U},
};
static const xx_file_type_t rvz_types[] = {XX_FILE_TYPE_RVZ};
static Abstractformat *rvz_open_window(xx_io_device *window) {
    xx_rvz *reader = xx_rvz_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void rvz_close_window(Abstractformat *format) {
    xx_rvz_free((xx_rvz *)format);
}
static const xx_format_search_desc rvz_desc = {
    rvz_types, sizeof(rvz_types) / sizeof(rvz_types[0]),
    rvz_anchors, sizeof(rvz_anchors) / sizeof(rvz_anchors[0]),
    rvz_open_window, rvz_close_window, false
};
static xx_format_search_state *rvz_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&rvz_desc, device, options, pd);
}
static const xx_format_search_info *rvz_current_search(xx_format_extractor *self,
                                                        xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool rvz_next_search(xx_format_extractor *self,
                            xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void rvz_free_search(xx_format_extractor *self,
                            xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_rvz_extractor = {
    rvz_create_search, rvz_current_search, rvz_next_search, rvz_free_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(rvz, rvz_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
