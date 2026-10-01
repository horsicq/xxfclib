/* SPDX-License-Identifier: MIT
 * Independently implemented from https://fhi-aims.org/uploads/documents/FHI-aims.250320.pdf */
#include "xxfclib/formats/aims_geometry/xx_aims_geometry.h"
#include "../xx_sixteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f16_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_aims_geometry_init(xx_aims_geometry *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AIMS_GEOMETRY,"aims_geometry"); } }
xx_aims_geometry *xx_aims_geometry_create(xx_io_device *d,int64_t b) { xx_aims_geometry *r=(xx_aims_geometry *)xx_mem_alloc(sizeof(*r)); if(r) xx_aims_geometry_init(r,d,b); return r; }
void xx_aims_geometry_destroy(xx_aims_geometry *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_aims_geometry_free(xx_aims_geometry *r) { if(r) { xx_aims_geometry_destroy(r); xx_mem_free(r); } }
bool xx_aims_geometry_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_aims_geometry_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
