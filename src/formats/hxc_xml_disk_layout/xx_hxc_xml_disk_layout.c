/* SPDX-License-Identifier: MIT. HxC XML descriptor entry points.
 * The bounded XML grammar, stored descriptor iterator and mapped-sector
 * unpack callbacks are shared with the explicit raw layout engine. */
#include "xxfclib/formats/hxc_xml_disk_layout/xx_hxc_xml_disk_layout.h"
#include "xxfclib/memory/xx_memory.h"
void xx_hxc_xml_disk_layout_init(xx_hxc_xml_disk_layout *r, xx_io_device *d, int64_t b)
{
    xx_hxc_raw_floppy_init(r, d, b);
    if (r) {
        r->format.file_type = XX_FILE_TYPE_HXC_XML_DISK_LAYOUT;
        xx_format_set_extension(&r->format, "xml");
    }
}
xx_hxc_xml_disk_layout *xx_hxc_xml_disk_layout_create(xx_io_device *d, int64_t b)
{
    xx_hxc_xml_disk_layout *r = (xx_hxc_xml_disk_layout *)xx_mem_alloc(sizeof(*r));
    if (r) {
        xx_hxc_xml_disk_layout_init(r, d, b);
    }
    return r;
}
void xx_hxc_xml_disk_layout_destroy(xx_hxc_xml_disk_layout *r)
{
    xx_hxc_raw_floppy_destroy(r);
}
void xx_hxc_xml_disk_layout_free(xx_hxc_xml_disk_layout *r)
{
    xx_hxc_raw_floppy_free(r);
}
bool xx_hxc_xml_disk_layout_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_hxc_raw_floppy_check_is_valid(f, pd);
}
bool xx_hxc_xml_disk_layout_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_hxc_raw_floppy_handle_base_info(f, pd);
}
