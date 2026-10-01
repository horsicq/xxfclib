/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/goldenPath/help/wiggle.html */
#include "xxfclib/formats/genomics_wiggle/xx_genomics_wiggle.h"
#include "../xx_fifteenth_root.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    nh_blob b={0};bool ok=false;
    NH_NEED(nh_load(f,&b,pd));ok=f15_parse(f,s,&b);
    if(ok) s->size=(int64_t)b.n;
done:xx_mem_free(b.p);return ok;
}

void xx_genomics_wiggle_init(xx_genomics_wiggle *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GENOMICS_WIGGLE,"genomics_wiggle"); } }
xx_genomics_wiggle *xx_genomics_wiggle_create(xx_io_device *d,int64_t b) { xx_genomics_wiggle *r=(xx_genomics_wiggle *)xx_mem_alloc(sizeof(*r)); if(r) xx_genomics_wiggle_init(r,d,b); return r; }
void xx_genomics_wiggle_destroy(xx_genomics_wiggle *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_genomics_wiggle_free(xx_genomics_wiggle *r) { if(r) { xx_genomics_wiggle_destroy(r); xx_mem_free(r); } }
bool xx_genomics_wiggle_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_genomics_wiggle_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
