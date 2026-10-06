/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_adf_extractor.c - search raw data for Amiga ADF.
 *
 * Scans for:
 *   44 4F 53 00 .. 44 4F 53 07 ("DOS" + flags) at +0
 * Offset 0 is always tried as well, which is where a floppy whose boot
 * blocks are blank (no "DOS" tag) is accepted: only as an exact DD or HD
 * image.  A root-block anchor (+0x6E000) was measured and dropped: an anchor
 * that deep costs a seek and read per scanned byte in this engine.
 * Each candidate must be accepted by the adf reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/adf/xx_adf.h"

static const uint8_t k_dos0[] = { 0x44, 0x4F, 0x53, 0x00 };
static const uint8_t k_dos1[] = { 0x44, 0x4F, 0x53, 0x01 };
static const uint8_t k_dos2[] = { 0x44, 0x4F, 0x53, 0x02 };
static const uint8_t k_dos3[] = { 0x44, 0x4F, 0x53, 0x03 };
static const uint8_t k_dos4[] = { 0x44, 0x4F, 0x53, 0x04 };
static const uint8_t k_dos5[] = { 0x44, 0x4F, 0x53, 0x05 };
static const uint8_t k_dos6[] = { 0x44, 0x4F, 0x53, 0x06 };
static const uint8_t k_dos7[] = { 0x44, 0x4F, 0x53, 0x07 };

static const xx_format_search_anchor k_anchors[] = {
    { k_dos0, sizeof(k_dos0), 0U },
    { k_dos1, sizeof(k_dos1), 0U },
    { k_dos2, sizeof(k_dos2), 0U },
    { k_dos3, sizeof(k_dos3), 0U },
    { k_dos4, sizeof(k_dos4), 0U },
    { k_dos5, sizeof(k_dos5), 0U },
    { k_dos6, sizeof(k_dos6), 0U },
    { k_dos7, sizeof(k_dos7), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ADF };

static Abstractformat *xx_adf_search_open(xx_io_device *window) {
    xx_adf *reader = xx_adf_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_adf_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_adf_free((xx_adf *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_adf_search_open, xx_adf_search_close, false
};

static xx_format_search_state *xx_adf_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_adf_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_adf_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_adf_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_adf_extractor = {
    xx_adf_create_format_search,
    xx_adf_get_current_format_info,
    xx_adf_format_search_find_next,
    xx_adf_free_format_search
};
