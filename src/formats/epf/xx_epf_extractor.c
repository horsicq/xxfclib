/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/epf/xx_epf.h"

#ifdef EPF
#define EPF_EXTRACTOR_TYPE XX_FILE_TYPE_EPF
#else
#define EPF_EXTRACTOR_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const uint8_t epf_magic[] = {'E', 'P', 'F', 'S'};
static const xx_format_search_anchor epf_anchors[] = {
    {epf_magic, sizeof(epf_magic), 0U}
};
static const xx_file_type_t epf_types[] = {EPF_EXTRACTOR_TYPE};

static Abstractformat *epf_open(xx_io_device *window) {
    xx_epf *archive = xx_epf_create(window, 0);
    return archive ? &archive->format : NULL;
}
static void epf_close(Abstractformat *format) {
    xx_epf_free((xx_epf *)format);
}
static const xx_format_search_desc epf_desc = {
    epf_types, 1U, epf_anchors, 1U, epf_open, epf_close
};
static xx_format_search_state *epf_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&epf_desc, device, options, pd);
}
static const xx_format_search_info *epf_current(xx_format_extractor *self,
                                                xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool epf_find_next(xx_format_extractor *self,
                          xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void epf_free_search(xx_format_extractor *self,
                            xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_epf_extractor = {
    epf_create_search, epf_current, epf_find_next, epf_free_search
};
