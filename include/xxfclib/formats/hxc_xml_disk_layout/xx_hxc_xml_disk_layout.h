/* SPDX-License-Identifier: MIT */
#ifndef XX_HXC_XML_DISK_LAYOUT_H
#define XX_HXC_XML_DISK_LAYOUT_H
#include "xxfclib/formats/hxc_raw_floppy/xx_hxc_raw_floppy.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_raw_floppy xx_hxc_xml_disk_layout;
XXFC_API void xx_hxc_xml_disk_layout_init(xx_hxc_xml_disk_layout *, xx_io_device *, int64_t);
XXFC_API xx_hxc_xml_disk_layout *xx_hxc_xml_disk_layout_create(xx_io_device *, int64_t);
XXFC_API void xx_hxc_xml_disk_layout_destroy(xx_hxc_xml_disk_layout *);
XXFC_API void xx_hxc_xml_disk_layout_free(xx_hxc_xml_disk_layout *);
XXFC_API bool xx_hxc_xml_disk_layout_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hxc_xml_disk_layout_handle_base_info(Abstractformat *, xx_pd_struct *);
static inline Abstractformat *xx_hxc_xml_disk_layout_to_format(xx_hxc_xml_disk_layout *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
