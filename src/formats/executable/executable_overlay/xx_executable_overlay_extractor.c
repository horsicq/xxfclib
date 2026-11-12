/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Registration for an explicit/deep unpacking interpretation.
 */
#include "xxfclib/formats/executable/executable_overlay/xx_executable_overlay.h"
#include "../../xx_format_extractor_engine.h"

/* No physical signature: detection keeps the outer PE/NE/MSDOS identity. */
static bool xx_overlay_no_physical_signature(xx_io_device *device, int64_t base, bool mapped)
{
    (void)device; (void)base; (void)mapped;
    return false;
}
static xx_file_type_t xx_overlay_reader_type(xx_io_device *device, int64_t base, bool mapped)
{
    (void)device; (void)base; (void)mapped;
    return XX_FILE_TYPE_EXECUTABLE_OVERLAY;
}
static int xx_overlay_structural_signature(xx_io_device *device, int64_t base, bool mapped)
{
    (void)device; (void)base; (void)mapped;
    return -1;
}
static Abstractformat *xx_overlay_open(xx_io_device *device)
{
    return (Abstractformat *)xx_executable_overlay_new(device);
}
static void xx_overlay_close(Abstractformat *format)
{
    xx_executable_overlay_free((xx_executable_overlay *)format);
}
static const xx_file_type_t xx_overlay_types[] = {XX_FILE_TYPE_EXECUTABLE_OVERLAY};
static const xx_format_search_desc xx_overlay_desc = {
    xx_overlay_types, 1, NULL, 0, xx_overlay_open, xx_overlay_close, true
};
static int64_t xx_overlay_size(xx_io_device *device, int64_t base, bool mapped)
{
    return xx_format_search_size(&xx_overlay_desc, device, base, mapped);
}
static char *xx_overlay_version(xx_io_device *device, int64_t base, bool mapped)
{
    return xx_format_search_get_version(&xx_overlay_desc, device, base, mapped);
}
static xx_format_search_state *xx_overlay_search(Abstractextractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_create(&xx_overlay_desc, device, options, pd);
}
static const xx_format_search_info *xx_overlay_current(Abstractextractor *self, xx_format_search_state *state)
{
    (void)self;
    return xx_format_search_current(state);
}
static bool xx_overlay_next(Abstractextractor *self, xx_format_search_state *state, xx_pd_struct *pd)
{
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void xx_overlay_free_search(Abstractextractor *self, xx_format_search_state *state)
{
    (void)self;
    xx_format_search_free(state);
}
static xx_format_search_state *xx_overlay_legacy_create(xx_format_extractor *x, xx_io_device *d, const xx_list_s *o, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_create(&xx_overlay_desc, d, o, pd);
}
static const xx_format_search_info *xx_overlay_legacy_current(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    return xx_format_search_current(s);
}
static bool xx_overlay_legacy_next(xx_format_extractor *x, xx_format_search_state *s, xx_pd_struct *pd)
{
    (void)x;
    return xx_format_search_find_next(s, pd);
}
static void xx_overlay_legacy_free(xx_format_extractor *x, xx_format_search_state *s)
{
    (void)x;
    xx_format_search_free(s);
}
/* The legacy search makes the overlay a family of its own
 * (XFU_SUPPORT_FORMAT_EXECUTABLE_OVERLAY); the complete library reaches the
 * same descriptor through the Abstractextractor below. */
xx_format_extractor xx_executable_overlay_extractor = {xx_overlay_legacy_create, xx_overlay_legacy_current, xx_overlay_legacy_next, xx_overlay_legacy_free};

Abstractextractor *xx_executable_overlay_get_abstract_extractor(void)
{
    static Abstractextractor descriptor = {
        .fast_detect = xx_overlay_no_physical_signature,
        .size = xx_overlay_size,
        .create_format_search = xx_overlay_search,
        .get_current_format_info = xx_overlay_current,
        .format_search_find_next = xx_overlay_next,
        .free_format_search = xx_overlay_free_search,
        .file_type = xx_overlay_reader_type,
        .get_version = xx_overlay_version,
        .create_reader = xx_overlay_open,
        .free_reader = xx_overlay_close,
        .signature = xx_overlay_structural_signature
    };
    return &descriptor;
}
Abstractdetector *xx_executable_overlay_get_abstract_detector(void)
{
    static Abstractdetector descriptor = {
        .fast_detect = xx_overlay_no_physical_signature,
        .file_type = xx_overlay_reader_type
    };
    return &descriptor;
}
