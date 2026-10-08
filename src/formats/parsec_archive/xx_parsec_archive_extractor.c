/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/parsec_archive/xx_parsec_archive.h"

static const xx_file_type_t types[] = {XX_FILE_TYPE_PARSEC_ARCHIVE};
static Abstractformat *open_reader(xx_io_device *device) {
    xx_parsec_archive *reader = xx_parsec_archive_create(device, 0);
    return reader ? &reader->format : NULL;
}
static void close_reader(Abstractformat *format) {
    xx_parsec_archive_free((xx_parsec_archive *)format);
}
/* No fixed signature: the offset table is validated at candidate offset 0. */
static const xx_format_search_desc desc = {
    types, 1U, NULL, 0U, open_reader, close_reader, false
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
xx_format_extractor xx_parsec_archive_extractor = {
    create_search, current, next, free_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(parsec_archive, desc)
/* END GENERATED ABSTRACT EXTRACTOR */
