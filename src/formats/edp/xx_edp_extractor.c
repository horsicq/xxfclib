/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/edp/xx_edp.h"

static const uint8_t ce_magic[] = {'.', 'E', 'D', 'P', 0, 1};
static const xx_format_search_anchor ce_anchors[] = {
    {ce_magic, sizeof(ce_magic), 0U},
};
static const xx_file_type_t ce_types[] = {XX_FILE_TYPE_EDP};
static Abstractformat *ce_open(xx_io_device *window) {
    xx_edp *reader = xx_edp_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void ce_close(Abstractformat *format) { xx_edp_free((xx_edp *)format); }
static const xx_format_search_desc ce_desc = {
    ce_types, sizeof(ce_types) / sizeof(ce_types[0]),
    ce_anchors, sizeof(ce_anchors) / sizeof(ce_anchors[0]),
    ce_open, ce_close, false
};
static xx_format_search_state *ce_create(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&ce_desc, device, options, pd);
}
static const xx_format_search_info *ce_current(xx_format_extractor *self,
                                                xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool ce_next(xx_format_extractor *self, xx_format_search_state *state,
                    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void ce_free(xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_edp_extractor = {ce_create, ce_current, ce_next, ce_free};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(edp, ce_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
