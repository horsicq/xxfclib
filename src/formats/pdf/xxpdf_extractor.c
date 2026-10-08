/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Raw-data search for PDF documents. The native reader validates each
 * signature candidate and measures the complete document before it is
 * returned by the shared search engine. */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pdf/xxpdf.h"

static const uint8_t k_anchor0[] = { '%', 'P', 'D', 'F', '-' };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PDF };

static Abstractformat *xx_pdf_search_open(xx_io_device *window) {
    xx_pdf *reader = xx_pdf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_pdf_search_close(Abstractformat *format) {
    xx_pdf_free((xx_pdf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_pdf_search_open, xx_pdf_search_close, false
};

static xx_format_search_state *xx_pdf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pdf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pdf_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pdf_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pdf_extractor = {
    xx_pdf_create_format_search,
    xx_pdf_get_current_format_info,
    xx_pdf_format_search_find_next,
    xx_pdf_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pdf, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
