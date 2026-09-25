/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_uefi_fv_extractor.c - search raw data for UEFI firmware volume.
 *
 * Scans for:
 *   5F 46 56 48 at +40  ("_FVH")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the uefi_fv reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/uefi_fv/xx_uefi_fv.h"

static const uint8_t k_anchor0[] = { 0x5F, 0x46, 0x56, 0x48 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 40U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_UEFI_FV };

static Abstractformat *xx_uefi_fv_search_open(xx_io_device *window) {
    xx_uefi_fv *reader = xx_uefi_fv_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_uefi_fv_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_uefi_fv_free((xx_uefi_fv *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_uefi_fv_search_open, xx_uefi_fv_search_close
};

static xx_format_search_state *xx_uefi_fv_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_uefi_fv_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_uefi_fv_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_uefi_fv_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_uefi_fv_extractor = {
    xx_uefi_fv_create_format_search,
    xx_uefi_fv_get_current_format_info,
    xx_uefi_fv_format_search_find_next,
    xx_uefi_fv_free_format_search
};
