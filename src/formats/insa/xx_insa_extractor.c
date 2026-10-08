/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/insa/xx_insa.h"

#ifdef INSA
#define INSA_EXTRACTOR_TYPE XX_FILE_TYPE_INSA
#else
#define INSA_EXTRACTOR_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const uint8_t insa_magic[] = {0x01, 0x00};
static const xx_format_search_anchor insa_anchors[] = {
    {insa_magic, sizeof(insa_magic), 0U}
};
static const xx_file_type_t insa_types[] = {INSA_EXTRACTOR_TYPE};
static Abstractformat *insa_open(xx_io_device *window) {
    xx_insa *archive = xx_insa_create(window, 0);
    return archive ? &archive->format : NULL;
}
static void insa_close(Abstractformat *format) {
    xx_insa_free((xx_insa *)format);
}
static const xx_format_search_desc insa_desc = {
    insa_types, 1U, insa_anchors, 1U, insa_open, insa_close, false
};
static xx_format_search_state *insa_create_search(xx_format_extractor *self,
    xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&insa_desc, device, options, pd);
}
static const xx_format_search_info *insa_current(xx_format_extractor *self,
                                                 xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool insa_find_next(xx_format_extractor *self,
                           xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void insa_free_search(xx_format_extractor *self,
                             xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_insa_extractor = {
    insa_create_search, insa_current, insa_find_next, insa_free_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(insa, insa_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
