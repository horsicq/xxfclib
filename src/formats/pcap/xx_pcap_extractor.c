/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_pcap_extractor.c - search raw data for pcap.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the pcap reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pcap/xx_pcap.h"

static const xx_file_type_t k_types[] = {XX_FILE_TYPE_PCAP};

static Abstractformat *xx_pcap_search_open(xx_io_device *window)
{
    xx_pcap *reader = xx_pcap_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_pcap_search_close(Abstractformat *format)
{
    /* The format is the first member, so this is the reader itself. */
    xx_pcap_free((xx_pcap *)format);
}

static const uint8_t anchor_0[] = {0xd4, 0xc3, 0xb2, 0xa1};
static const uint8_t anchor_1[] = {0xa1, 0xb2, 0xc3, 0xd4};
static const uint8_t anchor_2[] = {0x4d, 0x3c, 0xb2, 0xa1};
static const uint8_t anchor_3[] = {0xa1, 0xb2, 0x3c, 0x4d};
static const xx_format_search_anchor anchors[] = {
    {anchor_0, sizeof(anchor_0), 0},
    {anchor_1, sizeof(anchor_1), 0},
    {anchor_2, sizeof(anchor_2), 0},
    {anchor_3, sizeof(anchor_3), 0},
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]), anchors, sizeof(anchors) / sizeof(anchors[0]), xx_pcap_search_open, xx_pcap_search_close, false};

static xx_format_search_state *xx_pcap_create_format_search(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pcap_get_current_format_info(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pcap_format_search_find_next(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pcap_free_format_search(xx_format_extractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pcap_extractor = {xx_pcap_create_format_search, xx_pcap_get_current_format_info, xx_pcap_format_search_find_next, xx_pcap_free_format_search};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(pcap, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
