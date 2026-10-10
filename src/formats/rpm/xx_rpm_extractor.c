/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_rpm_extractor.c - raw-data search for RPM package.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rpm/xx_rpm.h"

static const uint8_t k_anchor0[] = {0xED, 0xAB, 0xEE, 0xDB};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 0U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_RPM};

static Abstractformat *xx_rpm_search_open(xx_io_device *window)
{
    xx_rpm *reader = xx_rpm_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_rpm_search_close(Abstractformat *format)
{
    xx_rpm_free((xx_rpm *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_rpm_search_open, xx_rpm_search_close, false};

static xx_format_search_state *xx_rpm_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_rpm_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_rpm_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_rpm_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_rpm_extractor = {xx_rpm_create_format_search, xx_rpm_get_current_format_info, xx_rpm_format_search_find_next, xx_rpm_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(rpm, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
