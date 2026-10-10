/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_rtpatch_setup_data_extractor.c - search raw data for RTPatch Setup volume.
 *
 * Scans for:
 *   B5 9C at +18
 *   B5 9C at +19
 *   B5 9C at +20
 *   B5 9C at +21
 *   B5 9C at +22
 *   B5 9C at +23
 *   B5 9C at +24
 *   B5 9C at +25
 *   B5 9C at +26
 *   B5 9C at +27
 *   B5 9C at +28
 *   B5 9C at +29
 * Each candidate must be accepted by the rtpatch_setup_data reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/rtpatch_setup_data/xx_rtpatch_setup_data.h"

static const uint8_t k_anchor0[] = {0xB5, 0x9C};
static const uint8_t k_anchor1[] = {0xB5, 0x9C};
static const uint8_t k_anchor2[] = {0xB5, 0x9C};
static const uint8_t k_anchor3[] = {0xB5, 0x9C};
static const uint8_t k_anchor4[] = {0xB5, 0x9C};
static const uint8_t k_anchor5[] = {0xB5, 0x9C};
static const uint8_t k_anchor6[] = {0xB5, 0x9C};
static const uint8_t k_anchor7[] = {0xB5, 0x9C};
static const uint8_t k_anchor8[] = {0xB5, 0x9C};
static const uint8_t k_anchor9[] = {0xB5, 0x9C};
static const uint8_t k_anchor10[] = {0xB5, 0x9C};
static const uint8_t k_anchor11[] = {0xB5, 0x9C};

static const xx_format_search_anchor k_anchors[] = {
    {k_anchor0, sizeof(k_anchor0), 18U}, {k_anchor1, sizeof(k_anchor1), 19U}, {k_anchor2, sizeof(k_anchor2), 20U},   {k_anchor3, sizeof(k_anchor3), 21U},
    {k_anchor4, sizeof(k_anchor4), 22U}, {k_anchor5, sizeof(k_anchor5), 23U}, {k_anchor6, sizeof(k_anchor6), 24U},   {k_anchor7, sizeof(k_anchor7), 25U},
    {k_anchor8, sizeof(k_anchor8), 26U}, {k_anchor9, sizeof(k_anchor9), 27U}, {k_anchor10, sizeof(k_anchor10), 28U}, {k_anchor11, sizeof(k_anchor11), 29U},
};

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_RTPATCH_SETUP_DATA};

static Abstractformat *xx_rtpatch_setup_data_search_open(xx_io_device *window)
{
    xx_rtpatch_setup_data *reader = xx_rtpatch_setup_data_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_rtpatch_setup_data_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_rtpatch_setup_data_free((xx_rtpatch_setup_data *)format);
}

static const xx_format_search_desc k_desc = {k_types,
                                             sizeof(k_types) / sizeof(k_types[0]),
                                             k_anchors,
                                             sizeof(k_anchors) / sizeof(k_anchors[0]),
                                             xx_rtpatch_setup_data_search_open,
                                             xx_rtpatch_setup_data_search_close,
                                             false};

static xx_format_search_state *xx_rtpatch_setup_data_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_rtpatch_setup_data_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_rtpatch_setup_data_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_rtpatch_setup_data_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_rtpatch_setup_data_extractor = {xx_rtpatch_setup_data_create_format_search, xx_rtpatch_setup_data_get_current_format_info,
                                                       xx_rtpatch_setup_data_format_search_find_next, xx_rtpatch_setup_data_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(rtpatch_setup_data, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
