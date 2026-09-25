/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_lingvoarc_extractor.c - search raw data for LingvoArc.
 *
 * Scans for:
 *   6C 69 6E 67 76 6F 41 72 63 31 00 FD 00 DF 00 FF at +0  ("lingvoArc1......")
 *   6C 69 6E 67 76 6F 41 72 63 32 00 FD 00 DF 00 FF at +0  ("lingvoArc2......")
 *   4C 69 6E 67 76 6F 41 72 63 68 01 00 F0 1F 00 01 40 00 47 01 47 01 1F 83 41 01 at +0  ("LingvoArch......@.G.G...A.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the lingvoarc reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/lingvoarc/xx_lingvoarc.h"

static const uint8_t k_anchor0[] = { 0x6C, 0x69, 0x6E, 0x67, 0x76, 0x6F, 0x41, 0x72, 0x63, 0x31, 0x00, 0xFD, 0x00, 0xDF, 0x00, 0xFF };
static const uint8_t k_anchor1[] = { 0x6C, 0x69, 0x6E, 0x67, 0x76, 0x6F, 0x41, 0x72, 0x63, 0x32, 0x00, 0xFD, 0x00, 0xDF, 0x00, 0xFF };
static const uint8_t k_anchor2[] = { 0x4C, 0x69, 0x6E, 0x67, 0x76, 0x6F, 0x41, 0x72, 0x63, 0x68, 0x01, 0x00, 0xF0, 0x1F, 0x00, 0x01, 0x40, 0x00, 0x47, 0x01, 0x47, 0x01, 0x1F, 0x83, 0x41, 0x01 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LINGVOARC };

static Abstractformat *xx_lingvoarc_search_open(xx_io_device *window) {
    xx_lingvoarc *reader = xx_lingvoarc_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_lingvoarc_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_lingvoarc_free((xx_lingvoarc *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_lingvoarc_search_open, xx_lingvoarc_search_close
};

static xx_format_search_state *xx_lingvoarc_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_lingvoarc_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_lingvoarc_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_lingvoarc_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_lingvoarc_extractor = {
    xx_lingvoarc_create_format_search,
    xx_lingvoarc_get_current_format_info,
    xx_lingvoarc_format_search_find_next,
    xx_lingvoarc_free_format_search
};
