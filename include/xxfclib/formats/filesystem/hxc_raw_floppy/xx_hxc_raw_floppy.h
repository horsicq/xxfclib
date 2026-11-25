/* SPDX-License-Identifier: MIT */
#ifndef XX_HXC_RAW_FLOPPY_H
#define XX_HXC_RAW_FLOPPY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
    XX_HXC_RAW_SOURCE = 0,
    XX_HXC_RAW_FILL = 1,
    XX_HXC_RAW_INLINE = 2
};
/* Output runs are in logical cylinder/head/sector order. Source offsets are
 * relative to base_address. Inline data contains size*count bytes. */
typedef struct xx_hxc_raw_extent {
    uint64_t offset;
    uint32_t size, count;
    int64_t stride;
    uint8_t fill, mode;
    const uint8_t *data;
} xx_hxc_raw_extent;
typedef struct xx_hxc_raw_profile {
    const char *name;
    uint16_t tracks, sides;
    uint64_t source_size;
    const xx_hxc_raw_extent *extents;
    size_t extent_count;
} xx_hxc_raw_profile;
typedef struct xx_hxc_raw_floppy {
    Abstractformat format;
    const xx_hxc_raw_profile *profile;
    void *owned_profile;
    const xx_list_s *parse_options;
} xx_hxc_raw_floppy;
XXFC_API void xx_hxc_raw_floppy_init(xx_hxc_raw_floppy *, xx_io_device *, int64_t);
XXFC_API xx_hxc_raw_floppy *xx_hxc_raw_floppy_create(xx_io_device *, int64_t);
XXFC_API xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_profile(xx_io_device *, int64_t, const char *);
/* Deep copies the explicitly supplied layout, including inline bytes. */
XXFC_API xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_layout(xx_io_device *, int64_t, const xx_hxc_raw_profile *);
/* Applies a supplied HxC XML descriptor to this explicitly supplied raw device.
 * No path in a descriptor is opened. Outside-source sectors use declared fill. */
XXFC_API xx_hxc_raw_floppy *xx_hxc_raw_floppy_create_layout_xml(xx_io_device *, int64_t, const void *, size_t);
XXFC_API size_t xx_hxc_raw_floppy_profile_count(void);
XXFC_API const xx_hxc_raw_profile *xx_hxc_raw_floppy_profile_at(size_t);
XXFC_API const xx_hxc_raw_profile *xx_hxc_raw_floppy_profile_by_name(const char *);
XXFC_API void xx_hxc_raw_floppy_destroy(xx_hxc_raw_floppy *);
XXFC_API void xx_hxc_raw_floppy_free(xx_hxc_raw_floppy *);
XXFC_API bool xx_hxc_raw_floppy_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hxc_raw_floppy_handle_base_info(Abstractformat *, xx_pd_struct *);
static inline Abstractformat *xx_hxc_raw_floppy_to_format(xx_hxc_raw_floppy *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
