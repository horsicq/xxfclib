/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/parsec_pmm/xx_parsec_pmm.h"

static const xx_file_type_t types[] = {XX_FILE_TYPE_PARSEC_PMM};
static const uint8_t magic[] = {'M', 'T', 'C', 'V', 'T', 'S', ' ', 'P', 'S', 'M', ' ', '2', '.', '0', '0'};
static const xx_format_search_anchor anchors[] = {{magic, sizeof(magic), 0U}};
static Abstractformat *open_reader(xx_io_device *device) {
    xx_parsec_pmm *reader = xx_parsec_pmm_create(device, 0);
    return reader ? &reader->format : NULL;
}
static void close_reader(Abstractformat *format) {
    xx_parsec_pmm_free((xx_parsec_pmm *)format);
}
static const xx_format_search_desc desc = {
    types, 1U, anchors, 1U, open_reader, close_reader, false
};
static xx_format_search_state *create_search(xx_format_extractor *self,
        xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd) {
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
xx_format_extractor xx_parsec_pmm_extractor = {
    create_search, current, next, free_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(parsec_pmm, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
