/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_encrypted_apple_disk_image_extractor.c - search raw data for ENCRYPTED_APPLE_DISK_IMAGE.
 *
 * Candidates are the fixed bytes 656E637263647361 at +0.
 * Each candidate must be accepted by the encrypted_apple_disk_image reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/encrypted_apple_disk_image/xx_encrypted_apple_disk_image.h"


static const uint8_t k_anchor0[] = { 0x65, 0x6E, 0x63, 0x72, 0x63, 0x64, 0x73, 0x61 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_ENCRYPTED_APPLE_DISK_IMAGE };

static Abstractformat *xx_encrypted_apple_disk_image_search_open(xx_io_device *window) {
    xx_encrypted_apple_disk_image *reader = xx_encrypted_apple_disk_image_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_encrypted_apple_disk_image_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_encrypted_apple_disk_image_free((xx_encrypted_apple_disk_image *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_encrypted_apple_disk_image_search_open, xx_encrypted_apple_disk_image_search_close, false
};

static xx_format_search_state *xx_encrypted_apple_disk_image_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_encrypted_apple_disk_image_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_encrypted_apple_disk_image_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_encrypted_apple_disk_image_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_encrypted_apple_disk_image_extractor = {
    xx_encrypted_apple_disk_image_create_format_search,
    xx_encrypted_apple_disk_image_get_current_format_info,
    xx_encrypted_apple_disk_image_format_search_find_next,
    xx_encrypted_apple_disk_image_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(encrypted_apple_disk_image, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
