/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gpgsigned_extractor.c - raw-data search for OpenPGP ZIP-compressed packet.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gpgsigned/xx_gpgsigned.h"

static const uint8_t k_anchor0[] = {0xA3, 0x01};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_GPG_SIGNED};

static Abstractformat *xx_gpgsigned_search_open(xx_io_device *window)
{
    xx_gpgsigned *reader = xx_gpgsigned_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gpgsigned_search_close(Abstractformat *format)
{
    xx_gpgsigned_free((xx_gpgsigned *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_gpgsigned_search_open, xx_gpgsigned_search_close, false};

static xx_format_search_state *xx_gpgsigned_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gpgsigned_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gpgsigned_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gpgsigned_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gpgsigned_extractor = {xx_gpgsigned_create_format_search, xx_gpgsigned_get_current_format_info, xx_gpgsigned_format_search_find_next,
                                              xx_gpgsigned_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(gpgsigned, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
