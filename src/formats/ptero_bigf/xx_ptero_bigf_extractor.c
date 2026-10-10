/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ptero_bigf/xx_ptero_bigf.h"

static const uint8_t k_bigf[] = {'B', 'I', 'G', 'F'};
static const xx_format_search_anchor k_anchors[] = {{k_bigf, sizeof(k_bigf), 0U}};
static const xx_file_type_t k_types[] = {XX_FILE_TYPE_PTERO_BIGF};

static Abstractformat *xx_ptero_bigf_search_open(xx_io_device *window)
{
    xx_ptero_bigf *reader = xx_ptero_bigf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ptero_bigf_search_close(Abstractformat *format)
{
    xx_ptero_bigf_free((xx_ptero_bigf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]), xx_ptero_bigf_search_open, xx_ptero_bigf_search_close, true};

static xx_format_search_state *xx_ptero_bigf_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ptero_bigf_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ptero_bigf_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ptero_bigf_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ptero_bigf_extractor = {xx_ptero_bigf_create_format_search, xx_ptero_bigf_get_current_format_info, xx_ptero_bigf_format_search_find_next,
                                               xx_ptero_bigf_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(ptero_bigf, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
