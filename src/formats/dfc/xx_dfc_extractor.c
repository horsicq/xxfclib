/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DFC has no fixed magic, so only offset zero is considered.
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/dfc/xx_dfc.h"

#ifdef DFC
#define DFC_EXTRACTOR_TYPE XX_FILE_TYPE_DFC
#else
#define DFC_EXTRACTOR_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const xx_file_type_t dfc_types[] = {DFC_EXTRACTOR_TYPE};
static Abstractformat *dfc_open(xx_io_device *window) {
    xx_dfc *archive = xx_dfc_create(window, 0);
    return archive ? &archive->format : NULL;
}
static void dfc_close(Abstractformat *format) {
    xx_dfc_free((xx_dfc *)format);
}
static const xx_format_search_desc dfc_desc = {
    dfc_types, 1U, NULL, 0U, dfc_open, dfc_close, false
};
static xx_format_search_state *dfc_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&dfc_desc, device, options, pd);
}
static const xx_format_search_info *dfc_current(xx_format_extractor *self,
                                                xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool dfc_find_next(xx_format_extractor *self,
                          xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void dfc_free_search(xx_format_extractor *self,
                            xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_dfc_extractor = {
    dfc_create_search, dfc_current, dfc_find_next, dfc_free_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(dfc, dfc_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
